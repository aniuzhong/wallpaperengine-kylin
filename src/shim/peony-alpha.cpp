// peony-alpha: the library behind the peony backend of the desktop
// integration. Deployed as libpeony-alpha.so and injected with LD_PRELOAD,
// it makes peony-qt-desktop's desktop background truly transparent.
//
// Terminology: this is an interposer ("the shim"), not a code patch. It
// never rewrites memory; it works purely through ELF symbol resolution.
// LD_PRELOAD places it ahead of libQt5Widgets/libxcb/libX11 in the dynamic
// loader's global search order, so the hooked symbols bind to this library
// first, and dlsym(RTLD_NEXT) chains to the real implementations. The word
// "injection" in the integration docs refers only to deploying the library
// through the environment — nothing in the host process is modified.
//
// Two interception points (both verified against the peony 3.20.4.14 / Qt
// 5.12 symbol tables of Kylin V10 SP1):
//
// 1. The QPixmap(const QString&, const char*, Qt::ImageConversionFlags)
//    constructor. peony's DesktopBackground::setBackground() and
//    switchBackground() load the wallpaper via QPixmap(path). The peony
//    binary references this constructor symbol (confirmed with nm -D), not
//    QPixmap::load. When the path matches an entry of PEONY_ALPHA_WALLPAPER,
//    the pixmap is replaced with a fully transparent one of the same size,
//    so paintEvent's drawPixmap paints transparency and the
//    WA_TranslucentBackground ARGB window truly shows what is beneath it.
//    Zero color deviation, zero fringes.
//
// 2. xcb_change_property / XChangeProperty.
//    peony sets _NET_WM_WINDOW_TYPE_DESKTOP on its desktop window, but
//    ukui-kwin composites DESKTOP windows with 32-bit depth as opaque
//    (src/x11window.cpp: "It's a desktop after all, there is no window
//    below"). Before the property is uploaded, the DESKTOP atom is rewritten
//    to NORMAL. This happens pre-map, so ukui-kwin manages the window as a
//    normal translucent one from the start and no WM restart is needed.
//    The BELOW atom is also appended to _NET_WM_STATE so the desktop window
//    stays in kwin's BelowLayer: above the engine's DesktopLayer, below
//    regular windows. peony explicitly sets SkipTaskbar/SkipPager/
//    SkipSwitcher itself, which is type-independent, so no taskbar or
//    alt-tab pollution occurs.
//
// Without PEONY_ALPHA_WALLPAPER in the environment this library is inert.

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
// Qt headers must precede X11 — X11's Bool/Status/None macros would poison
// the Qt declarations.
#include <QPixmap>
#include <QString>
#include <QSize>
#include <QFileInfo>
#include <QColor>
#include <Qt>

#include <dlfcn.h>
#include <xcb/xcb.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include <fmt/format.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string>

#include <fcntl.h>
#include <pwd.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

inline std::string HomeDir() {
    const char* home = getenv("HOME");
    if (home != nullptr && *home != '\0')
        return home;
    if (const passwd* pw = getpwuid(getuid()); pw != nullptr && pw->pw_dir != nullptr)
        return pw->pw_dir;
    return {};
}

inline std::string DataHome() {
    const char* dataHome = getenv("XDG_DATA_HOME");
    if (dataHome != nullptr && *dataHome != '\0')
        return dataHome;
    return HomeDir() + "/.local/share";
}

inline std::string ProductDataDir() {
    return DataHome() + "/wallpaper-engine";
}

inline std::string PeonyDataDir() {
    return ProductDataDir() + "/peony";
}

inline std::string PeonyShimLog() {
    return PeonyDataDir() + "/peony-alpha.log";
}

// ---- logging ---------------------------------------------------------------
//
// A debugging surface for the injection itself, always on: every process
// the shim maps into logs, unconditionally, to one per-user destination.
// There is no override switch — the volume is intrinsically tiny (a
// handful of lines per peony generation), so plain append-to-file needs no
// rotation, no syslog machinery and no configuration; tests and manual
// experiments isolate themselves with XDG_DATA_HOME. The design keeps
// logging unable to harm the process being interposed:
//
//   - one file descriptor for the process lifetime, opened O_APPEND so all
//     generations append to one file (systemd restarts peony with
//     Restart=on-failure), and O_CLOEXEC so the descriptor can never leak
//     into a child the way the LD_PRELOAD variable once did;
//   - every line is prefixed with the wall clock and the pid: a
//     crash-restart loop then reads as distinct generations instead of one
//     undifferentiated stream, and the timestamps line up with
//     journalctl -u wallpaper-engine-peony;
//   - formatting goes through fmt (header-only, fetched at configure time;
//     the shim's one deliberate third-party payload) into a memory_buffer
//     whose ~500-byte inline storage keeps normal lines allocation-free,
//     and a bad format string is a compile error, never a runtime one;
//   - one write() per line, so concurrent processes cannot interleave
//     mid-line;
//   - write failures are swallowed: losing a debug line beats disturbing
//     the host, and an unresolvable or unwritable destination disables
//     logging entirely.

