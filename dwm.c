/* See LICENSE file for copyright and license details.
 *
 * dynamic window manager is designed like any other X client as well. It is
 * driven through handling X events. In contrast to other X clients, a window
 * manager selects for SubstructureRedirectMask on the root window, to receive
 * events about window (dis-)appearance. Only one X connection at a time is
 * allowed to select for this event mask.
 *
 * The event handlers of dwm are organized in an array which is accessed
 * whenever a new event has been fetched. This allows event dispatching
 * in O(1) time.
 *
 * Each child of the root window is called a client, except windows which have
 * set the override_redirect flag. Clients are organized in a linked client
 * list on each monitor, the focus history is remembered through a stack list
 * on each monitor. Each client contains a bit array to indicate the tags of a
 * client.
 *
 * Keys and tagging rules are organized as arrays and defined in config.h.
 *
 * To understand everything else, start reading main().
 */

/* TODO:
 *  - Change monitor and client list from linked-list to array
 *    - Instead of holding raw pointers to monitors and clients, use indices into the array (selected_monitor and variables like it become integers)
 *  - Find out how to reliably change monitor tagset to make each monitor have its own tags
 *  - Create a secondary process that loads a dynamic library and runs it
 *    - In the dynamic library, set the value of the status bar and handle keyboard shortcuts to launch apps
 *    - If the process dies (for whatever reason), dwm can report it and revive it when a keyboard shortcut is pressed or when the library is replaced with a new version
 *    - Some shortcuts should be kept in dwm in case the process crashes. That way the user can continue to fix the issue. (probably just vim and dmenu are needed
 *    - If the library is recompiled and placed into a known location then it should be reloaded
 */

#include <errno.h>
#include <locale.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>
#include <X11/XF86keysym.h>

#ifdef XINERAMA
#include <X11/extensions/Xinerama.h>
#endif /* XINERAMA */
#include <X11/Xft/Xft.h>

#include "drw.h"
#include "util.h"

/* macros */
#define ButtonMask              (ButtonPressMask|ButtonReleaseMask)
#define CleanMask(mask)         (mask & ~(numlockmask|LockMask) & (ShiftMask|ControlMask|Mod1Mask|Mod2Mask|Mod3Mask|Mod4Mask|Mod5Mask))
#define Intersect(x, y, width, height, monitor) (Maximum(0, Minimum((x)+(width),(monitor)->window_x+(monitor)->window_width) - Maximum((x),(monitor)->window_x)) \
                                                 * Maximum(0, Minimum((y)+(height),(monitor)->window_y+(monitor)->window_height) - Maximum((y),(monitor)->window_y)))
#define IsVisible(Client)            ((Client->tags & Client->monitor->selected_tags))
#define ArrayLength(X)          (sizeof(X) / sizeof((X)[0]))
#define MouseMask               (ButtonMask|PointerMotionMask)
#define ClientWidth(Client)     ((Client)->width + 2 * (Client)->border_width + gap_size)
#define ClientHeight(Client)    ((Client)->height + 2 * (Client)->border_width + gap_size)
#define TagMask                 ((1 << ArrayLength(tags)) - 1)
#define TEXTW(X)                (drw_fontset_getwidth(drw, (X)) + lrpad)

/* enums */
enum { CurNormal, CurResize, CurMove, CurLast }; /* cursor */
// TODO: Add color for multi-button shortcuts
enum { SchemeNorm, SchemeSel }; /* color schemes */
enum { NetSupported, NetWMName, NetWMState, NetWMCheck,
       NetWMFullscreen, NetActiveWindow, NetWMWindowType,
       NetWMWindowTypeDialog, NetClientList, NetLast }; /* EWMH atoms */
enum { WMProtocols, WMDelete, WMState, WMTakeFocus, WMLast }; /* default atoms */
enum { ClkTagBar, ClkLtSymbol, ClkStatusText, ClkWinTitle,
       ClkClientWin, ClkRootWin, ClkLast }; /* clicks */

typedef union Arg Arg;
union Arg {
    int i;
    unsigned int ui;
    float f;
    const void *v;
};

typedef struct Button Button;
struct Button {
    unsigned int click;
    unsigned int mask;
    unsigned int button;
    void (*func)(const Arg *arg);
    const Arg arg;
};

typedef struct Monitor Monitor;
typedef struct Client Client;
struct Client {
    char name[256];
    float min_aspect, max_aspect;
    int x, y, width, height;
    int oldx, oldy, old_width, old_height;

    int base_width, base_height;
    int inc_width, inc_height;
    int max_width, max_height;
    int min_width, min_height;

    int border_width, old_border_width;
    unsigned int tags;
    // struct {
        // flags
        signed char isfixed: 1;
        signed char isfloating: 1;
        signed char isurgent: 1;
        signed char isfullscreen: 1;
        signed char neverfocus: 1;
    // };
    int oldstate;
    Client *next;
    Client *snext; // next in stack
    Monitor *monitor;
    Window window;
};


typedef struct Key Key;
struct Key {
    unsigned int mod;
    KeySym keysym;
    void (*func)(const Arg *);
    const Arg arg;
};


typedef struct Layout Layout;
struct Layout {
    const char *symbol;
    void (*arrange)(Monitor *);
};

struct Monitor {
    char ltsymbol[16];
    float mfact;
    int nmaster;
    int num;
    int bar_height;               /* bar geometry */
    int screen_x, screen_y, screen_width, screen_height;   /* screen size */
    int window_x, window_y, window_width, window_height;   /* window area  */
    unsigned int selected_tags;
    unsigned int selected_layout;
    // unsigned int tagset[2];
    int showbar;
    int topbar;
    Client *clients;
    Client *selected_client;
    Client *stack;
    Monitor *next;
    Window barwin;
    // const Layout *layouts[2];
};

typedef struct Rule Rule;
struct Rule {
    const char *class;
    const char *instance;
    const char *title;
    unsigned int tags;
    int isfloating;
    int monitor_number;
};

/* function declarations */
static void applyrules(Client *client);
static int applysizehints(Client *client, int *x, int *y, int *width, int *height, int interact);
static void arrange(Monitor *monitor);
static void arrangemon(Monitor *monitor);
static void attach(Client *client);
static void attachstack(Client *client);
static void buttonpress(XEvent *event);
static void checkotherwm(void);
static void cleanup(void);
static void cleanup_monitors(Monitor *monitor);
static void clientmessage(XEvent *event);
static void configure(Client *client);
static void configurenotify(XEvent *event);
static void configurerequest(XEvent *event);
static Monitor *createmon(void);
static void destroynotify(XEvent *event);
static void detach(Client *client);
static void detachstack(Client *client);
static Monitor *dirtomon(int dir);
static void drawbar(Monitor *monitor);
static void drawbars(void);
static void enternotify(XEvent *event);
static void expose(XEvent *event);
static void focus(Client *client);
static void focusin(XEvent *event);
static void focusmon(const Arg *arg);
static void focusstack(const Arg *arg);
static Atom getatomprop(Client *client, Atom prop);
static int getrootptr(int *x, int *y);
static long getstate(Window window);
static pid_t getstatusbarpid();
static int gettextprop(Window window, Atom atom, char *text, unsigned int size);
static void grabbuttons(Client *client, int focused);
static void grabkeys(void);
static void incnmaster(const Arg *arg);
static void keypress(XEvent *event);
static void killclient(const Arg *arg);
static void manage(Window window, XWindowAttributes *wa);
static void mappingnotify(XEvent *event);
static void maprequest(XEvent *event);
static void monocle(Monitor *monitor);
static void motionnotify(XEvent *event);
static void movemouse(const Arg *arg);
static Client *nexttiled(Client *client);
static void pop(Client *);
static void propertynotify(XEvent *event);
static void quit(const Arg *arg);
static Monitor *recttomon(int x, int y, int width, int height);
static void resize(Client *client, int x, int y, int width, int height, int interact);
static void resizeclient(Client *client, int x, int y, int width, int height);
static void resizemouse(const Arg *arg);
static void restack(Monitor *monitor);
static void run(void);
static void scan(void);
static int sendevent(Client *client, Atom proto);
static void sendmon(Client *client, Monitor *monitor);
static void setclientstate(Client *client, long state);
static void setfocus(Client *client);
static void setfullscreen(Client *client, int fullscreen);
static void setlayout(const Arg *arg);
static void toggle_layout(const Arg *arg);
static void setmfact(const Arg *arg);
static void setup(void);
static void seturgent(Client *client, int urg);
static void showhide(Client *client);
static void sigchld(int unused);
static void sigstatusbar(const Arg *arg);
static void spawn(const Arg *arg);
static void tag(const Arg *arg);
static void tagmon(const Arg *arg);
static void tile(Monitor *);
static void togglebar(const Arg *arg);
static void togglefloating(const Arg *arg);
static void toggletag(const Arg *arg);
static void toggleview(const Arg *arg);
static void change_gap(const Arg *arg);
static void unfocus(Client *client, int setfocus);
static void unmanage(Client *client, int destroyed);
static void unmapnotify(XEvent *event);
static void updatebarpos(Monitor *monitor);
static void updatebars(void);
static void updateclientlist(void);
static int updategeom(void);
static void updatenumlockmask(void);
static void updatesizehints(Client *client);
static void updatestatus(void);
static void updatetitle(Client *client);
static void updatewindowtype(Client *client);
static void updatewmhints(Client *client);
static void view(const Arg *arg);
static Client *wintoclient(Window window);
static Monitor *wintomon(Window window);
static int xerror(Display *display, XErrorEvent *ee);
static int xerrordummy(Display *display, XErrorEvent *ee);
static int xerrorstart(Display *display, XErrorEvent *ee);
static void zoom(const Arg *arg);
/* variables */
static const char broken[] = "broken";
static char stext[256];
static int statusw;
static int statussig;
static pid_t statuspid = -1;
static int screen;
static int sw, sh;           /* X global_display screen geometry width, height */
static int bh, blw = 0;      /* bar geometry */
static int lrpad;            /* sum of left and right padding for text */
static int (*xerrorxlib)(Display *, XErrorEvent *);
static unsigned int numlockmask = 0;

