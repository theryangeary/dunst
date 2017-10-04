/* copyright 2013 Sascha Kruse and contributors (see LICENSE for licensing information) */
#include "x.h"

#include <X11/X.h>
#include <X11/XKBlib.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <assert.h>
#include <cairo.h>
#include <cairo-xlib.h>
#include <gdk/gdk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib-object.h>
#include <locale.h>
#include <math.h>
#include <pango/pangocairo.h>
#include <pango/pango-attributes.h>
#include <pango/pango-font.h>
#include <pango/pango-layout.h>
#include <pango/pango-types.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "src/draw.h"
#include "src/dunst.h"
#include "src/markup.h"
#include "src/notification.h"
#include "src/settings.h"
#include "src/utils.h"

#include "screen.h"

#define WIDTH 400
#define HEIGHT 400

xctx_t xctx;
bool dunst_grab_errored = false;

/* FIXME refactor setup teardown handlers into one setup and one teardown */
static void x_shortcut_setup_error_handler(void);
static int x_shortcut_tear_down_error_handler(void);
static void setopacity(Window win, unsigned long opacity);
static void x_handle_click(XEvent ev);
static void x_win_setup(void);

cairo_surface_t* x_cairo_create_surface()
{
        return cairo_xlib_surface_create(xctx.dpy,
                        xctx.win, DefaultVisual(xctx.dpy, 0), WIDTH, HEIGHT);

}

void x_win_move(int width, int height)
{

        int x, y;
        screen_info *scr = get_active_screen();
        /* calculate window position */
        if (draw_get_geometry()->x < 0) {
                x = (scr->dim.x + (scr->dim.w - width)) + draw_get_geometry()->x;
        } else {
                x = scr->dim.x + draw_get_geometry()->x;
        }

        if (draw_get_geometry()->y < 0) {
                y = scr->dim.y + (scr->dim.h + draw_get_geometry()->y) - height;
        } else {
                y = scr->dim.y + draw_get_geometry()->y;
        }

        /* move and resize */
        if (x != xctx.window_dim.x || y != xctx.window_dim.y) {
                XMoveWindow(xctx.dpy, xctx.win, x, y);
        }
        if (width != xctx.window_dim.w || height != xctx.window_dim.h) {
                XResizeWindow(xctx.dpy, xctx.win, width, height);
        }


        xctx.window_dim.x = x;
        xctx.window_dim.y = y;
        xctx.window_dim.h = height;
        xctx.window_dim.w = width;
}

static void setopacity(Window win, unsigned long opacity)
{
        Atom _NET_WM_WINDOW_OPACITY =
            XInternAtom(xctx.dpy, "_NET_WM_WINDOW_OPACITY", false);
        XChangeProperty(xctx.dpy, win, _NET_WM_WINDOW_OPACITY, XA_CARDINAL, 32,
                        PropModeReplace, (unsigned char *)&opacity, 1L);
}

/*
 * Returns the modifier which is NumLock.
 */
static KeySym x_numlock_mod()
{
        static KeyCode nl = 0;
        KeySym sym = 0;
        XModifierKeymap *map = XGetModifierMapping(xctx.dpy);

        if (!nl)
                nl = XKeysymToKeycode(xctx.dpy, XStringToKeysym("Num_Lock"));

        for (int mod = 0; mod < 8; mod++) {
                for (int j = 0; j < map->max_keypermod; j++) {
                        if (map->modifiermap[mod*map->max_keypermod+j] == nl) {
                                /* In theory, one could use `1 << mod`, but this
                                 * could count as 'using implementation details',
                                 * so use this large switch. */
                                switch (mod) {
                                        case ShiftMapIndex:
                                                sym = ShiftMask;
                                                goto end;
                                        case LockMapIndex:
                                                sym = LockMask;
                                                goto end;
                                        case ControlMapIndex:
                                                sym = ControlMask;
                                                goto end;
                                        case Mod1MapIndex:
                                                sym = Mod1Mask;
                                                goto end;
                                        case Mod2MapIndex:
                                                sym = Mod2Mask;
                                                goto end;
                                        case Mod3MapIndex:
                                                sym = Mod3Mask;
                                                goto end;
                                        case Mod4MapIndex:
                                                sym = Mod4Mask;
                                                goto end;
                                        case Mod5MapIndex:
                                                sym = Mod5Mask;
                                                goto end;
                                }
                        }
                }
        }

end:
        XFreeModifiermap(map);
        return sym;
}