namespace {

// The one log destination: PeonyShimLog() — literally the same
// function the service layer calls (README "Names" — the file belongs to
// the <host>-<effect> pair), so the two sides cannot drift. Header-only,
// so sharing it costs the shim no link dependency. Environment only, no
// filesystem work: when the directory does not exist (a shim mapped
// outside a setup pass) open() fails and logging stays off rather than the
// shim growing directory-management behavior.
std::string log_path() {
    return PeonyShimLog();
}

int log_fd() {
    static const int fd = []() {
        const std::string path = log_path();
        if (path.empty())
            return -1;
        return open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    }();
    return fd;
}

template <typename... Args>
void shim_log(fmt::format_string<Args...> format, Args&&... args) {
    const int fd = log_fd();
    if (fd < 0)
        return;

    timespec now {};
    clock_gettime(CLOCK_REALTIME, &now);
    fmt::memory_buffer buffer;
    fmt::format_to(fmt::appender(buffer), "{:d}.{:03d} [pid {:d}] ", static_cast<long long>(now.tv_sec),
                   now.tv_nsec / 1000000L, static_cast<long>(getpid()));
    // vformat_to, not format_to: the pack's value categories would make
    // format_to's own deduction disagree with the checked one at this
    // boundary (an rvalue argument deduces Args by value here but Args&&
    // as an lvalue there)
    fmt::vformat_to(fmt::appender(buffer), format, fmt::make_format_args(args...));
    if (buffer.size() == 0 || buffer[buffer.size() - 1] != '\n')
        buffer.push_back('\n');

    size_t written = 0;
    while (written < buffer.size()) {
        const ssize_t n = write(fd, buffer.data() + written, buffer.size() - written);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        written += static_cast<size_t>(n);
    }
}

} // namespace

bool shim_enabled() {
    const char* p = getenv("PEONY_ALPHA_WALLPAPER");
    return p && *p;
}

// ---- 0. do not follow the injection into children --------------------------
//
// LD_PRELOAD is inherited by every process peony spawns, but this library
// only loads into a process that already has Qt: the Qt symbols it uses
// resolve from the host. A non-Qt child — and GIO opens files through
// /bin/sh, so that is most of them — dies at load time with
//    symbol lookup error: libpeony-alpha.so: undefined symbol: qt_version_tag
// before it can run a single instruction. The visible symptom was a desktop
// where only the icons peony handles itself (computer, trash, home) still
// opened, while every real file did nothing at all.
//
// The library is already mapped by the time this constructor runs, so
// dropping the variable costs the desktop nothing and spares every child.
__attribute__((constructor)) static void shim_drop_preload_for_children() {
    shim_log("[shim] interposer mapped, enabled={}\n", shim_enabled() ? 1 : 0);
    unsetenv("LD_PRELOAD");
    shim_log("[shim] dropped LD_PRELOAD so children load clean\n");
}

// ---- 1. QPixmap constructor hook -------------------------------------------
//
// The peony binary references:
//   _ZN7QPixmapC1ERK7QStringPKc6QFlagsIN2Qt19ImageConversionFlagEE
// (nm -D /usr/bin/peony-qt-desktop). The inline QPixmap(fileName) ultimately
// calls this three-argument constructor.
//
// The constructor cannot be defined directly inside an injected library:
// the compiler synthesizes member construction/destruction code, and the
// complete type of the QExplicitlySharedDataPointer<QPlatformPixmap> member
// lives in a private header. Instead, plain functions carrying the exact
// mangled name are defined here (ABI: this=RDI, remaining arguments in
// order); the real constructor is obtained via dlsym(RTLD_NEXT) and invoked
// on the this pointer.

using pixmap_ctor3_t = void (*)(QPixmap*, const QString&, const char*, Qt::ImageConversionFlags);

// PEONY_ALPHA_WALLPAPER is a colon-separated path list; a hit on any path
// (or on any file's basename) triggers the replacement. Every file under
// /var/lib/AccountsService/backgrounds/ also matches: accountsservice
// normalizes user wallpapers into that store and peony loads the
// normalized path at startup, so the list entry may not survive the round
// trip.
static constexpr const char* kAccountsBackgroundDir = "/var/lib/AccountsService/backgrounds/";

