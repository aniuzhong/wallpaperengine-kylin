#include "ui.h"

#include "config.h"
#include "engineprocess.h"
#include "engineunit.h"
#include "integration.h"
#include "library.h"
#include "report.h"
#include "uiassets.h"
#include "uiicon.h" // generated from icon/wallpaper-engine-256.png
#include "uilaunch.h"
#include "uivalidate.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <sys/stat.h>

namespace Ui {

namespace {

using SteadyClock = std::chrono::steady_clock;

// The page polls every few seconds, and this much silence means the window
// is gone and the control surface should go with it. Generous on purpose:
// Chromium throttles timers in an occluded (minimised) window down to about
// one a minute, and the server must not die because the user looked away.
constexpr int kIdleTimeoutSec = 120;
constexpr int kPropertiesTimeoutMs = 30000;

// One --list-properties run, which takes the engine a while: the HTTP
// request returns a job id immediately and the page polls for the answer.
struct PropertyJob {
    bool done = false;
    int exitCode = 0;
    std::string output;
};

struct State {
    std::string token;     // empty means "could not be generated": refuse to serve
    int port = 0;          // known only after bind
    std::atomic<long long> lastRequestMs { 0 };
    std::atomic<bool> quit { false }; // set by /api/quit, acted on by the watchdog
    std::mutex jobsMutex;
    std::map<std::string, PropertyJob> jobs;
    int jobCounter = 0;
};

long long nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(SteadyClock::now().time_since_epoch()).count();
}

// The token is the entire boundary between a local process and this control
// surface, so an unreadable /dev/urandom is fatal rather than a reason to
// fall back on something predictable.
std::string randomToken() {
    unsigned char bytes[16] = { 0 };
    FILE* source = std::fopen("/dev/urandom", "rb");
    if (source == nullptr)
        return {};
    const size_t got = std::fread(bytes, 1, sizeof bytes, source);
    std::fclose(source);
    if (got != sizeof bytes)
        return {};

    static const char* kHex = "0123456789abcdef";
    std::string token;
    token.reserve(sizeof bytes * 2);
    for (const unsigned char byte : bytes) {
        token += kHex[byte >> 4];
        token += kHex[byte & 0x0f];
    }
    return token;
}

lwe::Error errorOf(lwe::Error::Kind kind, const std::string& message) {
    lwe::Error error;
    error.kind = kind;
    error.message = message;
    return error;
}

int statusFor(lwe::Error::Kind kind) {
    switch (kind) {
    case lwe::Error::NoError:
        return 200;
    case lwe::Error::InvalidInput:
        return 400;
    case lwe::Error::NoSuchUnit:
        return 404;
    case lwe::Error::JobFailed:
        return 502;
    case lwe::Error::BusUnreachable:
        return 503;
    case lwe::Error::FileError:
    case lwe::Error::CorruptConfig:
        return 500;
    case lwe::Error::Unknown:
        break;
    }
    return 500;
}

void sendJson(httplib::Response& res, const nlohmann::json& body, int status = 200) {
    res.status = status;
    res.set_content(body.dump(), "application/json; charset=utf-8");
}

void sendError(httplib::Response& res, const lwe::Error& error) {
    sendJson(res, Report::error(error), statusFor(error.kind));
}

// For "the thing you asked for is not here" — a missing preview or an id that
// does not exist. That is not a service failure, so it does not go through
// the error-kind mapping: it is a 404 with the same JSON shape.
void sendNotFound(httplib::Response& res, const std::string& message) {
    nlohmann::json detail;
    detail["kind"] = "not-found";
    detail["message"] = message;
    nlohmann::json body;
    body["error"] = std::move(detail);
    sendJson(res, body, 404);
}

// ---- projections ----------------------------------------------------------

nlohmann::json statusBody(const Config& config) {
    lwe::Error busError;
    const std::string state = EngineUnit::unitState(&busError);
    // the unit file is what systemd runs; config.json is the editor's draft
    std::map<std::string, std::string> screens = EngineUnit::unitBackgrounds();
    if (screens.empty())
        screens = config.screens;

    nlohmann::json body = Report::status(EngineUnit::unitName(), state, screens, config.enginePath);
    if (!busError.ok())
        body["status"]["busError"] = lwe::describe(busError);
    return body;
}

nlohmann::json configBody(const Config& config) {
    nlohmann::json settings;
    settings["enginePath"] = config.enginePath;
    settings["assetsDir"] = config.assetsDir;
    settings["workshopDir"] = config.workshopDir;
    settings["display"] = config.display;
    settings["scaling"] = config.scaling;
    settings["clamp"] = config.clamp;
    settings["fps"] = config.fps;
    settings["volume"] = config.volume;
    settings["silent"] = config.silent;
    settings["fullscreenPause"] = config.fullscreenPause;
    settings["automute"] = config.automute;
    settings["audioProcessing"] = config.audioProcessing;
    settings["disableParticles"] = config.disableParticles;
    settings["disableMouse"] = config.disableMouse;
    settings["disableParallax"] = config.disableParallax;

    nlohmann::json body;
    body["config"] = std::move(settings);
    return body;
}

nlohmann::json parseBody(const httplib::Request& req, bool* ok) {
    *ok = true;
    if (req.body.empty())
        return nlohmann::json::object();
    try {
        return nlohmann::json::parse(req.body);
    } catch (const std::exception&) {
        *ok = false;
        return nlohmann::json::object();
    }
}

// ---- file serving ---------------------------------------------------------

std::string mimeTypeFor(const std::string& path) {
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos)
        return "application/octet-stream";