typedef void Handler(XEvent*);
static Handler* handler[LASTEvent] = {
    [ButtonPress] = buttonpress,
    [ClientMessage] = clientmessage,
    [ConfigureRequest] = configurerequest,
    [ConfigureNotify] = configurenotify,
    [DestroyNotify] = destroynotify,
    [EnterNotify] = enternotify,
    [Expose] = expose,
    [FocusIn] = focusin,
    [KeyPress] = keypress,
    [MappingNotify] = mappingnotify,
    [MapRequest] = maprequest,
    [MotionNotify] = motionnotify,
    [PropertyNotify] = propertynotify,
    [UnmapNotify] = unmapnotify
};

static Atom wmatom[WMLast], netatom[NetLast];
static int global_running = 1;
static Cur *cursor[CurLast];
static Clr **scheme;
static Display *global_display;
static Drw *drw;
static Monitor *all_monitors, *selected_monitor;
static Window root, wmcheckwin;

inline void _unused(void* x, ...) {}
#define unused(...) _unused((void*)__VA_ARGS__)

/* configuration, allows nested code to access above variables */
#include "config.h"

/* compile-time check if all tags fit into an unsigned int bit array. */
struct NumTags { char limitexceeded[ArrayLength(tags) > 31 ? -1 : 1]; };

/* function implementations */
void applyrules(Client *client) {
    const char *class, *instance;
    unsigned int i;
    const Rule *rule;
    Monitor *monitor;
    XClassHint ch = { NULL, NULL };

    /* rule matching */
    client->isfloating = 0;
    client->tags = 0;
    XGetClassHint(global_display, client->window, &ch);
    class    = ch.res_class ? ch.res_class : broken;
    instance = ch.res_name  ? ch.res_name  : broken;

    for (i = 0; i < ArrayLength(rules); i++) {
        rule = &rules[i];
        if ((!rule->title || strstr(client->name, rule->title))
        && (!rule->class || strstr(class, rule->class))
        && (!rule->instance || strstr(instance, rule->instance)))
        {
            client->isfloating = rule->isfloating;
            client->tags |= rule->tags;
            for (monitor = all_monitors; monitor && monitor->num != rule->monitor_number; monitor = monitor->next){
                // do nothing, find monitor
            }
            if (monitor)
                client->monitor = monitor;
        }
    }
    if (ch.res_class)
        XFree(ch.res_class);
    if (ch.res_name)
        XFree(ch.res_name);
    client->tags = client->tags & TagMask ? client->tags & TagMask : client->monitor->selected_tags;
}

int applysizehints(Client *client, int *x, int *y, int *width, int *height, int interact) {
    int baseismin;
    Monitor *monitor = client->monitor;

    /* set minimum possible */
    *width = Maximum(1, *width);
    *height = Maximum(1, *height);
    if (interact) {
        if (*x > sw)
            *x = sw - ClientWidth(client);
        if (*y > sh)
            *y = sh - ClientHeight(client);
        if (*x + *width + 2 * client->border_width < 0)
            *x = 0;
        if (*y + *height + 2 * client->border_width < 0)
            *y = 0;
    } else {
        if (*x >= monitor->window_x + monitor->window_width)
            *x = monitor->window_x + monitor->window_width - ClientWidth(client);
        if (*y >= monitor->window_y + monitor->window_height)
            *y = monitor->window_y + monitor->window_height - ClientHeight(client);
        if (*x + *width + 2 * client->border_width <= monitor->window_x)
            *x = monitor->window_x;
        if (*y + *height + 2 * client->border_width <= monitor->window_y)
            *y = monitor->window_y;
    }
    if (*height < bh)
        *height = bh;
    if (*width < bh)
        *width = bh;
    if (resizehints || client->isfloating || !layouts[client->monitor->selected_layout].arrange) {
        /* see last two sentences in ICCCM 4.1.2.3 */
        baseismin = client->base_width == client->min_width && client->base_height == client->min_height;
        if (!baseismin) { /* temporarily remove base dimensions */
            *width -= client->base_width;
            *height -= client->base_height;
        }
        /* adjust for aspect limits */
        if (client->min_aspect > 0 && client->max_aspect > 0) {
            if (client->max_aspect < (float)*width / *height)
                *width = *height * client->max_aspect + 0.5;
            else if (client->min_aspect < (float)*height / *width)
                *height = *width * client->min_aspect + 0.5;
        }
        if (baseismin) { /* increment calculation requires this */
            *width -= client->base_width;
            *height -= client->base_height;
        }
        /* adjust for increment value */
        if (client->inc_width)
            *width -= *width % client->inc_width;
        if (client->inc_height)
            *height -= *height % client->inc_height;
        /* restore base dimensions */
        *width = Maximum(*width + client->base_width, client->min_width);
        *height = Maximum(*height + client->base_height, client->min_height);
        if (client->max_width)
            *width = Minimum(*width, client->max_width);
        if (client->max_height)
            *height = Minimum(*height, client->max_height);
    }
    return *x != client->x || *y != client->y || *width != client->width || *height != client->height;
}

void arrange(Monitor *monitor) {
    if (monitor)
        showhide(monitor->stack);
    else for (monitor = all_monitors; monitor; monitor = monitor->next)
        showhide(monitor->stack);
    if (monitor) {
        arrangemon(monitor);
        restack(monitor);
    } else for (monitor = all_monitors; monitor; monitor = monitor->next)
        arrangemon(monitor);
}

void arrangemon(Monitor *monitor) {
    strncpy(monitor->ltsymbol, layouts[monitor->selected_layout].symbol, sizeof(monitor->ltsymbol));
    if (layouts[monitor->selected_layout].arrange)
        layouts[monitor->selected_layout].arrange(monitor);
}

void attach(Client *client) {
    client->next = client->monitor->clients;
    client->monitor->clients = client;
}

void attachstack(Client *client) {
    client->snext = client->monitor->stack;
    client->monitor->stack = client;
}

void buttonpress(XEvent *event) {
    unsigned int i, x;
    Arg arg = {0};
    Client *client;
    Monitor *monitor;
    XButtonPressedEvent *ev = &event->xbutton;
 	char *text, *s, ch;

    unsigned int click = ClkRootWin;
    /* focus monitor if necessary */
    if ((monitor = wintomon(ev->window)) && monitor != selected_monitor) {
        unfocus(selected_monitor->selected_client, 1);
        selected_monitor = monitor;
        focus(NULL);
    }

    if (ev->window == selected_monitor->barwin) {
        i = x = 0;

        int occupied = 0;
        for (client = monitor->clients; client; client = client->next) {
            occupied |= client->tags;
        }

        do {
            if (occupied & (1 << i)) { 
                x += TEXTW(tags[i]);
            }
        } while (ev->x >= x && ++i < ArrayLength(tags));
        if (i < ArrayLength(tags)) {
            click = ClkTagBar;
            arg.ui = 1 << i;
        } else if (ev->x < x + blw) {
            click = ClkLtSymbol;
        } else if (ev->x > selected_monitor->window_width - statusw) {
            x = selected_monitor->window_width - statusw;
            click = ClkStatusText;
            statussig = 0;
            for (text = s = stext; *s && x <= ev->x; s++) {
                if ((unsigned char)(*s) < ' ') {
                    ch = *s;
                    *s = '\0';
                    x += TEXTW(text) - lrpad;
                    *s = ch;
                    text = s + 1;
                    if (x >= ev->x)
                        break;
                    statussig = ch;
                }
            }
        } else {
            click = ClkWinTitle;
        }
    } else if ((client = wintoclient(ev->window))) {
        focus(client);
        restack(selected_monitor);
        XAllowEvents(global_display, ReplayPointer, CurrentTime);
        click = ClkClientWin;
    }

    for (i = 0; i < ArrayLength(buttons); i++)
        if (click == buttons[i].click && buttons[i].func && buttons[i].button == ev->button
        && CleanMask(buttons[i].mask) == CleanMask(ev->state))
            buttons[i].func(click == ClkTagBar && buttons[i].arg.i == 0 ? &arg : &buttons[i].arg);
}

void checkotherwm(void) {
    xerrorxlib = XSetErrorHandler(xerrorstart);
    /* this causes an error if some other window manager is running */
    XSelectInput(global_display, DefaultRootWindow(global_display), SubstructureRedirectMask);
    XSync(global_display, False);
    XSetErrorHandler(xerror);
    XSync(global_display, False);
}

void cleanup(void) {
    Arg a = { .ui = ~0 };
    // Layout foo = { "", NULL };
    Monitor *monitor;
    size_t i;

    view(&a);
    // selected_monitor->layouts[selected_monitor->selected_layout] = &foo;
    for (monitor = all_monitors; monitor; monitor = monitor->next)
        while (monitor->stack)
            unmanage(monitor->stack, 0);
    XUngrabKey(global_display, AnyKey, AnyModifier, root);
    while (all_monitors)
        cleanup_monitors(all_monitors);
    for (i = 0; i < CurLast; i++)
        drw_cur_free(drw, cursor[i]);
    for (i = 0; i < ArrayLength(colors); i++)
        free(scheme[i]);
    XDestroyWindow(global_display, wmcheckwin);
    drw_free(drw);
    XSync(global_display, False);
    XSetInputFocus(global_display, PointerRoot, RevertToPointerRoot, CurrentTime);
    XDeleteProperty(global_display, root, netatom[NetActiveWindow]);
}

void cleanup_monitors(Monitor *mon) {
    Monitor *monitor;

    if (mon == all_monitors)
        all_monitors = all_monitors->next;
    else {
        for (monitor = all_monitors; monitor && monitor->next != mon; monitor = monitor->next);
        monitor->next = mon->next;
    }
    XUnmapWindow(global_display, mon->barwin);
    XDestroyWindow(global_display, mon->barwin);
    free(mon);
}

void clientmessage(XEvent *event) {
    XClientMessageEvent *cme = &event->xclient;
    Client *client = wintoclient(cme->window);

    if (!client)
        return;
    if (cme->message_type == netatom[NetWMState]) {
        if (cme->data.l[1] == netatom[NetWMFullscreen]
        || cme->data.l[2] == netatom[NetWMFullscreen])
            setfullscreen(client, (cme->data.l[0] == 1 /* _NET_WM_STATE_ADD    */
                || (cme->data.l[0] == 2 /* _NET_WM_STATE_TOGGLE */ && !client->isfullscreen)));
    } else if (cme->message_type == netatom[NetActiveWindow]) {
        if (client != selected_monitor->selected_client && !client->isurgent)
            seturgent(client, 1);
    }
}