/*
 * Helper function to use glib's mainloop mechanic
 * with Xlib
 */
gboolean x_mainloop_fd_prepare(GSource *source, gint *timeout)
{
        if (timeout)
                *timeout = -1;
        else
                g_print("BUG: x_mainloop_fd_prepare: timeout == NULL\n");
        return false;
}

/*
 * Helper function to use glib's mainloop mechanic
 * with Xlib
 */
gboolean x_mainloop_fd_check(GSource *source)
{
        return XPending(xctx.dpy) > 0;
}

/*
 * Main Dispatcher for XEvents
 */
gboolean x_mainloop_fd_dispatch(GSource *source, GSourceFunc callback,
                                gpointer user_data)
{
        XEvent ev;
        unsigned int state;
        while (XPending(xctx.dpy) > 0) {
                XNextEvent(xctx.dpy, &ev);
                switch (ev.type) {
                case Expose:
                        if (ev.xexpose.count == 0 && xctx.visible) {
                                draw();
                        }
                        break;
                case SelectionNotify:
                        if (ev.xselection.property == xctx.utf8)
                                break;
                case ButtonRelease:
                        if (ev.xbutton.window == xctx.win) {
                                x_handle_click(ev);
                        }
                        break;
                case KeyPress:
                        state = ev.xkey.state;
                        /* NumLock is also encoded in the state. Remove it. */
                        state &= ~x_numlock_mod();
                        if (settings.close_ks.str
                            && XLookupKeysym(&ev.xkey,
                                             0) == settings.close_ks.sym
                            && settings.close_ks.mask == state) {
                                if (displayed) {
                                        notification *n = g_queue_peek_head(displayed);
                                        if (n)
                                                notification_close(n, 2);
                                }
                        }
                        if (settings.history_ks.str
                            && XLookupKeysym(&ev.xkey,
                                             0) == settings.history_ks.sym
                            && settings.history_ks.mask == state) {
                                history_pop();
                        }
                        if (settings.close_all_ks.str
                            && XLookupKeysym(&ev.xkey,
                                             0) == settings.close_all_ks.sym
                            && settings.close_all_ks.mask == state) {
                                move_all_to_history();
                        }
                        if (settings.context_ks.str
                            && XLookupKeysym(&ev.xkey,
                                             0) == settings.context_ks.sym
                            && settings.context_ks.mask == state) {
                                context_menu();
                        }
                        break;
                case FocusIn:
                case FocusOut:
                case PropertyNotify:
                        wake_up();
                        break;
                default:
                        screen_check_event(ev);
                        break;
                }
        }
        return true;
}

/*
 * Check whether the user is currently idle.
 */
bool x_is_idle(void)
{
        XScreenSaverQueryInfo(xctx.dpy, DefaultRootWindow(xctx.dpy),
                              xctx.screensaver_info);
        if (settings.idle_threshold == 0) {
                return false;
        }
        return xctx.screensaver_info->idle / 1000 > settings.idle_threshold;
}

/* TODO move to x_mainloop_* */
/*
 * Handle incoming mouse click events
 */
