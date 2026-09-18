// Probe for the peony-alpha-shim hook tests. Launched with LD_PRELOAD set
// by the test runner; exercises one shim hook per mode and prints a
// machine-readable verdict line.
//
//   shim_probe ctor     <marker.png>   constructor hook: the wallpaper
//                                      pixmap must come out transparent
//   shim_probe xcbtype  <marker.png>   xcb_change_property hook: a
//                                      _NET_WM_WINDOW_TYPE=DESKTOP write
//                                      must land on the server as NORMAL
#include <QGuiApplication>
#include <QImage>
#include <QPixmap>
#include <xcb/xcb.h>
#include <cstring>
#include <cstdio>

static int runCtorHook (const QString& marker) {
    QPixmap pixmap (marker);
    const QImage image = pixmap.toImage ();
    const bool transparent = !image.isNull () && image.pixelColor (0, 0).alpha () == 0;
    std::printf ("RESULT transparent=%d\n", transparent ? 1 : 0);
    return transparent ? 0 : 2;
}

static int runXcbTypeHook () {
    xcb_connection_t* connection = xcb_connect (nullptr, nullptr);
    if (xcb_connection_has_error (connection)) {
        std::printf ("RESULT skipped=no-display\n");
        return 0;
    }
    xcb_screen_t* screen = xcb_setup_roots_iterator (xcb_get_setup (connection)).data;
    xcb_window_t window = xcb_generate_id (connection);
    xcb_create_window (connection, XCB_COPY_FROM_PARENT, window, screen->root, 0, 0, 100, 100, 0,
                       XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual, 0, nullptr);

    auto intern = [&] (const char* name) {
        xcb_intern_atom_cookie_t ck = xcb_intern_atom (connection, 0, strlen (name), name);
        xcb_intern_atom_reply_t* r = xcb_intern_atom_reply (connection, ck, nullptr);
        xcb_atom_t atom = r ? r->atom : 0;
        free (r);
        return atom;
    };
    const xcb_atom_t typeAtom = intern ("_NET_WM_WINDOW_TYPE");
    const xcb_atom_t desktopAtom = intern ("_NET_WM_WINDOW_TYPE_DESKTOP");
    const xcb_atom_t normalAtom = intern ("_NET_WM_WINDOW_TYPE_NORMAL");

    // the shim must rewrite this DESKTOP write into NORMAL pre-upload
    xcb_change_property (connection, XCB_PROP_MODE_REPLACE, window, typeAtom, XCB_ATOM_ATOM, 32, 1, &desktopAtom);
    xcb_flush (connection);

    xcb_get_property_cookie_t ck = xcb_get_property (connection, 0, window, typeAtom, XCB_ATOM_ATOM, 0, 8);
    xcb_get_property_reply_t* r = xcb_get_property_reply (connection, ck, nullptr);
    const char* verdict = "unknown";
    if (r) {
        const xcb_atom_t stored = *static_cast<const xcb_atom_t*> (xcb_get_property_value (r));
        verdict = (stored == normalAtom) ? "NORMAL" : (stored == desktopAtom ? "DESKTOP" : "other");
        free (r);
    }
    std::printf ("RESULT type-after=%s\n", verdict);
    return strcmp (verdict, "NORMAL") == 0 ? 0 : 2;
}

int main (int argc, char** argv) {
    QGuiApplication app (argc, argv);
    const QStringList args = QCoreApplication::arguments ();
    const QString mode = args.value (1);
    const QString marker = args.value (2);

    if (mode == "ctor")
        return runCtorHook (marker);
    if (mode == "xcbtype")
        return runXcbTypeHook ();
    std::printf ("RESULT unknown-mode\n");
    return 2;
}