void configure(Client *client) {
    XConfigureEvent configure_event;

    configure_event.type = ConfigureNotify;
    configure_event.display = global_display;
    configure_event.event = client->window;
    configure_event.window = client->window;
    configure_event.x = client->x;
    configure_event.y = client->y;
    configure_event.width = client->width;
    configure_event.height = client->height;
    configure_event.border_width = client->border_width;
    configure_event.above = None;
    configure_event.override_redirect = False;
    XSendEvent(global_display, client->window, False, StructureNotifyMask, (XEvent *)&configure_event);
}

void configurenotify(XEvent *event) {
    Monitor *monitor;
    Client *client;
    XConfigureEvent *ev = &event->xconfigure;
    int dirty;

    /* TODO: updategeom handling sucks, needs to be simplified */
    if (ev->window == root) {
        dirty = (sw != ev->width || sh != ev->height);
        sw = ev->width;
        sh = ev->height;
        if (updategeom() || dirty) {
            drw_resize(drw, sw, bh);
            updatebars();
            for (monitor = all_monitors; monitor; monitor = monitor->next) {
                for (client = monitor->clients; client; client = client->next)
                    if (client->isfullscreen)
                        resizeclient(client, monitor->screen_x, monitor->screen_y, monitor->screen_width, monitor->screen_height);
                XMoveResizeWindow(global_display, monitor->barwin, monitor->window_x, monitor->bar_height, monitor->window_width, bh);
            }
            focus(NULL);
            arrange(NULL);
        }
    }
}

void configurerequest(XEvent *event) {
    Client *client;
    Monitor *monitor;
    XConfigureRequestEvent *ev = &event->xconfigurerequest;
    XWindowChanges wc;

    if ((client = wintoclient(ev->window))) {
        if (ev->value_mask & CWBorderWidth)
            client->border_width = ev->border_width;
        else if (client->isfloating || !layouts[selected_monitor->selected_layout].arrange) {
            monitor = client->monitor;
            if (ev->value_mask & CWX) {
                client->oldx = client->x;
                client->x = monitor->screen_x + ev->x;
            }
            if (ev->value_mask & CWY) {
                client->oldy = client->y;
                client->y = monitor->screen_y + ev->y;
            }
            if (ev->value_mask & CWWidth) {
                client->old_width = client->width;
                client->width = ev->width;
            }
            if (ev->value_mask & CWHeight) {
                client->old_height = client->height;
                client->height = ev->height;
            }
            if ((client->x + client->width) > monitor->screen_x + monitor->screen_width && client->isfloating)
                client->x = monitor->screen_x + (monitor->screen_width / 2 - ClientWidth(client) / 2); /* center in x direction */
            if ((client->y + client->height) > monitor->screen_y + monitor->screen_height && client->isfloating)
                client->y = monitor->screen_y + (monitor->screen_height / 2 - ClientHeight(client) / 2); /* center in y direction */
            if ((ev->value_mask & (CWX|CWY)) && !(ev->value_mask & (CWWidth|CWHeight)))
                configure(client);
            if (IsVisible(client))
                XMoveResizeWindow(global_display, client->window, client->x, client->y, client->width, client->height);
        } else
            configure(client);
    } else {
        wc.x = ev->x;
        wc.y = ev->y;
        wc.width = ev->width;
        wc.height = ev->height;
        wc.border_width = ev->border_width;
        wc.sibling = ev->above;
        wc.stack_mode = ev->detail;
        XConfigureWindow(global_display, ev->window, ev->value_mask, &wc);
    }
    XSync(global_display, False);
}

Monitor *createmon(void) {
    Monitor *monitor;

    monitor = ecalloc(1, sizeof(Monitor));
    monitor->selected_tags = 1;
    // monitor->tagset[0] = monitor->tagset[1] = 1;
    monitor->mfact = mfact;
    monitor->nmaster = nmaster;
    monitor->showbar = showbar;
    monitor->topbar = topbar;
    // monitor->layouts[0] = &layouts[0];
    // monitor->layouts[1] = &layouts[1 % ArrayLength(layouts)];
    strncpy(monitor->ltsymbol, layouts[0].symbol, sizeof(monitor->ltsymbol));
    return monitor;
}

void destroynotify(XEvent *event) {
    Client *client;
    XDestroyWindowEvent *ev = &event->xdestroywindow;

    if ((client = wintoclient(ev->window)))
        unmanage(client, 1);
}

void detach(Client *client) {
    Client **tc;

    for (tc = &client->monitor->clients; *tc && *tc != client; tc = &(*tc)->next);
    *tc = client->next;
}

void detachstack(Client *client) {
    Client **tc, *t;

    for (tc = &client->monitor->stack; *tc && *tc != client; tc = &(*tc)->snext);
    *tc = client->snext;

    if (client == client->monitor->selected_client) {
        for (t = client->monitor->stack; t && !IsVisible(t); t = t->snext);
        client->monitor->selected_client = t;
    }
}

Monitor * dirtomon(int dir) {
    Monitor *monitor = NULL;

    if (dir > 0) {
        if (!(monitor = selected_monitor->next))
            monitor = all_monitors;
    } else if (selected_monitor == all_monitors)
        for (monitor = all_monitors; monitor->next; monitor = monitor->next);
    else
        for (monitor = all_monitors; monitor->next != selected_monitor; monitor = monitor->next);
    return monitor;
}

void drawbar(Monitor *monitor) {
    int x, width, tw = 0;
    int boxs = drw->fonts->height / 9;
    int boxw = drw->fonts->height / 6 + 2;
    unsigned int i, occupied = 0, urg = 0;
    Client *client;

    if (!monitor->showbar)
        return;

    /* draw status first so it can be overdrawn by tags later */
    if (monitor == selected_monitor) { /* status is only drawn on selected monitor */
        char *text, *s, ch;
        drw_setscheme(drw, scheme[SchemeNorm]);

        x = 0;
        for (text = s = stext; *s; s++) {
            if ((unsigned char)(*s) < ' ') {
                ch = *s;
                *s = '\0';
                tw = TEXTW(text) - lrpad;
                drw_text(drw, monitor->window_width - statusw + x, 0, tw, bh, 0, text, 0);
                x += tw;
                *s = ch;
                text = s + 1;
            }
        }
        tw = TEXTW(text) - lrpad + 2;
        drw_text(drw, monitor->window_width - statusw + x, 0, tw, bh, 0, text, 0);
        tw = statusw;
    }

    for (client = monitor->clients; client; client = client->next) {
        occupied |= client->tags;
        if (client->isurgent)
            urg |= client->tags;
    }

    x = 0;
    for (i = 0; i < ArrayLength(tags); i++) {
        int monitor_is_selected = monitor->selected_tags & (1 << i);
        if (occupied & (1 << i) || monitor_is_selected) {
            width = TEXTW(tags[i]);
            drw_setscheme(drw, scheme[monitor_is_selected ? SchemeSel : SchemeNorm]);
            drw_text(drw, x, 0, width, bh, lrpad / 2, tags[i], urg & 1 << i);

            // drw_rect(drw, x + boxs, boxs, boxw, boxw,
            //          monitor == selected_monitor && selected_monitor->selected_client && selected_monitor->selected_client->tags & 1 << i,
            //          urg & 1 << i);

            x += width;
        }
    }

    if(0) {
        width = blw = TEXTW(monitor->ltsymbol);
        drw_setscheme(drw, scheme[SchemeNorm]);
        x = drw_text(drw, x, 0, width, bh, lrpad / 2, monitor->ltsymbol, 0);
    }

    if ((width = monitor->window_width - tw - x) > bh) {
        if (monitor->selected_client) {
            // drw_setscheme(drw, scheme[monitor == selected_monitor ? SchemeSel : SchemeNorm]);
            drw_setscheme(drw, scheme[SchemeNorm]);
            drw_text(drw, x, 0, width, bh, lrpad / 2, monitor->selected_client->name, 0);
            if (monitor->selected_client->isfloating)
                drw_rect(drw, x + boxs, boxs, boxw, boxw, monitor->selected_client->isfixed, 0);
        } else {
            drw_setscheme(drw, scheme[SchemeNorm]);
            drw_rect(drw, x, 0, width, bh, 1, 1);
        }
    }
    drw_map(drw, monitor->barwin, 0, 0, monitor->window_width, bh);
}

void drawbars(void) {
    for (Monitor *monitor = all_monitors; monitor; monitor = monitor->next) {
        drawbar(monitor);
    }
}

void enternotify(XEvent *event) {
    Client *client;
    Monitor *monitor;
    XCrossingEvent *ev = &event->xcrossing;

    if ((ev->mode != NotifyNormal || ev->detail == NotifyInferior) && ev->window != root)
        return;
    client = wintoclient(ev->window);
    monitor = client ? client->monitor : wintomon(ev->window);
    if (monitor != selected_monitor) {
        unfocus(selected_monitor->selected_client, 1);
        selected_monitor = monitor;
    } else if (!client || client == selected_monitor->selected_client)
        return;
    focus(client);
}

void expose(XEvent *event) {
    Monitor *monitor;
    XExposeEvent *ev = &event->xexpose;

    if (ev->count == 0 && (monitor = wintomon(ev->window)))
        drawbar(monitor);
}

void focus(Client *client) {
    if (!client || !IsVisible(client))
        for (client = selected_monitor->stack; client && !IsVisible(client); client = client->snext);
    if (selected_monitor->selected_client && selected_monitor->selected_client != client)
        unfocus(selected_monitor->selected_client, 0);
    if (client) {
        if (client->monitor != selected_monitor)
            selected_monitor = client->monitor;
        if (client->isurgent)
            seturgent(client, 0);
        detachstack(client);
        attachstack(client);
        grabbuttons(client, 1);
        XSetWindowBorder(global_display, client->window, scheme[SchemeSel][ColBorder].pixel);
        setfocus(client);
    } else {
        XSetInputFocus(global_display, root, RevertToPointerRoot, CurrentTime);
        XDeleteProperty(global_display, root, netatom[NetActiveWindow]);
    }
    selected_monitor->selected_client = client;
    drawbars();
}