static void x_handle_click(XEvent ev)
{
        if (ev.xbutton.button == Button3) {
                move_all_to_history();

                return;
        }

        if (ev.xbutton.button == Button1 || ev.xbutton.button == Button2) {
                int y = settings.separator_height;
                notification *n = NULL;
                int first = true;
                for (GList *iter = g_queue_peek_head_link(displayed); iter;
                     iter = iter->next) {
                        n = iter->data;
                        if (ev.xbutton.y > y && ev.xbutton.y < y + n->displayed_height)
                                break;

                        y += n->displayed_height + settings.separator_height;
                        if (first)
                                y += settings.frame_width;
                }

                if (n) {
                        if (ev.xbutton.button == Button1)
                                notification_close(n, 2);
                        else
                                notification_do_action(n);
                }
        }
}

void x_free(void)
{
        if (xctx.dpy)
                XCloseDisplay(xctx.dpy);
}

void x_parse_geometry(struct geometry *geom_ret)
{
        int mask = XParseGeometry(settings.geom,
                       &(geom_ret->x),
                       &(geom_ret->y),
                       (unsigned int *) &(geom_ret->width),
                       (unsigned int *) &(geom_ret->height)
                       );

        geom_ret->dynamic_width = (mask & WidthValue && geom_ret->width == 0);

        if (settings.geom[0] == '-') {
                geom_ret->negative_width = true;
                settings.geom++;
        } else {
                geom_ret->negative_width = false;
        }
}

/*
 * Setup X11 stuff
 */
void x_setup(void)
{

        /* initialize xctx.dc, font, keyboard, colors */
        if (!setlocale(LC_CTYPE, "") || !XSupportsLocale())
                fputs("no locale support\n", stderr);
        if (!(xctx.dpy = XOpenDisplay(NULL))) {
                die("cannot open display\n", EXIT_FAILURE);
        }

        x_shortcut_init(&settings.close_ks);
        x_shortcut_init(&settings.close_all_ks);
        x_shortcut_init(&settings.history_ks);
        x_shortcut_init(&settings.context_ks);

        x_shortcut_grab(&settings.close_ks);
        x_shortcut_ungrab(&settings.close_ks);
        x_shortcut_grab(&settings.close_all_ks);
        x_shortcut_ungrab(&settings.close_all_ks);
        x_shortcut_grab(&settings.history_ks);
        x_shortcut_ungrab(&settings.history_ks);
        x_shortcut_grab(&settings.context_ks);
        x_shortcut_ungrab(&settings.context_ks);

        xctx.screensaver_info = XScreenSaverAllocInfo();

        init_screens();
        x_win_setup();
        x_shortcut_grab(&settings.history_ks);

}

static void x_set_wm(Window win)
{

        Atom data[2];

        /* set window title */
        char *title = settings.title != NULL ? settings.title : "Dunst";
        Atom _net_wm_title =
                XInternAtom(xctx.dpy, "_NET_WM_NAME", false);

        XStoreName(xctx.dpy, win, title);
        XChangeProperty(xctx.dpy, win, _net_wm_title,
                XInternAtom(xctx.dpy, "UTF8_STRING", false), 8,
                PropModeReplace, (unsigned char *) title, strlen(title));

        /* set window class */
        char *class = settings.class != NULL ? settings.class : "Dunst";
        XClassHint classhint = { class, "Dunst" };

        XSetClassHint(xctx.dpy, win, &classhint);

        /* set window type */
        Atom net_wm_window_type =
                XInternAtom(xctx.dpy, "_NET_WM_WINDOW_TYPE", false);

        data[0] = XInternAtom(xctx.dpy, "_NET_WM_WINDOW_TYPE_NOTIFICATION", false);
        data[1] = XInternAtom(xctx.dpy, "_NET_WM_WINDOW_TYPE_UTILITY", false);

        XChangeProperty(xctx.dpy, win, net_wm_window_type, XA_ATOM, 32,
                PropModeReplace, (unsigned char *) data, 2L);

        /* set state above */
        Atom net_wm_state =
                XInternAtom(xctx.dpy, "_NET_WM_STATE", false);

        data[0] = XInternAtom(xctx.dpy, "_NET_WM_STATE_ABOVE", false);

        XChangeProperty(xctx.dpy, win, net_wm_state, XA_ATOM, 32,
                PropModeReplace, (unsigned char *) data, 1L);
}

