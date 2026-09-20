// Smoke test of the browser frontend's server: starts the real binary with
// `ui --no-open` against a throwaway workshop directory, then talks HTTP to
// it over a socket. Qt-free on purpose — the frontend links no Qt, and this
// test would fail to link if that stopped being true.
//
// The security assertions matter as much as the API ones: a control surface
// for the user's session must refuse a request without the session cookie and
// a request carrying someone else's Host header, and that must not silently
// regress.
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifndef WALLPAPER_ENGINE_BIN
#define WALLPAPER_ENGINE_BIN "/usr/bin/true"
#endif

namespace fs = std::filesystem;

namespace {

int failures = 0;

void check (bool condition, const std::string& what) {
    if (!condition) {
        std::printf ("  FAIL: %s\n", what.c_str ());
        failures++;
    } else {
        std::printf ("  ok: %s\n", what.c_str ());
    }
}

void put (const std::string& path, const std::string& content) {
    std::ofstream file (path, std::ios::trunc);
    file << content;
}

std::string readAll (const std::string& path) {
    std::ifstream file (path, std::ios::binary);
    return std::string ((std::istreambuf_iterator<char> (file)), std::istreambuf_iterator<char> ());
}

// One request, one connection, one response — Connection: close makes the
// server hang up so the read below terminates.
std::string httpRequest (int port, const std::string& request) {
    const int fd = ::socket (AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return {};

    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_port = htons (static_cast<uint16_t> (port));
    ::inet_pton (AF_INET, "127.0.0.1", &address.sin_addr);
    if (::connect (fd, reinterpret_cast<sockaddr*> (&address), sizeof address) != 0) {
        ::close (fd);
        return {};
    }

    ::write (fd, request.data (), request.size ());
    std::string response;
    char buffer[4096];
    ssize_t got = 0;
    while ((got = ::read (fd, buffer, sizeof buffer)) > 0)
        response.append (buffer, static_cast<size_t> (got));
    ::close (fd);
    return response;
}

std::string get (int port, const std::string& path, const std::string& extraHeaders) {
    return httpRequest (port,
                        "GET " + path + " HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string (port) + "\r\n" +
                            extraHeaders + "Connection: close\r\n\r\n");
}

std::string post (int port, const std::string& path, const std::string& body, const std::string& extraHeaders) {
    return httpRequest (port, "POST " + path + " HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string (port) +
                                  "\r\nContent-Type: application/json\r\nContent-Length: " +
                                  std::to_string (body.size ()) + "\r\n" + extraHeaders +
                                  "Connection: close\r\n\r\n" + body);
}

bool startsWith (const std::string& text, const std::string& prefix) {
    return text.compare (0, prefix.size (), prefix) == 0;
}

bool contains (const std::string& text, const std::string& needle) {
    return text.find (needle) != std::string::npos;
}

// "Set-Cookie: lwe_session=<token>" out of a response, as the "name=value"
// pair a client would send back.
std::string sessionCookieOf (const std::string& response) {
    const size_t at = response.find ("lwe_session=");
    if (at == std::string::npos)
        return {};
    const size_t end = response.find (';', at);
    return response.substr (at, (end == std::string::npos ? response.find ('\r', at) : end) - at);
}

std::string bodyOf (const std::string& response) {
    const size_t at = response.find ("\r\n\r\n");
    return at == std::string::npos ? std::string () : response.substr (at + 4);
}

int portOf (const std::string& url) {
    const size_t colon = url.find (':', url.find ("//"));
    return colon == std::string::npos ? 0 : std::atoi (url.c_str () + colon + 1);
}

} // namespace

int main () {
    char tempTemplate[] = "/tmp/lwe-ui-smoke-XXXXXX";
    const char* tempDir = ::mkdtemp (tempTemplate);
    if (tempDir == nullptr) {
        std::printf ("smoke: cannot create a temp directory\n");
        return 1;
    }
    const std::string root = tempDir;
    const std::string workshop = root + "/workshop";
    const std::string wallpaper = workshop + "/843532366";

    fs::create_directories (wallpaper);
    fs::create_directories (root + "/config/lwe-dynamic-wallpaper");
    put (wallpaper + "/project.json", R"({"title":"冒烟壁纸","type":"scene","preview":"cover.png"})");
    put (wallpaper + "/cover.png", std::string (256, 'p'));
    put (root + "/config/lwe-dynamic-wallpaper/config.json",
         R"({"workshopDir":")" + workshop + R"(","screens":{"DP-0":"843532366"}})");

    // the CLI must not talk to the user's real unit or config
    ::setenv ("XDG_CONFIG_HOME", (root + "/config").c_str (), 1);
    ::setenv ("WALLPAPER_ENGINE_UNIT", "lwe-ui-smoke-test", 1);
    // QProcess-style: read the URL the server prints, then talk to it
    const std::string command = std::string ("exec '") + WALLPAPER_ENGINE_BIN + "' ui --no-open 2>/dev/null";
    FILE* server = ::popen (command.c_str (), "r");
    if (server == nullptr) {
        std::printf ("smoke: cannot start the server\n");
        return 1;
    }

    char urlLine[256] = { 0 };
    if (::fgets (urlLine, sizeof urlLine, server) == nullptr) {
        std::printf ("smoke: the server printed no address\n");
        ::pclose (server);
        return 1;
    }
    const std::string url (urlLine);
    const int port = portOf (url);
    std::printf ("ui smoke: server at %s", url.c_str ());
    if (port == 0) {
        std::printf ("smoke: could not parse the port\n");
        ::pclose (server);
        return 1;
    }

    // ---- security boundary
    const std::string anonymousRequest = get (port, "/api/status", "");
    check (startsWith (anonymousRequest, "HTTP/1.1 403"), "a request without the session cookie is refused");

    const std::string home = get (port, "/", "");
    check (startsWith (home, "HTTP/1.1 200"), "the app itself is served before any cookie exists");
    const std::string cookie = sessionCookieOf (home);
    check (!cookie.empty (), "the app response hands out a session cookie");

    const std::string cookieHeader = "Cookie: " + cookie + "\r\n";
    const std::string foreignHost = httpRequest (
        port, "GET /api/status HTTP/1.1\r\nHost: rebind.example.com\r\n" + cookieHeader + "Connection: close\r\n\r\n");
    check (startsWith (foreignHost, "HTTP/1.1 403"), "a foreign Host header is refused even with the cookie");

    // ---- the API
    const std::string status = get (port, "/api/status", cookieHeader);
    check (startsWith (status, "HTTP/1.1 200"), "status answers 200 with the cookie");
    check (contains (bodyOf (status), "\"unit\":\"lwe-ui-smoke-test\""), "status names the unit under test");
    check (contains (bodyOf (status), "843532366"), "status reports the configured wallpaper");

    const std::string library = get (port, "/api/library", cookieHeader);
    check (startsWith (library, "HTTP/1.1 200"), "library answers 200");
    check (contains (bodyOf (library), "冒烟壁纸"), "library carries the UTF-8 title");
    check (contains (bodyOf (library), "\"hasPreview\":true"), "library reports the preview");

    const std::string preview = get (port, "/api/preview/843532366", cookieHeader);
    check (startsWith (preview, "HTTP/1.1 200"), "preview answers 200");
    check (contains (preview, "Content-Type: image/png"), "preview carries its content type");

    const std::string ranged =
        httpRequest (port, "GET /api/preview/843532366 HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string (port) +
                               "\r\n" + cookieHeader + "Range: bytes=0-9\r\nConnection: close\r\n\r\n");
    check (startsWith (ranged, "HTTP/1.1 206"), "a ranged preview request answers 206");

    const std::string missing = get (port, "/api/preview/does-not-exist", cookieHeader);
    check (startsWith (missing, "HTTP/1.1 404"), "an unknown wallpaper is a 404");

    const std::string traversal = get (port, "/api/preview/..%2f..%2fetc%2fpasswd", cookieHeader);
    check (!startsWith (traversal, "HTTP/1.1 200"), "a traversal attempt is not served");

    const std::string unknownRoute = get (port, "/api/nope", cookieHeader);
    check (startsWith (unknownRoute, "HTTP/1.1 404"), "an unknown endpoint is a 404");
    check (contains (bodyOf (unknownRoute), "\"error\""), "an unknown endpoint still answers in JSON");

    const std::string badSwitch = post (port, "/api/switch", R"({"id":"not-a-wallpaper"})", cookieHeader);
    check (startsWith (badSwitch, "HTTP/1.1 400"), "switching to an unknown wallpaper is a 400");
    check (contains (bodyOf (badSwitch), "unknown wallpaper id"), "the 400 says which id was rejected");

    // ---- shutdown: the server exits on request, so nothing is left behind
    const std::string quit = post (port, "/api/quit", "", cookieHeader);
    check (startsWith (quit, "HTTP/1.1 200"), "quit is accepted");

    const int exitCode = ::pclose (server);
    check (exitCode == 0, "the server exited cleanly");
    check (get (port, "/api/status", cookieHeader).empty (), "the socket is gone after quit");

    fs::remove_all (root);
    std::printf ("ui smoke: %s\n", failures == 0 ? "ok" : "FAILED");
    return failures == 0 ? 0 : 1;
}