/* there are some broken focus acquiring clients needing extra handling */
void focusin(XEvent *event) {
    XFocusChangeEvent *ev = &event->xfocus;

    if (selected_monitor->selected_client && ev->window != selected_monitor->selected_client->window)
        setfocus(selected_monitor->selected_client);
}

void set_current_monitor(Monitor *monitor) {
    unfocus(selected_monitor->selected_client, 0);
    selected_monitor = monitor;
    focus(NULL);
}

void focusmon(const Arg *arg) {
    Monitor *monitor;

    if (!all_monitors->next)
        return;

    if ((monitor = dirtomon(arg->i)) == selected_monitor)
        return;

    set_current_monitor(monitor);
}

void focusstack(const Arg *arg) {
    Client *client = NULL, *i;

    if (!selected_monitor->selected_client || (selected_monitor->selected_client->isfullscreen && lockfullscreen))
        return;
    if (arg->i > 0) {
        for (client = selected_monitor->selected_client->next; client && !IsVisible(client); client = client->next);
        if (!client)
            for (client = selected_monitor->clients; client && !IsVisible(client); client = client->next);
    } else {
        for (i = selected_monitor->clients; i != selected_monitor->selected_client; i = i->next)
            if (IsVisible(i))
                client = i;
        if (!client)
            for (; i; i = i->next)
                if (IsVisible(i))
                    client = i;
    }
    if (client) {
        focus(client);
        restack(selected_monitor);
    }
}

Atom getatomprop(Client *client, Atom prop) {
    int di;
    unsigned long dl;
    unsigned char *p = NULL;
    Atom da, atom = None;

    if (XGetWindowProperty(global_display, client->window, prop, 0L, sizeof(atom), False, XA_ATOM,
                           &da, &di, &dl, &dl, &p) == Success && p) {
        atom = *(Atom *)p;
        XFree(p);
    }
    return atom;
}

pid_t getstatusbarpid() {
    char buf[32], *str = buf, *c;
    FILE *fp;

    if (statuspid > 0) {
        snprintf(buf, sizeof(buf), "/proc/%u/cmdline", statuspid);
        if ((fp = fopen(buf, "r"))) {
            fgets(buf, sizeof(buf), fp);
            while ((c = strchr(str, '/')))
                str = c + 1;
            fclose(fp);
            if (!strcmp(str, STATUSBAR))
                return statuspid;
        }
    }
    if (!(fp = popen("pgrep -o "STATUSBAR, "r")))
        return -1;
    fgets(buf, sizeof(buf), fp);
    pclose(fp);
    return strtol(buf, NULL, 10);
}

int getrootptr(int *x, int *y) {
    int di;
    unsigned int dui;
    Window dummy;

    return XQueryPointer(global_display, root, &dummy, &dummy, x, y, &di, &di, &dui);
}

long getstate(Window window) {
    int format;
    long result = -1;
    unsigned char *p = NULL;
    unsigned long n, extra;
    Atom real;

    if (XGetWindowProperty(global_display, window, wmatom[WMState], 0L, 2L, False, wmatom[WMState],
                           &real, &format, &n, &extra, (unsigned char **)&p) != Success)
        return -1;

    if (n != 0)
        result = *p;

    XFree(p);
    return result;
}

int gettextprop(Window window, Atom atom, char *text, unsigned int size) {
    char **list = NULL;
    int n;
    XTextProperty name;

    if (!text || size == 0)
        return 0;

    text[0] = '\0';
    if (!XGetTextProperty(global_display, window, &name, atom) || !name.nitems)
        return 0;

    if (name.encoding == XA_STRING) {
        strncpy(text, (char *)name.value, size - 1);
    } else {
        if (XmbTextPropertyToTextList(global_display, &name, &list, &n) >= Success && n > 0 && *list) {
            strncpy(text, *list, size - 1);
            XFreeStringList(list);
        }
    }
    text[size - 1] = '\0';
    XFree(name.value);
    return 1;
}

void grabbuttons(Client *client, int focused) {
    updatenumlockmask();
    {
        unsigned int i, j;
        unsigned int modifiers[] = { 0, LockMask, numlockmask, numlockmask|LockMask };
        XUngrabButton(global_display, AnyButton, AnyModifier, client->window);
        if (!focused)
            XGrabButton(global_display, AnyButton, AnyModifier, client->window, False,
                        ButtonMask, GrabModeSync, GrabModeSync, None, None);
        for (i = 0; i < ArrayLength(buttons); i++)
            if (buttons[i].click == ClkClientWin)
                for (j = 0; j < ArrayLength(modifiers); j++)
                    XGrabButton(global_display, buttons[i].button,
                                buttons[i].mask | modifiers[j],
                                client->window, False, ButtonMask,
                                GrabModeAsync, GrabModeSync, None, None);
    }
}

void grabkeys(void) {
    updatenumlockmask();
    {
        unsigned int i, j;
        unsigned int modifiers[] = { 0, LockMask, numlockmask, numlockmask|LockMask };

        XUngrabKey(global_display, AnyKey, AnyModifier, root);
        for (i = 0; i < ArrayLength(keys); i++) {
            KeyCode code = XKeysymToKeycode(global_display, keys[i].keysym);
            if (code != 0) {
                for (j = 0; j < ArrayLength(modifiers); j++) {
                    XGrabKey(global_display, code, keys[i].mod | modifiers[j], root, True, GrabModeAsync, GrabModeAsync);
                }
            }
        }
    }
}

void incnmaster(const Arg *arg) {
    selected_monitor->nmaster = Maximum(selected_monitor->nmaster + arg->i, 0);
    arrange(selected_monitor);
}

#ifdef XINERAMA
static int isuniquegeom(XineramaScreenInfo *unique, size_t n, XineramaScreenInfo *info) {
    while (n--)
        if(unique[n].x_org == info->x_org && unique[n].y_org  == info->y_org
           && unique[n].width == info->width && unique[n].height == info->height)
            return 0;
    return 1;
}
#endif /* XINERAMA */

void keypress(XEvent *event) {
    XKeyEvent *ev = &event->xkey;
    KeySym keysym = XKeycodeToKeysym(global_display, (KeyCode)ev->keycode, 0);

    unsigned int i = 0;
    for(; i < ArrayLength(keys); i++) {
        if (keysym == keys[i].keysym && CleanMask(keys[i].mod) == CleanMask(ev->state) && keys[i].func) {
            keys[i].func(&(keys[i].arg));
            break;
        }
    }

    if(i >= ArrayLength(keys)) {
        // TODO: check to see if a second process (the custom keys process) and its named pipe exist
        // If it does, then open the named pipe and send the bytes to it. If it doesn't then start it
        // and send the bytes over to that process.
    }
}

void killclient(const Arg *arg) {
    if (!selected_monitor->selected_client)
        return;
    if (!sendevent(selected_monitor->selected_client, wmatom[WMDelete])) {
        XGrabServer(global_display);
        XSetErrorHandler(xerrordummy);
        XSetCloseDownMode(global_display, DestroyAll);
        XKillClient(global_display, selected_monitor->selected_client->window);
        XSync(global_display, False);
        XSetErrorHandler(xerror);
        XUngrabServer(global_display);
    }
}

void manage(Window window, XWindowAttributes *wa) {
    Client *client, *t = NULL;
    Window trans = None;
    XWindowChanges wc;

    client = ecalloc(1, sizeof(Client));
    client->window = window;
    /* geometry */
    client->x = client->oldx = wa->x;
    client->y = client->oldy = wa->y;
    client->width = client->old_width = wa->width;
    client->height = client->old_height = wa->height;
    client->old_border_width = wa->border_width;

    updatetitle(client);
    if (XGetTransientForHint(global_display, window, &trans) && (t = wintoclient(trans))) {
        client->monitor = t->monitor;
        client->tags = t->tags;
    } else {
        client->monitor = selected_monitor;
        applyrules(client);
    }

    if (client->x + ClientWidth(client) > client->monitor->screen_x + client->monitor->screen_width)
        client->x = client->monitor->screen_x + client->monitor->screen_width - ClientWidth(client);
    if (client->y + ClientHeight(client) > client->monitor->screen_y + client->monitor->screen_height)
        client->y = client->monitor->screen_y + client->monitor->screen_height - ClientHeight(client);
    client->x = Maximum(client->x, client->monitor->screen_x);
    /* only fix client y-offset, if the client center might cover the bar */
    client->y = Maximum(client->y, ((client->monitor->bar_height == client->monitor->screen_y) && (client->x + (client->width / 2) >= client->monitor->window_x)
                                && (client->x + (client->width / 2) < client->monitor->window_x + client->monitor->window_width)) ? bh : client->monitor->screen_y);
    client->border_width = borderpx;

    wc.border_width = client->border_width;
    XConfigureWindow(global_display, window, CWBorderWidth, &wc);
    XSetWindowBorder(global_display, window, scheme[SchemeNorm][ColBorder].pixel);
    configure(client); /* propagates border_width, if size doesn't change */
    updatewindowtype(client);
    updatesizehints(client);
    updatewmhints(client);
    XSelectInput(global_display, window, EnterWindowMask|FocusChangeMask|PropertyChangeMask|StructureNotifyMask);
    grabbuttons(client, 0);
    if (!client->isfloating)
        client->isfloating = client->oldstate = trans != None || client->isfixed;
    if (client->isfloating)
        XRaiseWindow(global_display, client->window);
    attach(client);
    attachstack(client);
    XChangeProperty(global_display, root, netatom[NetClientList], XA_WINDOW, 32, PropModeAppend,
                    (unsigned char *) &(client->window), 1);
    XMoveResizeWindow(global_display, client->window, client->x + 2 * sw, client->y, client->width, client->height); /* some windows require this */
    setclientstate(client, NormalState);
    if (client->monitor == selected_monitor)
        unfocus(selected_monitor->selected_client, 0);
    client->monitor->selected_client = client;
    arrange(client->monitor);
    XMapWindow(global_display, client->window);
    focus(NULL);
}