    std::string extension = path.substr(dot + 1);
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (extension == "jpg" || extension == "jpeg")
        return "image/jpeg";
    if (extension == "png")
        return "image/png";
    if (extension == "gif")
        return "image/gif";
    if (extension == "webp")
        return "image/webp";
    if (extension == "bmp")
        return "image/bmp";
    if (extension == "mp4" || extension == "m4v")
        return "video/mp4";
    if (extension == "webm")
        return "video/webm";
    if (extension == "svg")
        return "image/svg+xml";
    return "application/octet-stream";
}

// Serve one file, sliced when the client asked for a range: browsers insist
// on partial responses to seek a video preview, and a preview may well be
// one.
void sendFile(const httplib::Request& req, httplib::Response& res, const std::string& path) {
    struct stat info {};
    if (::stat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode)) {
        sendError(res, errorOf(lwe::Error::FileError, "cannot read " + path));
        return;
    }

    auto file = std::make_shared<std::ifstream>(path, std::ios::binary);
    if (!file->is_open()) {
        sendError(res, errorOf(lwe::Error::FileError, "cannot open " + path));
        return;
    }

    res.status = req.ranges.empty() ? 200 : 206;
    res.set_content_provider(
        static_cast<size_t>(info.st_size), mimeTypeFor(path),
        [file](size_t offset, size_t length, httplib::DataSink& sink) {
            file->seekg(static_cast<std::streamoff>(offset));
            if (!file->good())
                return false;
            char buffer[16384];
            size_t remaining = length;
            while (remaining > 0) {
                const size_t chunk = std::min(remaining, sizeof buffer);
                file->read(buffer, static_cast<std::streamsize>(chunk));
                const std::streamsize got = file->gcount();
                if (got <= 0)
                    break;
                if (!sink.write(buffer, static_cast<size_t>(got)))
                    return false;
                remaining -= static_cast<size_t>(got);
            }
            return true;
        });
}

// ---- request handling -----------------------------------------------------

// Requests that must work before the browser holds the session cookie: the
// app itself carries nothing private, and serving it is what hands the
// cookie out.
bool isPublicAsset(const httplib::Request& req) {
    return req.method == "GET" && (req.path == "/" || req.path == "/app.js" || req.path == "/style.css" ||
                                   req.path == "/icon.png");
}

bool authorized(const httplib::Request& req, const State& state) {
    if (state.token.empty())
        return false;
    if (!hostAllowed(req.get_header_value("Host"), state.port))
        return false;
    return tokenMatches(sessionTokenFromCookie(req.get_header_value("Cookie"), kSessionCookie), state.token);
}