/*
 * Setup the window
 */
static void x_win_setup(void)
{

        Window root;
        XSetWindowAttributes wa;

        xctx.window_dim.x = 0;
        xctx.window_dim.y = 0;
        xctx.window_dim.w = 0;
        xctx.window_dim.h = 0;

        root = RootWindow(xctx.dpy, DefaultScreen(xctx.dpy));
        xctx.utf8 = XInternAtom(xctx.dpy, "UTF8_STRING", false);

        wa.override_redirect = true;
        wa.background_pixmap = ParentRelative;
        wa.event_mask =
            ExposureMask | KeyPressMask | VisibilityChangeMask |
            ButtonReleaseMask | FocusChangeMask| StructureNotifyMask;

        screen_info *scr = get_active_screen();
        xctx.win =
            XCreateWindow(xctx.dpy, root, scr->dim.x, scr->dim.y, scr->dim.w,
                          1, 0, DefaultDepth(xctx.dpy,
                                                       DefaultScreen(xctx.dpy)),
                          CopyFromParent, DefaultVisual(xctx.dpy,
                                                        DefaultScreen(xctx.dpy)),
                          CWOverrideRedirect | CWBackPixmap | CWEventMask, &wa);

        x_set_wm(xctx.win);
        settings.transparency =
            settings.transparency > 100 ? 100 : settings.transparency;
        setopacity(xctx.win,
                   (unsigned long)((100 - settings.transparency) *
                                   (0xffffffff / 100)));

        if (settings.f_mode != FOLLOW_NONE) {
                long root_event_mask = FocusChangeMask | PropertyChangeMask;
                XSelectInput(xctx.dpy, root, root_event_mask);
        }
}

/*
 * Show the window and grab shortcuts.
 */
void x_win_show(void)
{
        /* window is already mapped or there's nothing to show */
        if (xctx.visible || g_queue_is_empty(displayed)) {
                return;
        }

        x_shortcut_grab(&settings.close_ks);
        x_shortcut_grab(&settings.close_all_ks);
        x_shortcut_grab(&settings.context_ks);

        x_shortcut_setup_error_handler();
        XGrabButton(xctx.dpy, AnyButton, AnyModifier, xctx.win, false,
                    BUTTONMASK, GrabModeAsync, GrabModeSync, None, None);
        if (x_shortcut_tear_down_error_handler()) {
                fprintf(stderr, "Unable to grab mouse button(s)\n");
        }

        XMapRaised(xctx.dpy, xctx.win);
        xctx.visible = true;
}

/*
 * Hide the window and ungrab unused keyboard_shortcuts
 */
void x_win_hide()
{
        x_shortcut_ungrab(&settings.close_ks);
        x_shortcut_ungrab(&settings.close_all_ks);
        x_shortcut_ungrab(&settings.context_ks);

        XUngrabButton(xctx.dpy, AnyButton, AnyModifier, xctx.win);
        XUnmapWindow(xctx.dpy, xctx.win);
        XFlush(xctx.dpy);
        xctx.visible = false;
}

/*
 * Parse a string into a modifier mask.
 */
KeySym x_shortcut_string_to_mask(const char *str)
{
        if (!strcmp(str, "ctrl")) {
                return ControlMask;
        } else if (!strcmp(str, "mod4")) {
                return Mod4Mask;
        } else if (!strcmp(str, "mod3")) {
                return Mod3Mask;
        } else if (!strcmp(str, "mod2")) {
                return Mod2Mask;
        } else if (!strcmp(str, "mod1")) {
                return Mod1Mask;
        } else if (!strcmp(str, "shift")) {
                return ShiftMask;
        } else {
                fprintf(stderr, "Warning: Unknown Modifier: %s\n", str);
                return 0;
        }

}