void mappingnotify(XEvent *event) {
    XMappingEvent *ev = &event->xmapping;

    XRefreshKeyboardMapping(ev);
    if (ev->request == MappingKeyboard)
        grabkeys();
}

void maprequest(XEvent *event) {
    static XWindowAttributes wa;
    XMapRequestEvent *ev = &event->xmaprequest;

    if (!XGetWindowAttributes(global_display, ev->window, &wa))
        return;
    if (wa.override_redirect)
        return;
    if (!wintoclient(ev->window))
        manage(ev->window, &wa);
}

// Layouts
void monocle(Monitor *monitor) {
    unsigned int n = 0;
    Client *client;

    for (client = monitor->clients; client; client = client->next) {
        if (IsVisible(client)) {
            n++;
        }
    }

    if (n > 0) { /* override layout symbol */
        snprintf(monitor->ltsymbol, sizeof(monitor->ltsymbol), "[%d]", n);
    }

    for (client = nexttiled(monitor->clients); client; client = nexttiled(client->next)) {
        resize(client, monitor->window_x, monitor->window_y, monitor->window_width - 2 * client->border_width, monitor->window_height - 2 * client->border_width, 0);
    }
}

void tile(Monitor *monitor) {
    unsigned int n;
    Client *client = nexttiled(monitor->clients);

    for (n = 0; client; client = nexttiled(client->next), n++) {
    }
    if (n == 0)
        return;

    unsigned int monitor_width = (n > monitor->nmaster) ?
      (monitor->nmaster ? monitor->window_width * monitor->mfact : 0) :
      (monitor->window_width);

    unsigned int ty = 0;
    client = nexttiled(monitor->clients);
    for (unsigned int i = 0, monitor_y = 0; client != NULL; client = nexttiled(client->next), i++) {
        unsigned int height = 0;
        if (i < monitor->nmaster) {
            height = (monitor->window_height - monitor_y) / (Minimum(n, monitor->nmaster) - i);
            resize(client, monitor->window_x, monitor->window_y + monitor_y, monitor_width - (2*client->border_width), height - (2*client->border_width), 0);
 			// resize(client, monitor->window_x, monitor->window_y + monitor_y, monitor_width - (2*client->border_width) + (n > 1 ? gap_size : 0), height - (2*client->border_width), 0);
 			resize(client, monitor->window_x, monitor->window_y + monitor_y, monitor_width - (2*client->border_width) + gap_size, height - (2*client->border_width), 0);
            if (monitor_y + ClientHeight(client) < monitor->window_height) {
                monitor_y += ClientHeight(client);
            }
        } else {
            height = (monitor->window_height - ty) / (n - i);
            resize(client, monitor->window_x + monitor_width, monitor->window_y + ty, monitor->window_width - monitor_width - (2*client->border_width), height - (2*client->border_width), 0);
            if (ty + ClientHeight(client) < monitor->window_height) {
                ty += ClientHeight(client);
            }
        }
    }
}

// --

void motionnotify(XEvent *event) {
    static Monitor *previous_monitor = NULL;
    XMotionEvent *ev = &event->xmotion;

    if (ev->window != root)
        return;

    Monitor *monitor = recttomon(ev->x_root, ev->y_root, 1, 1);
    if (monitor != previous_monitor && previous_monitor) {
        unfocus(selected_monitor->selected_client, 1);
        selected_monitor = monitor;
        focus(NULL);
    }
    previous_monitor = monitor;
}

void movemouse(const Arg *arg) {
    int x, y, ocx, ocy, nx, ny;
    Client *client;
    Monitor *monitor;
    XEvent ev;
    Time lasttime = 0;

    if (!(client = selected_monitor->selected_client))
        return;
    if (client->isfullscreen) /* no support moving fullscreen windows by mouse */
        return;
    restack(selected_monitor);
    ocx = client->x;
    ocy = client->y;
    if (XGrabPointer(global_display, root, False, MouseMask, GrabModeAsync, GrabModeAsync,
                     None, cursor[CurMove]->cursor, CurrentTime) != GrabSuccess)
        return;
    if (!getrootptr(&x, &y))
        return;
    do {
        XMaskEvent(global_display, MouseMask|ExposureMask|SubstructureRedirectMask, &ev);
        switch(ev.type) {
            case ConfigureRequest:
            case Expose:
            case MapRequest:
                handler[ev.type](&ev);
                break;
            case MotionNotify:
                if ((ev.xmotion.time - lasttime) <= (1000 / 60))
                    continue;
                lasttime = ev.xmotion.time;

                nx = ocx + (ev.xmotion.x - x);
                ny = ocy + (ev.xmotion.y - y);
                if (abs(selected_monitor->window_x - nx) < snap)
                    nx = selected_monitor->window_x;
                else if (abs((selected_monitor->window_x + selected_monitor->window_width) - (nx + ClientWidth(client))) < snap)
                    nx = selected_monitor->window_x + selected_monitor->window_width - ClientWidth(client);
                if (abs(selected_monitor->window_y - ny) < snap)
                    ny = selected_monitor->window_y;
                else if (abs((selected_monitor->window_y + selected_monitor->window_height) - (ny + ClientHeight(client))) < snap)
                    ny = selected_monitor->window_y + selected_monitor->window_height - ClientHeight(client);
                if (!client->isfloating && layouts[selected_monitor->selected_layout].arrange
                    && (abs(nx - client->x) > snap || abs(ny - client->y) > snap))
                    togglefloating(NULL);
                if (!layouts[selected_monitor->selected_layout].arrange || client->isfloating)
                    resize(client, nx, ny, client->width, client->height, 1);
                break;
        }
    } while (ev.type != ButtonRelease);
    XUngrabPointer(global_display, CurrentTime);

    if ((monitor = recttomon(client->x, client->y, client->width, client->height)) != selected_monitor) {
        sendmon(client, monitor);
        selected_monitor = monitor;
        focus(NULL);
    }
}

Client *nexttiled(Client *client) {
    for (; client && (client->isfloating || !IsVisible(client)); client = client->next);
    return client;
}

void pop(Client *client) {
    detach(client);
    attach(client);
    focus(client);
    arrange(client->monitor);
}

void propertynotify(XEvent *event) {
    Client *client;
    Window trans;
    XPropertyEvent *ev = &event->xproperty;

    if ((ev->window == root) && (ev->atom == XA_WM_NAME))
        updatestatus();
    else if (ev->state == PropertyDelete)
        return; /* ignore */
    else if ((client = wintoclient(ev->window))) {
        switch(ev->atom) {
            default: break;
            case XA_WM_TRANSIENT_FOR:
                     if (!client->isfloating && (XGetTransientForHint(global_display, client->window, &trans)) &&
                         (client->isfloating = (wintoclient(trans)) != NULL))
                         arrange(client->monitor);
                     break;
            case XA_WM_NORMAL_HINTS:
                     updatesizehints(client);
                     break;
            case XA_WM_HINTS:
                     updatewmhints(client);
                     drawbars();
                     break;
        }
        if (ev->atom == XA_WM_NAME || ev->atom == netatom[NetWMName]) {
            updatetitle(client);
            if (client == client->monitor->selected_client)
                drawbar(client->monitor);
        }
        if (ev->atom == netatom[NetWMWindowType])
            updatewindowtype(client);
    }
}

void quit(const Arg *arg) {
    global_running = 0;
}

Monitor *recttomon(int x, int y, int width, int height) {
    Monitor *monitor, *r = selected_monitor;
    int a, area = 0;

    for (monitor = all_monitors; monitor; monitor = monitor->next)
        if ((a = Intersect(x, y, width, height, monitor)) > area) {
            area = a;
            r = monitor;
        }
    return r;
}

void resize(Client *client, int x, int y, int width, int height, int interact) {
    if (applysizehints(client, &x, &y, &width, &height, interact))
        resizeclient(client, x, y, width, height);
}

void resizeclient(Client *client, int x, int y, int width, int height) {
    XWindowChanges wc;
 	unsigned int n;
 	unsigned int gapoffset;
 	unsigned int gapincr;
 	Client *nbc;

    // client->oldx = client->x; client->x = wc.x = x;
    // client->oldy = client->y; client->y = wc.y = y;
    // client->old_width = client->width; client->width = wc.width = width;
    // client->old_height = client->height; client->height = wc.height = height;
    wc.border_width = client->border_width;
 
 	/* Get number of clients for the client's monitor */
 	for (n = 0, nbc = nexttiled(client->monitor->clients); nbc; nbc = nexttiled(nbc->next), n++);
 
 	/* Do nothing if layout is floating */
 	if (client->isfloating || layouts[client->monitor->selected_layout].arrange == NULL) {
 		gapincr = gapoffset = 0;
 	} else {
 		/* Remove border and gap if layout is monocle or only one client */
 		if (layouts[client->monitor->selected_layout].arrange == monocle || n == 1) {
 			// gapoffset = 0;
 			gapincr = -2 * borderpx;
 			wc.border_width = 0;
 		} else {
 			gapincr = 2 * gap_size;
 		}
        gapoffset = gap_size;
 	}
 
 	client->oldx = client->x; client->x = wc.x = x + gapoffset;
 	client->oldy = client->y; client->y = wc.y = y + gapoffset;
 	client->old_width  = client->width; client->width = wc.width = width - gapincr;
 	client->old_height = client->height; client->height = wc.height = height - gapincr;
 

    XConfigureWindow(global_display, client->window, CWX|CWY|CWWidth|CWHeight|CWBorderWidth, &wc);
    configure(client);
    XSync(global_display, False);
}