void registerRoutes(httplib::Server& server, const std::shared_ptr<State>& state) {
    server.set_logger([](const httplib::Request&, const httplib::Response&) {});

    server.set_exception_handler([](const httplib::Request&, httplib::Response& res, std::exception_ptr failure) {
        std::string message = "internal error";
        try {
            std::rethrow_exception(failure);
        } catch (const std::exception& error) {
            message = error.what();
        } catch (...) {
        }
        sendError(res, errorOf(lwe::Error::Unknown, message));
    });

    // An unmatched route is a real 404: keep the status, and give it the same
    // JSON shape as everything else instead of the library's HTML page.
    server.set_error_handler([](const httplib::Request&, httplib::Response& res) {
        if (res.status != 404)
            return;
        nlohmann::json detail;
        detail["kind"] = "not-found";
        detail["message"] = "no such endpoint";
        nlohmann::json body;
        body["error"] = std::move(detail);
        res.set_content(body.dump(), "application/json; charset=utf-8");
    });

    // Every request passes the boundary first, and every request counts as
    // liveness: the page does not have to keep a separate timer honest for
    // the server to notice it is still there.
    server.set_pre_routing_handler([state](const httplib::Request& req, httplib::Response& res) {
        state->lastRequestMs = nowMs();
        if (isPublicAsset(req))
            return httplib::Server::HandlerResponse::Unhandled;
        if (!authorized(req, *state)) {
            nlohmann::json detail;
            detail["kind"] = "forbidden";
            detail["message"] = "missing or invalid session token";
            nlohmann::json body;
            body["error"] = std::move(detail);
            sendJson(res, body, 403);
            return httplib::Server::HandlerResponse::Handled;
        }
        return httplib::Server::HandlerResponse::Unhandled;
    });

    // ---- the app

    server.Get("/", [state](const httplib::Request&, httplib::Response& res) {
        // SameSite=Strict is what stops a page on another origin from riding
        // this cookie into the control API; HttpOnly keeps any script from
        // reading it. The token stays out of the URL on purpose — a URL ends
        // up in argv, and /proc/<pid>/cmdline is readable by every user on
        // the machine.
        res.set_header("Set-Cookie", std::string(kSessionCookie) + "=" + state->token +
                                         "; Path=/; SameSite=Strict; HttpOnly");
        res.set_header("Cache-Control", "no-store");
        res.set_content(indexHtml(), "text/html; charset=utf-8");
    });

    server.Get("/style.css", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Cache-Control", "no-store");
        res.set_content(styleCss(), "text/css; charset=utf-8");
    });

    server.Get("/app.js", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Cache-Control", "no-store");
        res.set_content(appJs(), "application/javascript; charset=utf-8");
    });

    // The same icon the launcher installs, so the app window carries it too:
    // in --app= mode the window icon comes from the page's favicon.
    server.Get("/icon.png", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Cache-Control", "max-age=3600");
        res.set_content(reinterpret_cast<const char*>(kIconPng), kIconPngSize, "image/png");
    });

    server.Post("/api/heartbeat", [](const httplib::Request&, httplib::Response& res) {
        sendJson(res, { { "ok", true } });
    });

    // The page cannot close its own window (it was opened by the browser,
    // not by a script), so quitting means "stop the server and tell me" —
    // the watchdog does the stopping, because a handler must not tear down
    // the server it is running on.
    server.Post("/api/quit", [state](const httplib::Request&, httplib::Response& res) {
        sendJson(res, { { "stopping", true } });
        state->quit = true;
    });

    // ---- reading state

    server.Get("/api/status", [](const httplib::Request&, httplib::Response& res) {
        sendJson(res, statusBody(Config::load()));
    });

    server.Get("/api/library", [](const httplib::Request&, httplib::Response& res) {
        const Config config = Config::load();
        sendJson(res, Report::library(scanLibrary(config.workshopDir)));
    });

    server.Get("/api/screens", [](const httplib::Request&, httplib::Response& res) {
        sendJson(res, { { "screens", EngineUnit::screenNames() } });
    });

    server.Get("/api/config", [](const httplib::Request&, httplib::Response& res) {
        sendJson(res, configBody(Config::load()));
    });

    server.Get("/api/integration", [](const httplib::Request&, httplib::Response& res) {
        const Integration::Status status = Integration::detect();
        nlohmann::json detail;
        detail["peonyPid"] = status.peonyPid;
        detail["shimLoaded"] = status.shimLoaded;
        detail["configured"] = status.configured();
        detail["shimPath"] = Integration::locateShim();

        nlohmann::json body;
        body["integration"] = std::move(detail);
        sendJson(res, body);
    });

    // ---- changing state

    server.Post("/api/switch", [](const httplib::Request& req, httplib::Response& res) {
        bool parsed = false;
        const nlohmann::json body = parseBody(req, &parsed);
        if (!parsed) {
            sendError(res, errorOf(lwe::Error::InvalidInput, "request body is not JSON"));
            return;
        }

        const std::string id = body.value("id", std::string());
        if (!isSafeWallpaperId(id)) {
            sendError(res, errorOf(lwe::Error::InvalidInput, "missing or unusable wallpaper id"));
            return;
        }

        const Config config = Config::load();
        const std::vector<WallpaperEntry> library = scanLibrary(config.workshopDir);
        const bool known = std::any_of(library.begin(), library.end(),
                                       [&id](const WallpaperEntry& entry) { return entry.id == id; });
        if (!known) {
            sendError(res, errorOf(lwe::Error::InvalidInput, "unknown wallpaper id " + id));
            return;
        }

        std::string screen = body.value("screen", std::string());
        if (screen.empty())
            screen = EngineUnit::defaultScreenFor(config, EngineUnit::fallbackScreenName());
        if (screen.empty()) {
            sendError(res, errorOf(lwe::Error::InvalidInput, "no screen to target"));
            return;
        }

        lwe::Error error;
        if (!EngineUnit::applyConfig(EngineUnit::assignScreen(config, screen, id), &error)) {
            sendError(res, error);
            return;
        }
        // 202, not 200: the unit was restarted, and "restarted" is not
        // "rendering". The page polls /api/status until the unit is active,
        // which is also how it notices an engine that dies on startup.
        sendJson(res, statusBody(Config::load()), 202);
    });

    // start/stop/restart differ only in which unit operation they call
    const auto lifecycle = [](bool (*operation)(lwe::Error*)) {
        return [operation](const httplib::Request&, httplib::Response& res) {
            lwe::Error error;
            if (!operation(&error)) {
                sendError(res, error);
                return;
            }
            sendJson(res, statusBody(Config::load()), 202);
        };
    };
    server.Post("/api/start", lifecycle(&EngineUnit::startUnit));
    server.Post("/api/stop", lifecycle(&EngineUnit::stopUnit));
    server.Post("/api/restart", lifecycle(&EngineUnit::restartUnit));

    server.Post("/api/config", [](const httplib::Request& req, httplib::Response& res) {
        bool parsed = false;
        const nlohmann::json patch = parseBody(req, &parsed);
        if (!parsed) {
            sendError(res, errorOf(lwe::Error::InvalidInput, "request body is not JSON"));
            return;
        }

        const Config updated = Config::load().patched(patch);
        lwe::Error error;
        if (!EngineUnit::applyConfig(updated, &error)) {
            sendError(res, error);
            return;
        }
        sendJson(res, configBody(updated), 202);
    });

    server.Post("/api/integration/setup", [](const httplib::Request&, httplib::Response& res) {
        lwe::Error error;
        if (!Integration::setup(&error)) {
            sendError(res, error);
            return;
        }
        sendJson(res, { { "configured", true } });
    });

    // The way back out of the injection. Without it the only escape was to
    // work out by hand that peony runs under a transient unit with the shim
    // preloaded — and that the desktop's own wallpaper had been replaced.
    server.Post("/api/integration/remove", [](const httplib::Request&, httplib::Response& res) {
        lwe::Error error;
        if (!Integration::teardown(&error)) {
            sendError(res, error);
            return;
        }
        sendJson(res, { { "configured", false } });
    });

    // ---- previews

    server.Get(R"(/api/preview/([^/]+))", [](const httplib::Request& req, httplib::Response& res) {
        const std::string id = req.matches[1];
        if (!isSafeWallpaperId(id)) {
            sendError(res, errorOf(lwe::Error::InvalidInput, "unusable wallpaper id"));
            return;
        }

        const Config config = Config::load();
        for (const WallpaperEntry& entry : scanLibrary(config.workshopDir)) {
            if (entry.id != id)
                continue;
            if (entry.previewPath.empty()) {
                sendNotFound(res, "this wallpaper ships no preview");
                return;
            }
            sendFile(req, res, entry.previewPath);
            return;
        }
        sendNotFound(res, "unknown wallpaper id " + id);
    });

    // ---- engine properties (too slow for one request)

    server.Get(R"(/api/properties/([^/]+))", [state](const httplib::Request& req, httplib::Response& res) {
        const std::string id = req.matches[1];
        if (!isSafeWallpaperId(id)) {
            sendError(res, errorOf(lwe::Error::InvalidInput, "unusable wallpaper id"));
            return;
        }

        const Config config = Config::load();
        std::string jobId;
        {
            std::lock_guard<std::mutex> lock(state->jobsMutex);
            jobId = std::to_string(++state->jobCounter);
            state->jobs[jobId] = PropertyJob {};
        }

        std::thread([state, config, id, jobId] {
            std::string output;
            bool timedOut = false;
            const int exitCode = EngineProcess::runCaptured(
                config.enginePath, { "--list-properties", "--assets-dir", config.assetsDir, id },
                kPropertiesTimeoutMs, &output, &timedOut);

            std::lock_guard<std::mutex> lock(state->jobsMutex);
            PropertyJob& job = state->jobs[jobId];
            job.done = true;
            job.exitCode = exitCode;
            job.output = timedOut ? "the engine did not answer in time" : output;
        }).detach();

        sendJson(res, { { "job", jobId } }, 202);
    });

    server.Get(R"(/api/jobs/([^/]+))", [state](const httplib::Request& req, httplib::Response& res) {
        const std::string jobId = req.matches[1];
        std::lock_guard<std::mutex> lock(state->jobsMutex);
        const auto job = state->jobs.find(jobId);
        if (job == state->jobs.end()) {
            sendError(res, errorOf(lwe::Error::InvalidInput, "unknown job " + jobId));
            return;
        }
        sendJson(res, { { "done", job->second.done },
                        { "exitCode", job->second.exitCode },
                        { "output", job->second.output } });
    });
}