/*
 * Error handler for grabbing mouse and keyboard errors.
 */
static int GrabXErrorHandler(Display *display, XErrorEvent *e)
{
        dunst_grab_errored = true;
        char err_buf[BUFSIZ];
        XGetErrorText(display, e->error_code, err_buf, BUFSIZ);
        fputs(err_buf, stderr);
        fputs("\n", stderr);

        if (e->error_code != BadAccess) {
                exit(EXIT_FAILURE);
        }

        return 0;
}

/*
 * Setup the Error handler.
 */
static void x_shortcut_setup_error_handler(void)
{
        dunst_grab_errored = false;

        XFlush(xctx.dpy);
        XSetErrorHandler(GrabXErrorHandler);
}

/*
 * Tear down the Error handler.
 */
static int x_shortcut_tear_down_error_handler(void)
{
        XFlush(xctx.dpy);
        XSync(xctx.dpy, false);
        XSetErrorHandler(NULL);
        return dunst_grab_errored;
}

/*
 * Grab the given keyboard shortcut.
 */
int x_shortcut_grab(keyboard_shortcut *ks)
{
        if (!ks->is_valid)
                return 1;
        Window root;
        root = RootWindow(xctx.dpy, DefaultScreen(xctx.dpy));

        x_shortcut_setup_error_handler();

        if (ks->is_valid) {
                XGrabKey(xctx.dpy, ks->code, ks->mask, root,
                         true, GrabModeAsync, GrabModeAsync);
                XGrabKey(xctx.dpy, ks->code, ks->mask | x_numlock_mod() , root,
                         true, GrabModeAsync, GrabModeAsync);
        }

        if (x_shortcut_tear_down_error_handler()) {
                fprintf(stderr, "Unable to grab key \"%s\"\n", ks->str);
                ks->is_valid = false;
                return 1;
        }
        return 0;
}

/*
 * Ungrab the given keyboard shortcut.
 */
void x_shortcut_ungrab(keyboard_shortcut *ks)
{
        Window root;
        root = RootWindow(xctx.dpy, DefaultScreen(xctx.dpy));
        if (ks->is_valid) {
                XUngrabKey(xctx.dpy, ks->code, ks->mask, root);
                XUngrabKey(xctx.dpy, ks->code, ks->mask | x_numlock_mod(), root);
        }
}

/*
 * Initialize the keyboard shortcut.
 */
void x_shortcut_init(keyboard_shortcut *ks)
{
        if (ks == NULL || ks->str == NULL)
                return;

        if (!strcmp(ks->str, "none") || (!strcmp(ks->str, ""))) {
                ks->is_valid = false;
                return;
        }

        char *str = g_strdup(ks->str);
        char *str_begin = str;

        while (strchr(str, '+')) {
                char *mod = str;
                while (*str != '+')
                        str++;
                *str = '\0';
                str++;
                g_strchomp(mod);
                ks->mask = ks->mask | x_shortcut_string_to_mask(mod);
        }
        g_strstrip(str);

        ks->sym = XStringToKeysym(str);
        /* find matching keycode for ks->sym */
        int min_keysym, max_keysym;
        XDisplayKeycodes(xctx.dpy, &min_keysym, &max_keysym);

        ks->code = NoSymbol;

        for (int i = min_keysym; i <= max_keysym; i++) {
                if (XkbKeycodeToKeysym(xctx.dpy, i, 0, 0) == ks->sym
                    || XkbKeycodeToKeysym(xctx.dpy, i, 0, 1) == ks->sym) {
                        ks->code = i;
                        break;
                }
        }

        if (ks->sym == NoSymbol || ks->code == NoSymbol) {
                fprintf(stderr, "Warning: Unknown keyboard shortcut: %s\n",
                        ks->str);
                ks->is_valid = false;
        } else {
                ks->is_valid = true;
        }

        g_free(str_begin);
}

/* vim: set tabstop=8 shiftwidth=8 expandtab textwidth=0: */