void resizemouse(const Arg *arg) {
    int ocx, ocy, nw, nh;
    Client *client;
    Monitor *monitor;
    XEvent ev;
    Time lasttime = 0;

    if (!(client = selected_monitor->selected_client))
        return;
    if (client->isfullscreen) /* no support resizing fullscreen windows by mouse */
        return;
    restack(selected_monitor);
    ocx = client->x;
    ocy = client->y;
    if (XGrabPointer(global_display, root, False, MouseMask, GrabModeAsync, GrabModeAsync,
                     None, cursor[CurResize]->cursor, CurrentTime) != GrabSuccess)
        return;
    XWarpPointer(global_display, None, client->window, 0, 0, 0, 0, client->width + client->border_width - 1, client->height + client->border_width - 1);
    do {
        XMaskEvent(global_display, MouseMask|ExposureMask|SubstructureRedirectMask, &ev);
        switch(ev.type) {
            case ConfigureRequest:
            case Expose:
            case MapRequest:
                handler[ev.type](&ev);
                break;
            case MotionNotify:
                if ((ev.xmotion.time - lasttime) <= (1000 / 60))
                    continue;
                lasttime = ev.xmotion.time;

                nw = Maximum(ev.xmotion.x - ocx - 2 * client->border_width + 1, 1);
                nh = Maximum(ev.xmotion.y - ocy - 2 * client->border_width + 1, 1);
                if (client->monitor->window_x + nw >= selected_monitor->window_x && client->monitor->window_x + nw <= selected_monitor->window_x + selected_monitor->window_width
                    && client->monitor->window_y + nh >= selected_monitor->window_y && client->monitor->window_y + nh <= selected_monitor->window_y + selected_monitor->window_height)
                {
                    if (!client->isfloating && layouts[selected_monitor->selected_layout].arrange
                        && (abs(nw - client->width) > snap || abs(nh - client->height) > snap))
                        togglefloating(NULL);
                }
                if (!layouts[selected_monitor->selected_layout].arrange || client->isfloating)
                    resize(client, client->x, client->y, nw, nh, 1);
                break;
        }
    } while (ev.type != ButtonRelease);
    XWarpPointer(global_display, None, client->window, 0, 0, 0, 0, client->width + client->border_width - 1, client->height + client->border_width - 1);
    XUngrabPointer(global_display, CurrentTime);
    while (XCheckMaskEvent(global_display, EnterWindowMask, &ev));
    if ((monitor = recttomon(client->x, client->y, client->width, client->height)) != selected_monitor) {
        sendmon(client, monitor);
        selected_monitor = monitor;
        focus(NULL);
    }
}

void restack(Monitor *monitor) {
    Client *client;
    XEvent ev;
    XWindowChanges wc;

    drawbar(monitor);
    if (!monitor->selected_client)
        return;
    if (monitor->selected_client->isfloating || !layouts[monitor->selected_layout].arrange)
        XRaiseWindow(global_display, monitor->selected_client->window);
    if (layouts[monitor->selected_layout].arrange) {
        wc.stack_mode = Below;
        wc.sibling = monitor->barwin;
        for (client = monitor->stack; client; client = client->snext)
            if (!client->isfloating && IsVisible(client)) {
                XConfigureWindow(global_display, client->window, CWSibling|CWStackMode, &wc);
                wc.sibling = client->window;
            }
    }
    XSync(global_display, False);
    while (XCheckMaskEvent(global_display, EnterWindowMask, &ev));
}

void run(void) {
    XEvent ev;
    /* main event loop */
    XSync(global_display, False);
    while (global_running && !XNextEvent(global_display, &ev))
        if (handler[ev.type])
            handler[ev.type](&ev); /* call handler */
}

void scan(void) {
    unsigned int i, num;
    Window d1, d2, *wins = NULL;
    XWindowAttributes wa;

    if (XQueryTree(global_display, root, &d1, &d2, &wins, &num)) {
        for (i = 0; i < num; i++) {
            if (!XGetWindowAttributes(global_display, wins[i], &wa)
                || wa.override_redirect || XGetTransientForHint(global_display, wins[i], &d1))
                continue;
            if (wa.map_state == IsViewable || getstate(wins[i]) == IconicState)
                manage(wins[i], &wa);
        }
        for (i = 0; i < num; i++) { /* now the transients */
            if (!XGetWindowAttributes(global_display, wins[i], &wa))
                continue;
            if (XGetTransientForHint(global_display, wins[i], &d1)
                && (wa.map_state == IsViewable || getstate(wins[i]) == IconicState))
                manage(wins[i], &wa);
        }
        if (wins)
            XFree(wins);
    }
}

void
sendmon(Client *client, Monitor *monitor)
{
    if (client->monitor == monitor)
        return;
    unfocus(client, 1);
    detach(client);
    detachstack(client);
    client->monitor = monitor;
    client->tags = monitor->selected_tags; /* assign tags of target monitor */
    attach(client);
    attachstack(client);
    focus(NULL);
    arrange(NULL);
}

void
setclientstate(Client *client, long state)
{
    long data[] = { state, None };

    XChangeProperty(global_display, client->window, wmatom[WMState], wmatom[WMState], 32,
                    PropModeReplace, (unsigned char *)data, 2);
}

int
sendevent(Client *client, Atom proto)
{
    int n;
    Atom *protocols;
    int exists = 0;
    XEvent ev;

    if (XGetWMProtocols(global_display, client->window, &protocols, &n)) {
        while (!exists && n--)
            exists = protocols[n] == proto;
        XFree(protocols);
    }
    if (exists) {
        ev.type = ClientMessage;
        ev.xclient.window = client->window;
        ev.xclient.message_type = wmatom[WMProtocols];
        ev.xclient.format = 32;
        ev.xclient.data.l[0] = proto;
        ev.xclient.data.l[1] = CurrentTime;
        XSendEvent(global_display, client->window, False, NoEventMask, &ev);
    }
    return exists;
}

void setfocus(Client *client) {
    if (!client->neverfocus) {
        XSetInputFocus(global_display, client->window, RevertToPointerRoot, CurrentTime);
        XChangeProperty(global_display, root, netatom[NetActiveWindow],
                        XA_WINDOW, 32, PropModeReplace,
                        (unsigned char *) &(client->window), 1);
    }
    sendevent(client, wmatom[WMTakeFocus]);
}

void setfullscreen(Client *client, int fullscreen) {
    if (fullscreen && !client->isfullscreen) {
        XChangeProperty(global_display, client->window, netatom[NetWMState], XA_ATOM, 32,
                        PropModeReplace, (unsigned char*)&netatom[NetWMFullscreen], 1);
        client->isfullscreen = 1;
        client->oldstate = client->isfloating;
        client->old_border_width = client->border_width;
        client->border_width = 0;
        client->isfloating = 1;
        resizeclient(client, client->monitor->screen_x, client->monitor->screen_y, client->monitor->screen_width, client->monitor->screen_height);
        XRaiseWindow(global_display, client->window);
    } else if (!fullscreen && client->isfullscreen){
        XChangeProperty(global_display, client->window, netatom[NetWMState], XA_ATOM, 32,
                        PropModeReplace, (unsigned char*)0, 0);
        client->isfullscreen = 0;
        client->isfloating = client->oldstate;
        client->border_width = client->old_border_width;
        client->x = client->oldx;
        client->y = client->oldy;
        client->width = client->old_width;
        client->height = client->old_height;
        resizeclient(client, client->x, client->y, client->width, client->height);
        arrange(client->monitor);
    }
}

void setlayout(const Arg *arg) {
    if (!arg || !arg->v || arg->v != &layouts[selected_monitor->selected_layout]) {
        selected_monitor->selected_layout ^= 1;
    }

    if (arg && arg->v) {
        // layouts[selected_monitor->selected_layout] = (Layout *)arg->v;
    }

    // strncpy(selected_monitor->ltsymbol, layouts[selected_monitor->selected_layout]->symbol, sizeof(selected_monitor->ltsymbol));
    if (selected_monitor->selected_client) {
        arrange(selected_monitor);
    } else {
        drawbar(selected_monitor);
    }
}

void toggle_layout(const Arg *arg) {
    selected_monitor->selected_layout ^= 1;
    if (selected_monitor->selected_client) {
        arrange(selected_monitor);
    } else {
        drawbar(selected_monitor);
    }

    // const Layout *current_layout = layouts[selected_monitor->selected_layout];

    // Toggle between tiled and monocle (fullscreen-like layout)
    // setlayout(&layout_arg);
}

/* arg > 1.0 will set mfact absolutely */
void setmfact(const Arg *arg) {
    float f;

    if (!arg || !layouts[selected_monitor->selected_layout].arrange)
        return;
    f = arg->f < 1.0 ? arg->f + selected_monitor->mfact : arg->f - 1.0;
    if (f < 0.05 || f > 0.95)
        return;
    selected_monitor->mfact = f;
    arrange(selected_monitor);
}

