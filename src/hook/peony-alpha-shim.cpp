// peony-alpha-shim: makes peony-qt-desktop's desktop background truly
// transparent (LD_PRELOAD injection).
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

#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <cstdio>

// ---- logging

static void shim_log(const char* fmt, ...) {
    const char* path = getenv("PEONY_ALPHA_LOG");
    if (!path || !*path)
        return;
    if (FILE* f = fopen(path, "a")) {
        va_list ap;
        va_start(ap, fmt);
        vfprintf(f, fmt, ap);
        va_end(ap);
        fclose(f);
    }
}

static bool shim_enabled() {
    const char* p = getenv("PEONY_ALPHA_WALLPAPER");
    return p && *p;
}

// ---- 0. do not follow the injection into children
//
// LD_PRELOAD is inherited by every process peony spawns, but this library
// only loads into a process that already has Qt: the Qt symbols it uses
// resolve from the host. A non-Qt child — and GIO opens files through
// /bin/sh, so that is most of them — dies at load time with
//    symbol lookup error: libpeony-alpha-shim.so: undefined symbol: qt_version_tag
// before it can run a single instruction. The visible symptom was a desktop
// where only the icons peony handles itself (computer, trash, home) still
// opened, while every real file did nothing at all.
//
// The library is already mapped by the time this constructor runs, so
// dropping the variable costs the desktop nothing and spares every child.
__attribute__((constructor)) static void shim_drop_preload_for_children() {
    unsetenv("LD_PRELOAD");
    shim_log("[shim] dropped LD_PRELOAD so children load clean\n");
}

// ---- 1. QPixmap constructor hook
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

static bool is_wallpaper_path(const QString& fileName) {
    const char* list = getenv("PEONY_ALPHA_WALLPAPER");
    if (!list || !*list)
        return false;
    if (fileName.isEmpty())
        return false;
    if (fileName.startsWith(kAccountsBackgroundDir))
        return true;
    QString base = QFileInfo(fileName).fileName();
    const char* start = list;
    for (const char* p = list;; p++) {
        if (*p == ':' || *p == '\0') {
            if (p > start) {
                QString candidate = QString::fromLocal8Bit(start, static_cast<int>(p - start));
                if (fileName == candidate || base == candidate)
                    return true;
            }
            if (*p == '\0')
                break;
            start = p + 1;
        }
    }
    return false;
}

static void nullify_if_wallpaper(QPixmap* pm, const QString& fileName) {
    if (!pm->isNull() && is_wallpaper_path(fileName)) {
        // same-size fully transparent replacement; fill(transparent) yields
        // valid premultiplied alpha=0 pixels
        QPixmap transparent(pm->size());
        transparent.fill(Qt::transparent);
        *pm = transparent;
        shim_log("[shim] nullified wallpaper pixmap: %s (%dx%d)\n", fileName.toUtf8().constData(),
                 pm->size().width(), pm->size().height());
    }
}

extern "C" __attribute__((visibility("default"))) void
_ZN7QPixmapC1ERK7QStringPKc6QFlagsIN2Qt19ImageConversionFlagEE(QPixmap* pm, const QString& fileName,
                                                               const char* format, Qt::ImageConversionFlags flags) {
    static pixmap_ctor3_t real = nullptr;
    if (!real)
        real = reinterpret_cast<pixmap_ctor3_t>(
            dlsym(RTLD_NEXT, "_ZN7QPixmapC1ERK7QStringPKc6QFlagsIN2Qt19ImageConversionFlagEE"));
    real(pm, fileName, format, flags);
    nullify_if_wallpaper(pm, fileName);
}

extern "C" __attribute__((visibility("default"))) void
_ZN7QPixmapC2ERK7QStringPKc6QFlagsIN2Qt19ImageConversionFlagEE(QPixmap* pm, const QString& fileName,
                                                               const char* format, Qt::ImageConversionFlags flags) {
    static pixmap_ctor3_t real = nullptr;
    if (!real)
        real = reinterpret_cast<pixmap_ctor3_t>(
            dlsym(RTLD_NEXT, "_ZN7QPixmapC2ERK7QStringPKc6QFlagsIN2Qt19ImageConversionFlagEE"));
    real(pm, fileName, format, flags);
    nullify_if_wallpaper(pm, fileName);
}

// ---- 2. property rewriting (xcb/Xlib)