struct Options {
    int port = 0; // 0: let the OS pick one
    bool open = true;
    bool help = false;
};

bool parseOptions(const std::vector<std::string>& args, Options* options, std::string* error) {
    for (size_t i = 0; i < args.size(); i++) {
        const std::string& arg = args[i];
        if (arg == "--no-open") {
            options->open = false;
        } else if (arg == "--open") {
            options->open = true;
        } else if (arg == "--help" || arg == "-h") {
            options->help = true;
        } else if (arg == "--port" && i + 1 < args.size()) {
            options->port = std::atoi(args[++i].c_str());
        } else if (arg.rfind("--port=", 0) == 0) {
            options->port = std::atoi(arg.c_str() + 7);
        } else {
            *error = "unknown option: " + arg;
            return false;
        }
    }
    if (options->port < 0 || options->port > 65535) {
        *error = "port out of range: " + std::to_string(options->port);
        return false;
    }
    return true;
}

} // namespace

int runUi(const std::vector<std::string>& args) {
    Options options;
    std::string optionError;
    if (!parseOptions(args, &options, &optionError)) {
        std::fprintf(stderr, "ui: %s\n", optionError.c_str());
        return 2;
    }
    if (options.help) {
        std::printf("usage: wallpaper-engine ui [--port N] [--no-open]\n"
                    "\n"
                    "Serves the wallpaper browser on 127.0.0.1 and opens it in the\n"
                    "default browser. The port defaults to one the OS picks; --no-open\n"
                    "prints the address instead of launching anything. The server exits\n"
                    "after %d seconds without a request from the page.\n",
                    kIdleTimeoutSec);
        return 0;
    }

    auto state = std::make_shared<State>();
    state->token = randomToken();
    if (state->token.empty()) {
        std::fprintf(stderr, "ui: cannot read /dev/urandom — refusing to serve without a session token\n");
        return 1;
    }

    httplib::Server server;
    server.set_read_timeout(30, 0);
    server.set_write_timeout(30, 0);
    registerRoutes(server, state);

    // 127.0.0.1 only: this must never be reachable from the network, and an
    // OS-assigned port keeps two windows from colliding.
    const int port = server.bind_to_any_port("127.0.0.1", options.port);
    if (port <= 0) {
        std::fprintf(stderr, "ui: cannot bind 127.0.0.1:%d\n", options.port);
        return 1;
    }
    state->port = port;
    state->lastRequestMs = nowMs();

    const std::string url = "http://127.0.0.1:" + std::to_string(port) + "/";
    // the smoke test and --no-open users read the address from here
    std::printf("%s\n", url.c_str());
    std::fflush(stdout);

    if (options.open) {
        std::string launchError;
        if (!openBrowser(url, &launchError))
            std::fprintf(stderr, "ui: %s — open %s yourself\n", launchError.c_str(), url.c_str());
    }

    std::atomic<bool> running { true };
    std::thread watchdog([&server, state, &running] {
        while (running.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            const bool idle =
                nowMs() - state->lastRequestMs.load() > static_cast<long long>(kIdleTimeoutSec) * 1000;
            if (state->quit.load() || idle) {
                server.stop();
                return;
            }
        }
    });

    server.listen_after_bind();
    running = false;
    watchdog.join();
    return 0;
}

} // namespace Ui