void setup(void) {
    int i;
    XSetWindowAttributes wa;
    Atom utf8string;

    /* clean up any zombies immediately */
    sigchld(0);

    /* init screen */
    screen = DefaultScreen(global_display);
    sw = DisplayWidth(global_display, screen);
    sh = DisplayHeight(global_display, screen);
    root = RootWindow(global_display, screen);
    drw = drw_create(global_display, screen, root, sw, sh);
    if (!drw_fontset_create(drw, fonts, ArrayLength(fonts)))
        die("no fonts could be loaded.");
    lrpad = drw->fonts->height;
    bh = drw->fonts->height + 2;
    updategeom();
    /* init atoms */
    utf8string = XInternAtom(global_display, "UTF8_STRING", False);
    wmatom[WMProtocols] = XInternAtom(global_display, "WM_PROTOCOLS", False);
    wmatom[WMDelete] = XInternAtom(global_display, "WM_DELETE_WINDOW", False);
    wmatom[WMState] = XInternAtom(global_display, "WM_STATE", False);
    wmatom[WMTakeFocus] = XInternAtom(global_display, "WM_TAKE_FOCUS", False);
    netatom[NetActiveWindow] = XInternAtom(global_display, "_NET_ACTIVE_WINDOW", False);
    netatom[NetSupported] = XInternAtom(global_display, "_NET_SUPPORTED", False);
    netatom[NetWMName] = XInternAtom(global_display, "_NET_WM_NAME", False);
    netatom[NetWMState] = XInternAtom(global_display, "_NET_WM_STATE", False);
    netatom[NetWMCheck] = XInternAtom(global_display, "_NET_SUPPORTING_WM_CHECK", False);
    netatom[NetWMFullscreen] = XInternAtom(global_display, "_NET_WM_STATE_FULLSCREEN", False);
    netatom[NetWMWindowType] = XInternAtom(global_display, "_NET_WM_WINDOW_TYPE", False);
    netatom[NetWMWindowTypeDialog] = XInternAtom(global_display, "_NET_WM_WINDOW_TYPE_DIALOG", False);
    netatom[NetClientList] = XInternAtom(global_display, "_NET_CLIENT_LIST", False);
    /* init cursors */
    cursor[CurNormal] = drw_cur_create(drw, XC_left_ptr);
    cursor[CurResize] = drw_cur_create(drw, XC_sizing);
    cursor[CurMove] = drw_cur_create(drw, XC_fleur);
    /* init appearance */
    scheme = ecalloc(ArrayLength(colors), sizeof(Clr *));
    for (i = 0; i < ArrayLength(colors); i++)
        scheme[i] = drw_scm_create(drw, colors[i], 3);
    /* init bars */
    updatebars();
    updatestatus();
    /* supporting window for NetWMCheck */
    wmcheckwin = XCreateSimpleWindow(global_display, root, 0, 0, 1, 1, 0, 0, 0);
    XChangeProperty(global_display, wmcheckwin, netatom[NetWMCheck], XA_WINDOW, 32,
                    PropModeReplace, (unsigned char *) &wmcheckwin, 1);
    XChangeProperty(global_display, wmcheckwin, netatom[NetWMName], utf8string, 8,
                    PropModeReplace, (unsigned char *) "dwm", 3);
    XChangeProperty(global_display, root, netatom[NetWMCheck], XA_WINDOW, 32,
                    PropModeReplace, (unsigned char *) &wmcheckwin, 1);
    /* EWMH support per view */
    XChangeProperty(global_display, root, netatom[NetSupported], XA_ATOM, 32,
                    PropModeReplace, (unsigned char *) netatom, NetLast);
    XDeleteProperty(global_display, root, netatom[NetClientList]);
    /* select events */
    wa.cursor = cursor[CurNormal]->cursor;
    wa.event_mask = SubstructureRedirectMask|SubstructureNotifyMask
      |ButtonPressMask|PointerMotionMask|EnterWindowMask
      |LeaveWindowMask|StructureNotifyMask|PropertyChangeMask;
    XChangeWindowAttributes(global_display, root, CWEventMask|CWCursor, &wa);
    XSelectInput(global_display, root, wa.event_mask);
    grabkeys();
    focus(NULL);
}


void
seturgent(Client *client, int urg)
{
    XWMHints *wmh;

    client->isurgent = urg;
    if (!(wmh = XGetWMHints(global_display, client->window)))
        return;
    wmh->flags = urg ? (wmh->flags | XUrgencyHint) : (wmh->flags & ~XUrgencyHint);
    XSetWMHints(global_display, client->window, wmh);
    XFree(wmh);
}

void showhide(Client *client) {
    if (!client)
        return;
    if (IsVisible(client)) {
        /* show clients top down */
        XMoveWindow(global_display, client->window, client->x, client->y);
        if ((!layouts[client->monitor->selected_layout].arrange || client->isfloating) && !client->isfullscreen) {
            resize(client, client->x, client->y, client->width, client->height, 0);
        }
        showhide(client->snext);
    } else {
        /* hide clients bottom up */
        showhide(client->snext);
        XMoveWindow(global_display, client->window, ClientWidth(client) * -2, client->y);
    }
}

void sigchld(int unused) {
    // TODO: Handle dead app launcher or status bar
    if (signal(SIGCHLD, sigchld) == SIG_ERR)
        die("can't install SIGCHLD handler:");
    while (0 < waitpid(-1, NULL, WNOHANG));
}

void sigstatusbar(const Arg *arg) {
    union sigval sv;

    if (!statussig)
        return;
    sv.sival_int = arg->i;
    if ((statuspid = getstatusbarpid()) <= 0)
        return;

    sigqueue(statuspid, SIGRTMIN+statussig, sv);
}

void spawn(const Arg *arg) {
    if (arg->v == dmenucmd)
        dmenumon[0] = '0' + selected_monitor->num;
    if (fork() == 0) {
        if (global_display)
            close(ConnectionNumber(global_display));
        setsid();
        execvp(((char **)arg->v)[0], (char **)arg->v);
        fprintf(stderr, "dwm: execvp %s", ((char **)arg->v)[0]);
        perror(" failed");
        exit(EXIT_SUCCESS);
    }
}

void tag(const Arg *arg) {
    if (selected_monitor->selected_client && arg->ui & TagMask) {
        selected_monitor->selected_client->tags = arg->ui & TagMask;
        focus(NULL);
        arrange(selected_monitor);
    }
}

void tagmon(const Arg *arg) {
    if (!selected_monitor->selected_client || !all_monitors->next)
        return;
    sendmon(selected_monitor->selected_client, dirtomon(arg->i));
}

void togglebar(const Arg *arg) {
    selected_monitor->showbar = !selected_monitor->showbar;
    updatebarpos(selected_monitor);
    XMoveResizeWindow(global_display, selected_monitor->barwin, selected_monitor->window_x, selected_monitor->bar_height, selected_monitor->window_width, bh);
    arrange(selected_monitor);
}

void togglefloating(const Arg *arg) {
    if (!selected_monitor->selected_client)
        return;
    if (selected_monitor->selected_client->isfullscreen) /* no support for fullscreen windows */
        return;
    selected_monitor->selected_client->isfloating = !selected_monitor->selected_client->isfloating || selected_monitor->selected_client->isfixed;
    if (selected_monitor->selected_client->isfloating) {
        resize(selected_monitor->selected_client, selected_monitor->selected_client->x, selected_monitor->selected_client->y,
               selected_monitor->selected_client->width, selected_monitor->selected_client->height, 0);
    }
    arrange(selected_monitor);
}

void toggletag(const Arg *arg) {
    unsigned int newtags;

    if (!selected_monitor->selected_client)
        return;
    newtags = selected_monitor->selected_client->tags ^ (arg->ui & TagMask);
    if (newtags) {
        selected_monitor->selected_client->tags = newtags;
        focus(NULL);
        arrange(selected_monitor);
    }
}

void toggleview(const Arg *arg) {
    unsigned int newtagset = selected_monitor->selected_tags ^ (arg->ui & TagMask);

    if (newtagset) {
        selected_monitor->selected_tags = newtagset;
        focus(NULL);
        arrange(selected_monitor);
    }
}

void change_gap(const Arg *arg) {
    int new_gap_size = gap_size + arg->i;
    if(new_gap_size >= 0) {
        gap_size = new_gap_size;
        focus(NULL);
        arrange(NULL);
    }
}

void unfocus(Client *client, int setfocus) {
    if (!client)
        return;

    grabbuttons(client, 0);
    XSetWindowBorder(global_display, client->window, scheme[SchemeNorm][ColBorder].pixel);
    if (setfocus) {
        XSetInputFocus(global_display, root, RevertToPointerRoot, CurrentTime);
        XDeleteProperty(global_display, root, netatom[NetActiveWindow]);
    }
}

void unmanage(Client *client, int destroyed) {
    Monitor *monitor = client->monitor;
    XWindowChanges wc;

    detach(client);
    detachstack(client);
    if (!destroyed) {
        wc.border_width = client->old_border_width;
        XGrabServer(global_display); /* avoid race conditions */
        XSetErrorHandler(xerrordummy);
        XConfigureWindow(global_display, client->window, CWBorderWidth, &wc); /* restore border */
        XUngrabButton(global_display, AnyButton, AnyModifier, client->window);
        setclientstate(client, WithdrawnState);
        XSync(global_display, False);
        XSetErrorHandler(xerror);
        XUngrabServer(global_display);
    }
    free(client);
    focus(NULL);
    updateclientlist();
    arrange(monitor);
}

void
unmapnotify(XEvent *event)
{
    Client *client;
    XUnmapEvent *ev = &event->xunmap;

    if ((client = wintoclient(ev->window))) {
        if (ev->send_event)
            setclientstate(client, WithdrawnState);
        else
            unmanage(client, 0);
    }
}

void
updatebars(void)
{
    Monitor *monitor;
    XSetWindowAttributes wa = {
        .override_redirect = True,
        .background_pixmap = ParentRelative,
        .event_mask = ButtonPressMask|ExposureMask
    };
    XClassHint ch = {"dwm", "dwm"};
    for (monitor = all_monitors; monitor; monitor = monitor->next) {
        if (monitor->barwin)
            continue;
        monitor->barwin = XCreateWindow(global_display, root, monitor->window_x, monitor->bar_height, monitor->window_width, bh, 0, DefaultDepth(global_display, screen),
                                        CopyFromParent, DefaultVisual(global_display, screen),
                                        CWOverrideRedirect|CWBackPixmap|CWEventMask, &wa);
        XDefineCursor(global_display, monitor->barwin, cursor[CurNormal]->cursor);
        XMapRaised(global_display, monitor->barwin);
        XSetClassHint(global_display, monitor->barwin, &ch);
    }
}

void
updatebarpos(Monitor *monitor)
{
    monitor->window_y = monitor->screen_y;
    monitor->window_height = monitor->screen_height;
    if (monitor->showbar) {
        monitor->window_height -= bh;
        monitor->bar_height = monitor->topbar ? monitor->window_y : monitor->window_y + monitor->window_height;
        monitor->window_y = monitor->topbar ? monitor->window_y + bh : monitor->window_y;
    } else
        monitor->bar_height = -bh;
}

void updateclientlist() {
    XDeleteProperty(global_display, root, netatom[NetClientList]);
    for (Monitor *monitor = all_monitors; monitor; monitor = monitor->next)
        for (Client *client = monitor->clients; client; client = client->next)
            XChangeProperty(global_display, root, netatom[NetClientList],
                            XA_WINDOW, 32, PropModeAppend,
                            (unsigned char *) &(client->window), 1);
}