static xcb_atom_t a_wm_type = 0, a_type_desktop = 0, a_type_normal = 0;
static xcb_atom_t a_wm_state = 0, a_state_below = 0;

static void ensure_atoms(xcb_connection_t* c) {
    // C++11 thread-safe static initialization: the interning runs exactly
    // once even if several threads race into the property hooks
    static const bool atoms_ready = [c] {
        struct {
            const char* name;
            xcb_atom_t* out;
        } list[] = {
            { "_NET_WM_WINDOW_TYPE", &a_wm_type },         { "_NET_WM_WINDOW_TYPE_DESKTOP", &a_type_desktop },
            { "_NET_WM_WINDOW_TYPE_NORMAL", &a_type_normal }, { "_NET_WM_STATE", &a_wm_state },
            { "_NET_WM_STATE_BELOW", &a_state_below },
        };
        for (auto& it : list) {
            xcb_intern_atom_cookie_t ck = xcb_intern_atom(c, 0, strlen(it.name), it.name);
            xcb_intern_atom_reply_t* r = xcb_intern_atom_reply(c, ck, nullptr);
            if (r) {
                *it.out = r->atom;
                free(r);
            }
        }
        shim_log("[shim] xcb atoms ready (type=%lu desktop=%lu normal=%lu state=%lu below=%lu)\n",
                 (unsigned long) a_wm_type, (unsigned long) a_type_desktop, (unsigned long) a_type_normal,
                 (unsigned long) a_wm_state, (unsigned long) a_state_below);
        return true;
    } ();
    (void) atoms_ready;
}

using xcb_ccp_t = xcb_void_cookie_t (*) (xcb_connection_t*, uint8_t, xcb_window_t, xcb_atom_t, xcb_atom_t, uint8_t,
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
                shim_log("[shim] win 0x%x: WINDOW_TYPE DESKTOP -> NORMAL\n", window);
                return real_xcb_ccp(c, mode, window, property, type, format, data_len, buf);
            }
        } else if (property == a_wm_state) {
            bool has_below = false;
            for (uint32_t i = 0; i < data_len; i++) has_below |= (atoms[i] == a_state_below);
            if (!has_below && data_len < 32) {
                xcb_atom_t buf[33];
                memcpy(buf, data, data_len * 4);
                buf[data_len++] = a_state_below;
                shim_log("[shim] win 0x%x: appended STATE BELOW (%u atoms)\n", window, data_len);
                return real_xcb_ccp(c, mode, window, property, type, format, data_len, buf);
            }
        }
    }
    return real_xcb_ccp(c, mode, window, property, type, format, data_len, data);
}

// Xlib fallback (KWindowSystem and other paths may go through Xlib; note the
// 32-bit property data is an array of longs)
using xlib_ccp_t = int (*) (Display*, Window, Atom, Atom, int, int, const unsigned char*, int);
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
        } ();
        (void) xlib_atoms_ready;

        if (type == XA_ATOM && property == x_wm_type) {
            const auto* in = reinterpret_cast<const unsigned long*>(data);
            unsigned long buf[32];
            bool changed = false;
            for (int i = 0; i < nelements; i++) {
                buf[i] = in[i];
                if (buf[i] == (unsigned long) x_type_desktop) {
                    buf[i] = (unsigned long) x_type_normal;
                    changed = true;
                }
            }
            if (changed) {
                shim_log("[shim] xlib win 0x%lx: DESKTOP -> NORMAL\n", (unsigned long) w);
                return real_xlib_ccp(display, w, property, type, format, mode, (unsigned char*) buf, nelements);
            }
        } else if (type == XA_ATOM && property == x_wm_state) {
            const auto* in = reinterpret_cast<const unsigned long*>(data);
            bool has_below = false;
            for (int i = 0; i < nelements; i++) has_below |= (in[i] == (unsigned long) x_state_below);
            if (!has_below) {
                unsigned long buf[33];
                for (int i = 0; i < nelements; i++) buf[i] = in[i];
                buf[nelements++] = (unsigned long) x_state_below;
                shim_log("[shim] xlib win 0x%lx: appended BELOW\n", (unsigned long) w);
                return real_xlib_ccp(display, w, property, type, format, mode, (unsigned char*) buf, nelements);
            }
        }
    }
    return real_xlib_ccp(display, w, property, type, format, mode, data, nelements);
}