bool is_wallpaper_path(const QString& file_name) {
    const char* list = getenv("PEONY_ALPHA_WALLPAPER");
    if (!list || !*list)
        return false;
    if (file_name.isEmpty())
        return false;
    if (file_name.startsWith(kAccountsBackgroundDir))
        return true;
    QString base = QFileInfo(file_name).fileName();
    const char* start = list;
    for (const char* p = list;; p++) {
        if (*p == ':' || *p == '\0') {
            if (p > start) {
                QString candidate = QString::fromLocal8Bit(start, static_cast<int>(p - start));
                if (file_name == candidate || base == candidate)
                    return true;
            }
            if (*p == '\0')
                break;
            start = p + 1;
        }
    }
    return false;
}

void nullify_if_wallpaper(QPixmap* pm, const QString& file_name) {
    if (!pm->isNull() && is_wallpaper_path(file_name)) {
        // same-size fully transparent replacement; fill(transparent) yields
        // valid premultiplied alpha=0 pixels
        QPixmap transparent(pm->size());
        transparent.fill(Qt::transparent);
        *pm = transparent;
        shim_log("[shim] nullified wallpaper pixmap: {} ({}x{})\n", file_name.toUtf8().constData(),
                 pm->size().width(), pm->size().height());
    }
}

// The exported names below are ABI, not API: they exist so the dynamic
// loader binds peony's references here. Their spelling is the mangled C++
// name / the C library symbol and is not negotiable, hence the exemption
// from the usual naming conventions.

extern "C" __attribute__((visibility("default"))) void
_ZN7QPixmapC1ERK7QStringPKc6QFlagsIN2Qt19ImageConversionFlagEE(QPixmap* pm, const QString& file_name,
                                                               const char* format, Qt::ImageConversionFlags flags) {
    static pixmap_ctor3_t real = nullptr;
    if (!real)
        real = reinterpret_cast<pixmap_ctor3_t>(
            dlsym(RTLD_NEXT, "_ZN7QPixmapC1ERK7QStringPKc6QFlagsIN2Qt19ImageConversionFlagEE"));
    real(pm, file_name, format, flags);
    nullify_if_wallpaper(pm, file_name);
}

extern "C" __attribute__((visibility("default"))) void
_ZN7QPixmapC2ERK7QStringPKc6QFlagsIN2Qt19ImageConversionFlagEE(QPixmap* pm, const QString& file_name,
                                                               const char* format, Qt::ImageConversionFlags flags) {
    static pixmap_ctor3_t real = nullptr;
    if (!real)
        real = reinterpret_cast<pixmap_ctor3_t>(
            dlsym(RTLD_NEXT, "_ZN7QPixmapC2ERK7QStringPKc6QFlagsIN2Qt19ImageConversionFlagEE"));
    real(pm, file_name, format, flags);
    nullify_if_wallpaper(pm, file_name);
}

// ---- 2. property rewriting (xcb/Xlib) --------------------------------------

static xcb_atom_t a_wm_type = 0, a_type_desktop = 0, a_type_normal = 0;
static xcb_atom_t a_wm_state = 0, a_state_below = 0;

void ensure_atoms(xcb_connection_t* c) {
    // C++11 thread-safe static initialization: the interning runs exactly
    // once even if several threads race into the property hooks
    static const bool atoms_ready = [c] {
        struct {
            const char* name;
            xcb_atom_t* out;
        } list[] = {
            {"_NET_WM_WINDOW_TYPE", &a_wm_type},
            {"_NET_WM_WINDOW_TYPE_DESKTOP", &a_type_desktop},
            {"_NET_WM_WINDOW_TYPE_NORMAL", &a_type_normal},
            {"_NET_WM_STATE", &a_wm_state},
            {"_NET_WM_STATE_BELOW", &a_state_below},
        };
        for (auto& it : list) {
            xcb_intern_atom_cookie_t ck = xcb_intern_atom(c, 0, strlen(it.name), it.name);
            xcb_intern_atom_reply_t* r = xcb_intern_atom_reply(c, ck, nullptr);
            if (r) {
                *it.out = r->atom;
                free(r);
            }
        }
        shim_log("[shim] xcb atoms ready (type={} desktop={} normal={} state={} below={})\n",
                 (unsigned long)a_wm_type, (unsigned long)a_type_desktop, (unsigned long)a_type_normal,
                 (unsigned long)a_wm_state, (unsigned long)a_state_below);
        return true;
    }();
    (void)atoms_ready;
}

using xcb_ccp_t = xcb_void_cookie_t (*)(xcb_connection_t*, uint8_t, xcb_window_t, xcb_atom_t, xcb_atom_t, uint8_t,
                                        uint32_t, const void*);
static xcb_ccp_t real_xcb_ccp = nullptr;