int updategeom(void) {
    int dirty = 0;

#ifdef XINERAMA
    if (XineramaIsActive(global_display)) {
        int i, j, monitor_count, num_screens;
        Client *client;
        XineramaScreenInfo *info = XineramaQueryScreens(global_display, &num_screens);
        XineramaScreenInfo *unique = NULL;

        Monitor *monitor = all_monitors;
        for (monitor_count = 0; monitor; monitor = monitor->next, monitor_count++) {
        }
        /* only consider unique geometries as separate screens */
        unique = ecalloc(num_screens, sizeof(XineramaScreenInfo));
        for (i = 0, j = 0; i < num_screens; i++)
            if (isuniquegeom(unique, j, &info[i]))
                memcpy(&unique[j++], &info[i], sizeof(XineramaScreenInfo));
        XFree(info);
        num_screens = j;
        if (monitor_count <= num_screens) { /* new monitors available */
            for (i = 0; i < (num_screens - monitor_count); i++) {
                Monitor *new_monitor = createmon();

                if(all_monitors) {
                    Monitor *current_monitor = all_monitors;
                    for (; current_monitor->next; current_monitor = current_monitor->next) {
                    }
                    current_monitor->next = new_monitor;
                } else {
                    all_monitors = new_monitor;
                }
            }

            if (all_monitors) {
                Monitor *current_monitor = all_monitors;
                for (int i = 0; i < num_screens; current_monitor = current_monitor->next, i++) {
                    if(i >= monitor_count
                       || unique[i].x_org  != current_monitor->screen_x
                       || unique[i].y_org  != current_monitor->screen_y
                       || unique[i].width  != current_monitor->screen_width
                       || unique[i].height != current_monitor->screen_height) {
                        dirty = 1;
                        current_monitor->num = i;
                        current_monitor->screen_x = current_monitor->window_x = unique[i].x_org;
                        current_monitor->screen_y = current_monitor->window_y = unique[i].y_org;
                        current_monitor->screen_width = current_monitor->window_width = unique[i].width;
                        current_monitor->screen_height = current_monitor->window_height = unique[i].height;
                        updatebarpos(current_monitor);
                    }
                }
            }
        } else { /* less monitors available num_screens < monitor_count */
            for (i = num_screens; i < monitor_count; i++) {
                for (monitor = all_monitors; monitor && monitor->next; monitor = monitor->next);
                while ((client = monitor->clients)) {
                    dirty = 1;
                    monitor->clients = client->next;
                    detachstack(client);
                    client->monitor = all_monitors;
                    attach(client);
                    attachstack(client);
                }
                if (monitor == selected_monitor)
                    selected_monitor = all_monitors;
                cleanup_monitors(monitor);
            }
        }
        free(unique);
    } else
#endif /* XINERAMA */
    { /* default monitor setup */
        if (!all_monitors)
            all_monitors = createmon();
        if (all_monitors->screen_width != sw || all_monitors->screen_height != sh) {
            dirty = 1;
            all_monitors->screen_width = all_monitors->window_width = sw;
            all_monitors->screen_height = all_monitors->window_height = sh;
            updatebarpos(all_monitors);
        }
    }
    if (dirty) {
        selected_monitor = all_monitors;
        selected_monitor = wintomon(root);
    }
    return dirty;
}

void
updatenumlockmask(void)
{
    unsigned int i, j;
    XModifierKeymap *modmap;

    numlockmask = 0;
    modmap = XGetModifierMapping(global_display);
    for (i = 0; i < 8; i++)
        for (j = 0; j < modmap->max_keypermod; j++)
            if (modmap->modifiermap[i * modmap->max_keypermod + j]
                == XKeysymToKeycode(global_display, XK_Num_Lock))
                numlockmask = (1 << i);
    XFreeModifiermap(modmap);
}

void
updatesizehints(Client *client)
{
    long msize;
    XSizeHints size;

    if (!XGetWMNormalHints(global_display, client->window, &size, &msize))
        /* size is uninitialized, ensure that size.flags aren't used */
        size.flags = PSize;
    if (size.flags & PBaseSize) {
        client->base_width = size.base_width;
        client->base_height = size.base_height;
    } else if (size.flags & PMinSize) {
        client->base_width = size.min_width;
        client->base_height = size.min_height;
    } else
        client->base_width = client->base_height = 0;
    if (size.flags & PResizeInc) {
        client->inc_width = size.width_inc;
        client->inc_height = size.height_inc;
    } else
        client->inc_width = client->inc_height = 0;
    if (size.flags & PMaxSize) {
        client->max_width = size.max_width;
        client->max_height = size.max_height;
    } else
        client->max_width = client->max_height = 0;
    if (size.flags & PMinSize) {
        client->min_width = size.min_width;
        client->min_height = size.min_height;
    } else if (size.flags & PBaseSize) {
        client->min_width = size.base_width;
        client->min_height = size.base_height;
    } else
        client->min_width = client->min_height = 0;
    if (size.flags & PAspect) {
        client->min_aspect = (float)size.min_aspect.y / size.min_aspect.x;
        client->max_aspect = (float)size.max_aspect.x / size.max_aspect.y;
    } else
        client->max_aspect = client->min_aspect = 0.0;
    client->isfixed = (client->max_width && client->max_height && client->max_width == client->min_width && client->max_height == client->min_height);
}

void updatestatus(void) {
    if (!gettextprop(root, XA_WM_NAME, stext, sizeof(stext))) {
        strcpy(stext, "dwm-"VERSION);
        statusw = TEXTW(stext) - lrpad + 2;
    } else {
        char *text, *s, ch;

        statusw  = 0;
        for (text = s = stext; *s; s++) {
            if ((unsigned char)(*s) < ' ') {
                ch = *s;
                *s = '\0';
                statusw += TEXTW(text) - lrpad;
                *s = ch;
                text = s + 1;
            }
        }
        statusw += TEXTW(text) - lrpad + 2;
    }
    drawbar(selected_monitor);
}

void
updatetitle(Client *client)
{
    if (!gettextprop(client->window, netatom[NetWMName], client->name, sizeof(client->name)))
        gettextprop(client->window, XA_WM_NAME, client->name, sizeof(client->name));
    if (client->name[0] == '\0') /* hack to mark broken clients */
        strcpy(client->name, broken);
}

void
updatewindowtype(Client *client)
{
    Atom state = getatomprop(client, netatom[NetWMState]);
    Atom wtype = getatomprop(client, netatom[NetWMWindowType]);

    if (state == netatom[NetWMFullscreen])
        setfullscreen(client, 1);
    if (wtype == netatom[NetWMWindowTypeDialog])
        client->isfloating = 1;
}

void updatewmhints(Client *client) {
    XWMHints *wmh;

    if((wmh = XGetWMHints(global_display, client->window)) != NULL) {
        if(client == selected_monitor->selected_client && wmh->flags & XUrgencyHint) {
            wmh->flags &= ~XUrgencyHint;
            XSetWMHints(global_display, client->window, wmh);
        } else {
            client->isurgent = (wmh->flags & XUrgencyHint) ? 1 : 0;
        }

        if(wmh->flags & InputHint) {
            client->neverfocus = !wmh->input;
        } else {
            client->neverfocus = 0;
        }
        XFree(wmh);
    }
}

void view(const Arg *arg) {
    int new_tags = arg->ui & TagMask;
    if (new_tags != selected_monitor->selected_tags) {
        if (new_tags)
            selected_monitor->selected_tags = new_tags;

        focus(NULL);
        arrange(selected_monitor);
    }
}

Client *wintoclient(Window window) {
    for (Monitor *monitor = all_monitors; monitor; monitor = monitor->next)
        for (Client *client = monitor->clients; client; client = client->next)
            if (client->window == window)
                return client;
    return NULL;
}

Monitor *wintomon(Window window) {
    int x = 0, y = 0;
    if (window == root && getrootptr(&x, &y))
        return recttomon(x, y, 1, 1);

    for (Monitor *monitor = all_monitors; monitor; monitor = monitor->next)
        if (window == monitor->barwin)
            return monitor;

    Client *client = NULL;
    if ((client = wintoclient(window)))
        return client->monitor;

    return selected_monitor;
}

/* There's no way to check accesses to destroyed windows, thus those cases are
 * ignored (especially on UnmapNotify's). Other types of errors call Xlibs
 * default error handler, which may call exit. */
int xerror(Display *display, XErrorEvent *ee) {
    if (ee->error_code == BadWindow
        || (ee->request_code == X_SetInputFocus && ee->error_code == BadMatch)
        || (ee->request_code == X_PolyText8 && ee->error_code == BadDrawable)
        || (ee->request_code == X_PolyFillRectangle && ee->error_code == BadDrawable)
        || (ee->request_code == X_PolySegment && ee->error_code == BadDrawable)
        || (ee->request_code == X_ConfigureWindow && ee->error_code == BadMatch)
        || (ee->request_code == X_GrabButton && ee->error_code == BadAccess)
        || (ee->request_code == X_GrabKey && ee->error_code == BadAccess)
        || (ee->request_code == X_CopyArea && ee->error_code == BadDrawable))
        return 0;
    fprintf(stderr, "dwm: fatal error: request code=%d, error code=%d\n",
            ee->request_code, ee->error_code);
    return xerrorxlib(display, ee); /* may call exit */
}

int xerrordummy(Display *display, XErrorEvent *ee) {
    return 0;
}

/* Startup Error handler to check if another window manager is already running. */
int xerrorstart(Display *display, XErrorEvent *ee) {
    die("dwm: another window manager is already running");
    return -1;
}

void zoom(const Arg *arg) {
    Client *client = selected_monitor->selected_client;

    if(!layouts[selected_monitor->selected_layout].arrange
       || (selected_monitor->selected_client && selected_monitor->selected_client->isfloating))
        return;

    if(client == nexttiled(selected_monitor->clients))
        if(!client || !(client = nexttiled(client->next)))
            return;

    pop(client);
}

int main(int argc, char *argv[]) {
    unused(incnmaster, setlayout, togglebar);
    if (argc == 2 && !strcmp("-v", argv[1]))
        die("dwm-"VERSION);
    else if (argc != 1)
        die("usage: dwm [-v]");
    if (!setlocale(LC_CTYPE, "") || !XSupportsLocale())
        fputs("warning: no locale support\n", stderr);
    if (!(global_display = XOpenDisplay(NULL)))
        die("dwm: cannot open global_display");
    checkotherwm();
    setup();
#ifdef __OpenBSD__
    if (pledge("stdio rpath proc exec", NULL) == -1)
        die("pledge");
#endif /* __OpenBSD__ */
    scan();
    run();
    cleanup();
    XCloseDisplay(global_display);
    return EXIT_SUCCESS;
}