__attribute__((visibility("default"))) xcb_void_cookie_t
xcb_change_property(xcb_connection_t* c, uint8_t mode, xcb_window_t window, xcb_atom_t property, xcb_atom_t type,
                    uint8_t format, uint32_t data_len, const void* data) {
    if (!real_xcb_ccp)
        real_xcb_ccp = reinterpret_cast<xcb_ccp_t>(dlsym(RTLD_NEXT, "xcb_change_property"));

    if (shim_enabled() && type == XCB_ATOM_ATOM && format == 32 && data_len > 0 && data_len <= 32) {
        ensure_atoms(c);
        const auto* atoms = static_cast<const xcb_atom_t*>(data);

        if (property == a_wm_type) {
            xcb_atom_t buf[32];
            memcpy(buf, data, data_len * 4);
            bool changed = false;
            for (uint32_t i = 0; i < data_len; i++) {
                if (buf[i] == a_type_desktop) {
                    buf[i] = a_type_normal;
                    changed = true;
                }
            }
            if (changed) {
                shim_log("[shim] win 0x{:x}: WINDOW_TYPE DESKTOP -> NORMAL\n", window);
                return real_xcb_ccp(c, mode, window, property, type, format, data_len, buf);
            }
        } else if (property == a_wm_state) {
            bool has_below = false;
            for (uint32_t i = 0; i < data_len; i++)
                has_below |= (atoms[i] == a_state_below);
            if (!has_below && data_len < 32) {
                xcb_atom_t buf[33];
                memcpy(buf, data, data_len * 4);
                buf[data_len++] = a_state_below;
                shim_log("[shim] win 0x{:x}: appended STATE BELOW ({} atoms)\n", window, data_len);
                return real_xcb_ccp(c, mode, window, property, type, format, data_len, buf);
            }
        }
    }
    return real_xcb_ccp(c, mode, window, property, type, format, data_len, data);
}

// Xlib fallback (KWindowSystem and other paths may go through Xlib; note the
// 32-bit property data is an array of longs)
using xlib_ccp_t = int (*)(Display*, Window, Atom, Atom, int, int, const unsigned char*, int);
static xlib_ccp_t real_xlib_ccp = nullptr;

static Atom xlib_atom(Display* d, const char* name) {
    return XInternAtom(d, name, False);
}

__attribute__((visibility("default"))) int
XChangeProperty(Display* display, Window w, Atom property, Atom type, int format, int mode,
                const unsigned char* data, int nelements) {
    if (!real_xlib_ccp)
        real_xlib_ccp = reinterpret_cast<xlib_ccp_t>(dlsym(RTLD_NEXT, "XChangeProperty"));

    if (shim_enabled() && format == 32 && nelements > 0 && nelements <= 32) {
        static Atom x_wm_type = 0, x_type_desktop = 0, x_type_normal = 0, x_wm_state = 0, x_state_below = 0;
        // C++11 thread-safe static initialization (same rationale as the xcb
        // path): the first caller's display interns the atoms exactly once
        static const bool xlib_atoms_ready = [&] {
            x_wm_type = xlib_atom(display, "_NET_WM_WINDOW_TYPE");
            x_type_desktop = xlib_atom(display, "_NET_WM_WINDOW_TYPE_DESKTOP");
            x_type_normal = xlib_atom(display, "_NET_WM_WINDOW_TYPE_NORMAL");
            x_wm_state = xlib_atom(display, "_NET_WM_STATE");
            x_state_below = xlib_atom(display, "_NET_WM_STATE_BELOW");
            shim_log("[shim] xlib atoms ready\n");
            return true;
        }();
        (void)xlib_atoms_ready;

        if (type == XA_ATOM && property == x_wm_type) {
            const auto* in = reinterpret_cast<const unsigned long*>(data);
            unsigned long buf[32];
            bool changed = false;
            for (int i = 0; i < nelements; i++) {
                buf[i] = in[i];
                if (buf[i] == (unsigned long)x_type_desktop) {
                    buf[i] = (unsigned long)x_type_normal;
                    changed = true;
                }
            }
            if (changed) {
                shim_log("[shim] xlib win 0x{:x}: DESKTOP -> NORMAL\n", (unsigned long)w);
                return real_xlib_ccp(display, w, property, type, format, mode, (unsigned char*)buf, nelements);
            }
        } else if (type == XA_ATOM && property == x_wm_state) {
            const auto* in = reinterpret_cast<const unsigned long*>(data);
            bool has_below = false;
            for (int i = 0; i < nelements; i++)
                has_below |= (in[i] == (unsigned long)x_state_below);
            if (!has_below) {
                unsigned long buf[33];
                for (int i = 0; i < nelements; i++)
                    buf[i] = in[i];
                buf[nelements++] = (unsigned long)x_state_below;
                shim_log("[shim] xlib win 0x{:x}: appended BELOW\n", (unsigned long)w);
                return real_xlib_ccp(display, w, property, type, format, mode, (unsigned char*)buf, nelements);
            }
        }
    }
    return real_xlib_ccp(display, w, property, type, format, mode, data, nelements);
}
