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
 *  - Upload this to github for safekeeping
 *
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
#define BUTTONMASK              (ButtonPressMask|ButtonReleaseMask)
#define CLEANMASK(mask)         (mask & ~(numlockmask|LockMask) & (ShiftMask|ControlMask|Mod1Mask|Mod2Mask|Mod3Mask|Mod4Mask|Mod5Mask))
#define INTERSECT(x,y,w,h,m)    (MAX(0, MIN((x)+(w),(m)->wx+(m)->ww) - MAX((x),(m)->wx)) \
                               * MAX(0, MIN((y)+(h),(m)->wy+(m)->wh) - MAX((y),(m)->wy)))
#define ISVISIBLE(C)            ((C->tags & C->mon->tagset[C->mon->seltags]))
#define LENGTH(X)               (sizeof(X) / sizeof((X)[0]))
#define MOUSEMASK               (BUTTONMASK|PointerMotionMask)
#define WIDTH(X)                ((X)->w + 2 * (X)->bw)
#define HEIGHT(X)               ((X)->h + 2 * (X)->bw)
#define TAGMASK                 ((1 << LENGTH(tags)) - 1)
#define TEXTW(X)                (drw_fontset_getwidth(drw, (X)) + lrpad)

/* enums */
enum { CurNormal, CurResize, CurMove, CurLast }; /* cursor */
enum { SchemeNorm, SchemeSel }; /* color schemes */
enum { NetSupported, NetWMName, NetWMState, NetWMCheck,
       NetWMFullscreen, NetActiveWindow, NetWMWindowType,
       NetWMWindowTypeDialog, NetClientList, NetLast }; /* EWMH atoms */
enum { WMProtocols, WMDelete, WMState, WMTakeFocus, WMLast }; /* default atoms */
enum { ClkTagBar, ClkLtSymbol, ClkStatusText, ClkWinTitle,
       ClkClientWin, ClkRootWin, ClkLast }; /* clicks */

typedef union {
    int i;
    unsigned int ui;
    float f;
    const void *v;
} Arg;

typedef struct {
    unsigned int click;
    unsigned int mask;
    unsigned int button;
    void (*func)(const Arg *arg);
    const Arg arg;
} Button;

typedef struct Monitor Monitor;
typedef struct Client Client;
struct Client {
    char name[256];
    float mina, maxa;
    int x, y, w, h;
    int oldx, oldy, oldw, oldh;
    int basew, baseh, incw, inch, maxw, maxh, minw, minh;
    int bw, oldbw;
    unsigned int tags;
    int isfixed, isfloating, isurgent, neverfocus, oldstate, isfullscreen;
    Client *next;
    Client *snext;
    Monitor *mon;
    Window win;
};

typedef struct {
    unsigned int mod;
    KeySym keysym;
    void (*func)(const Arg *);
    const Arg arg;
} Key;

typedef struct {
    const char *symbol;
    void (*arrange)(Monitor *);
} Layout;

struct Monitor {
    char ltsymbol[16];
    float mfact;
    int nmaster;
    int num;
    int by;               /* bar geometry */
    int mx, my, mw, mh;   /* screen size */
    int wx, wy, ww, wh;   /* window area  */
    unsigned int seltags;
    unsigned int selected_layout;
    unsigned int tagset[2];
    int showbar;
    int topbar;
    Client *clients;
    Client *selected_client;
    Client *stack;
    Monitor *next;
    Window barwin;
    const Layout *layouts[2];
};

typedef struct {
    const char *class;
    const char *instance;
    const char *title;
    unsigned int tags;
    int isfloating;
    int monitor;
} Rule;

/* function declarations */
static void applyrules(Client *client);
static int applysizehints(Client *client, int *x, int *y, int *w, int *h, int interact);
static void arrange(Monitor *monitor);
static void arrangemon(Monitor *monitor);
static void attach(Client *client);
static void attachstack(Client *client);
static void buttonpress(XEvent *e);
static void checkotherwm(void);
static void cleanup(void);
static void cleanupmon(Monitor *mon);
static void clientmessage(XEvent *e);
static void configure(Client *client);
static void configurenotify(XEvent *e);
static void configurerequest(XEvent *e);
static Monitor *createmon(void);
static void destroynotify(XEvent *e);
static void detach(Client *client);
static void detachstack(Client *client);
static Monitor *dirtomon(int dir);
static void drawbar(Monitor *monitor);
static void drawbars(void);
static void enternotify(XEvent *e);
static void expose(XEvent *e);
static void focus(Client *client);
static void focusin(XEvent *e);
static void focusmon(const Arg *arg);
static void focusstack(const Arg *arg);
static Atom getatomprop(Client *client, Atom prop);
static int getrootptr(int *x, int *y);
static long getstate(Window w);
static int gettextprop(Window w, Atom atom, char *text, unsigned int size);
static void grabbuttons(Client *client, int focused);
static void grabkeys(void);
static void incnmaster(const Arg *arg);
static void keypress(XEvent *e);
static void killclient(const Arg *arg);
static void manage(Window w, XWindowAttributes *wa);
static void mappingnotify(XEvent *e);
static void maprequest(XEvent *e);
static void monocle(Monitor *monitor);
static void motionnotify(XEvent *e);
static void movemouse(const Arg *arg);
static Client *nexttiled(Client *client);
static void pop(Client *);
static void propertynotify(XEvent *e);
static void quit(const Arg *arg);
static Monitor *recttomon(int x, int y, int w, int h);
static void resize(Client *client, int x, int y, int w, int h, int interact);
static void resizeclient(Client *client, int x, int y, int w, int h);
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
static void setmfact(const Arg *arg);
static void setup(void);
static void seturgent(Client *client, int urg);
static void showhide(Client *client);
static void sigchld(int unused);
static void spawn(const Arg *arg);
static void tag(const Arg *arg);
static void tagmon(const Arg *arg);
static void tile(Monitor *);
static void togglebar(const Arg *arg);
static void togglefloating(const Arg *arg);
static void toggletag(const Arg *arg);
static void toggleview(const Arg *arg);
static void unfocus(Client *client, int setfocus);
static void unmanage(Client *client, int destroyed);
static void unmapnotify(XEvent *e);
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
static Client *wintoclient(Window w);
static Monitor *wintomon(Window w);
static int xerror(Display *dpy, XErrorEvent *ee);
static int xerrordummy(Display *dpy, XErrorEvent *ee);
static int xerrorstart(Display *dpy, XErrorEvent *ee);
static void zoom(const Arg *arg);

/* variables */
static const char broken[] = "broken";
static char stext[256];
static int screen;
static int sw, sh;           /* X display screen geometry width, height */
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
static int running = 1;
static Cur *cursor[CurLast];
static Clr **scheme;
static Display *dpy;
static Drw *drw;
static Monitor *all_monitors, *selected_monitor;
static Window root, wmcheckwin;

/* configuration, allows nested code to access above variables */
#include "config.h"

/* compile-time check if all tags fit into an unsigned int bit array. */
struct NumTags { char limitexceeded[LENGTH(tags) > 31 ? -1 : 1]; };

/* function implementations */
void applyrules(Client *client) {
    const char *class, *instance;
    unsigned int i;
    const Rule *r;
    Monitor *monitor;
    XClassHint ch = { NULL, NULL };

    /* rule matching */
    client->isfloating = 0;
    client->tags = 0;
    XGetClassHint(dpy, client->win, &ch);
    class    = ch.res_class ? ch.res_class : broken;
    instance = ch.res_name  ? ch.res_name  : broken;

    for (i = 0; i < LENGTH(rules); i++) {
        r = &rules[i];
        if ((!r->title || strstr(client->name, r->title))
        && (!r->class || strstr(class, r->class))
        && (!r->instance || strstr(instance, r->instance)))
        {
            client->isfloating = r->isfloating;
            client->tags |= r->tags;
            for (monitor = all_monitors; monitor && monitor->num != r->monitor; monitor = monitor->next);
            if (monitor)
                client->mon = monitor;
        }
    }
    if (ch.res_class)
        XFree(ch.res_class);
    if (ch.res_name)
        XFree(ch.res_name);
    client->tags = client->tags & TAGMASK ? client->tags & TAGMASK : client->mon->tagset[client->mon->seltags];
}

int applysizehints(Client *client, int *x, int *y, int *w, int *h, int interact) {
    int baseismin;
    Monitor *monitor = client->mon;

    /* set minimum possible */
    *w = MAX(1, *w);
    *h = MAX(1, *h);
    if (interact) {
        if (*x > sw)
            *x = sw - WIDTH(client);
        if (*y > sh)
            *y = sh - HEIGHT(client);
        if (*x + *w + 2 * client->bw < 0)
            *x = 0;
        if (*y + *h + 2 * client->bw < 0)
            *y = 0;
    } else {
        if (*x >= monitor->wx + monitor->ww)
            *x = monitor->wx + monitor->ww - WIDTH(client);
        if (*y >= monitor->wy + monitor->wh)
            *y = monitor->wy + monitor->wh - HEIGHT(client);
        if (*x + *w + 2 * client->bw <= monitor->wx)
            *x = monitor->wx;
        if (*y + *h + 2 * client->bw <= monitor->wy)
            *y = monitor->wy;
    }
    if (*h < bh)
        *h = bh;
    if (*w < bh)
        *w = bh;
    if (resizehints || client->isfloating || !client->mon->layouts[client->mon->selected_layout]->arrange) {
        /* see last two sentences in ICCCM 4.1.2.3 */
        baseismin = client->basew == client->minw && client->baseh == client->minh;
        if (!baseismin) { /* temporarily remove base dimensions */
            *w -= client->basew;
            *h -= client->baseh;
        }
        /* adjust for aspect limits */
        if (client->mina > 0 && client->maxa > 0) {
            if (client->maxa < (float)*w / *h)
                *w = *h * client->maxa + 0.5;
            else if (client->mina < (float)*h / *w)
                *h = *w * client->mina + 0.5;
        }
        if (baseismin) { /* increment calculation requires this */
            *w -= client->basew;
            *h -= client->baseh;
        }
        /* adjust for increment value */
        if (client->incw)
            *w -= *w % client->incw;
        if (client->inch)
            *h -= *h % client->inch;
        /* restore base dimensions */
        *w = MAX(*w + client->basew, client->minw);
        *h = MAX(*h + client->baseh, client->minh);
        if (client->maxw)
            *w = MIN(*w, client->maxw);
        if (client->maxh)
            *h = MIN(*h, client->maxh);
    }
    return *x != client->x || *y != client->y || *w != client->w || *h != client->h;
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
    strncpy(monitor->ltsymbol, monitor->layouts[monitor->selected_layout]->symbol, sizeof monitor->ltsymbol);
    if (monitor->layouts[monitor->selected_layout]->arrange)
        monitor->layouts[monitor->selected_layout]->arrange(monitor);
}

void attach(Client *client) {
    client->next = client->mon->clients;
    client->mon->clients = client;
}

void attachstack(Client *client) {
    client->snext = client->mon->stack;
    client->mon->stack = client;
}

void buttonpress(XEvent *e) {
    unsigned int i, x, click;
    Arg arg = {0};
    Client *client;
    Monitor *monitor;
    XButtonPressedEvent *ev = &e->xbutton;

    click = ClkRootWin;
    /* focus monitor if necessary */
    if ((monitor = wintomon(ev->window)) && monitor != selected_monitor) {
        unfocus(selected_monitor->selected_client, 1);
        selected_monitor = monitor;
        focus(NULL);
    }
    if (ev->window == selected_monitor->barwin) {
        i = x = 0;
        do
            x += TEXTW(tags[i]);
        while (ev->x >= x && ++i < LENGTH(tags));
        if (i < LENGTH(tags)) {
            click = ClkTagBar;
            arg.ui = 1 << i;
        } else if (ev->x < x + blw)
            click = ClkLtSymbol;
        else if (ev->x > selected_monitor->ww - (int)TEXTW(stext))
            click = ClkStatusText;
        else
            click = ClkWinTitle;
    } else if ((client = wintoclient(ev->window))) {
        focus(client);
        restack(selected_monitor);
        XAllowEvents(dpy, ReplayPointer, CurrentTime);
        click = ClkClientWin;
    }
    for (i = 0; i < LENGTH(buttons); i++)
        if (click == buttons[i].click && buttons[i].func && buttons[i].button == ev->button
        && CLEANMASK(buttons[i].mask) == CLEANMASK(ev->state))
            buttons[i].func(click == ClkTagBar && buttons[i].arg.i == 0 ? &arg : &buttons[i].arg);
}

void checkotherwm(void) {
    xerrorxlib = XSetErrorHandler(xerrorstart);
    /* this causes an error if some other window manager is running */
    XSelectInput(dpy, DefaultRootWindow(dpy), SubstructureRedirectMask);
    XSync(dpy, False);
    XSetErrorHandler(xerror);
    XSync(dpy, False);
}

void cleanup(void) {
    Arg a = {.ui = ~0};
    Layout foo = { "", NULL };
    Monitor *monitor;
    size_t i;

    view(&a);
    selected_monitor->layouts[selected_monitor->selected_layout] = &foo;
    for (monitor = all_monitors; monitor; monitor = monitor->next)
        while (monitor->stack)
            unmanage(monitor->stack, 0);
    XUngrabKey(dpy, AnyKey, AnyModifier, root);
    while (all_monitors)
        cleanupmon(all_monitors);
    for (i = 0; i < CurLast; i++)
        drw_cur_free(drw, cursor[i]);
    for (i = 0; i < LENGTH(colors); i++)
        free(scheme[i]);
    XDestroyWindow(dpy, wmcheckwin);
    drw_free(drw);
    XSync(dpy, False);
    XSetInputFocus(dpy, PointerRoot, RevertToPointerRoot, CurrentTime);
    XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
}

void cleanupmon(Monitor *mon) {
    Monitor *monitor;

    if (mon == all_monitors)
        all_monitors = all_monitors->next;
    else {
        for (monitor = all_monitors; monitor && monitor->next != mon; monitor = monitor->next);
        monitor->next = mon->next;
    }
    XUnmapWindow(dpy, mon->barwin);
    XDestroyWindow(dpy, mon->barwin);
    free(mon);
}

void clientmessage(XEvent *e) {
    XClientMessageEvent *cme = &e->xclient;
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
    XConfigureEvent ce;

    ce.type = ConfigureNotify;
    ce.display = dpy;
    ce.event = client->win;
    ce.window = client->win;
    ce.x = client->x;
    ce.y = client->y;
    ce.width = client->w;
    ce.height = client->h;
    ce.border_width = client->bw;
    ce.above = None;
    ce.override_redirect = False;
    XSendEvent(dpy, client->win, False, StructureNotifyMask, (XEvent *)&ce);
}

void configurenotify(XEvent *e) {
    Monitor *monitor;
    Client *client;
    XConfigureEvent *ev = &e->xconfigure;
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
                        resizeclient(client, monitor->mx, monitor->my, monitor->mw, monitor->mh);
                XMoveResizeWindow(dpy, monitor->barwin, monitor->wx, monitor->by, monitor->ww, bh);
            }
            focus(NULL);
            arrange(NULL);
        }
    }
}

void configurerequest(XEvent *e) {
    Client *client;
    Monitor *monitor;
    XConfigureRequestEvent *ev = &e->xconfigurerequest;
    XWindowChanges wc;

    if ((client = wintoclient(ev->window))) {
        if (ev->value_mask & CWBorderWidth)
            client->bw = ev->border_width;
        else if (client->isfloating || !selected_monitor->layouts[selected_monitor->selected_layout]->arrange) {
            monitor = client->mon;
            if (ev->value_mask & CWX) {
                client->oldx = client->x;
                client->x = monitor->mx + ev->x;
            }
            if (ev->value_mask & CWY) {
                client->oldy = client->y;
                client->y = monitor->my + ev->y;
            }
            if (ev->value_mask & CWWidth) {
                client->oldw = client->w;
                client->w = ev->width;
            }
            if (ev->value_mask & CWHeight) {
                client->oldh = client->h;
                client->h = ev->height;
            }
            if ((client->x + client->w) > monitor->mx + monitor->mw && client->isfloating)
                client->x = monitor->mx + (monitor->mw / 2 - WIDTH(client) / 2); /* center in x direction */
            if ((client->y + client->h) > monitor->my + monitor->mh && client->isfloating)
                client->y = monitor->my + (monitor->mh / 2 - HEIGHT(client) / 2); /* center in y direction */
            if ((ev->value_mask & (CWX|CWY)) && !(ev->value_mask & (CWWidth|CWHeight)))
                configure(client);
            if (ISVISIBLE(client))
                XMoveResizeWindow(dpy, client->win, client->x, client->y, client->w, client->h);
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
        XConfigureWindow(dpy, ev->window, ev->value_mask, &wc);
    }
    XSync(dpy, False);
}

Monitor * createmon(void) {
    Monitor *monitor;

    monitor = ecalloc(1, sizeof(Monitor));
    monitor->tagset[0] = monitor->tagset[1] = 1;
    monitor->mfact = mfact;
    monitor->nmaster = nmaster;
    monitor->showbar = showbar;
    monitor->topbar = topbar;
    monitor->layouts[0] = &layouts[0];
    monitor->layouts[1] = &layouts[1 % LENGTH(layouts)];
    strncpy(monitor->ltsymbol, layouts[0].symbol, sizeof monitor->ltsymbol);
    return monitor;
}

void destroynotify(XEvent *e) {
    Client *client;
    XDestroyWindowEvent *ev = &e->xdestroywindow;

    if ((client = wintoclient(ev->window)))
        unmanage(client, 1);
}

void detach(Client *client) {
    Client **tc;

    for (tc = &client->mon->clients; *tc && *tc != client; tc = &(*tc)->next);
    *tc = client->next;
}

void detachstack(Client *client) {
    Client **tc, *t;

    for (tc = &client->mon->stack; *tc && *tc != client; tc = &(*tc)->snext);
    *tc = client->snext;

    if (client == client->mon->selected_client) {
        for (t = client->mon->stack; t && !ISVISIBLE(t); t = t->snext);
        client->mon->selected_client = t;
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
    int x, w, tw = 0;
    int boxs = drw->fonts->h / 9;
    int boxw = drw->fonts->h / 6 + 2;
    unsigned int i, occ = 0, urg = 0;
    Client *client;

    if (!monitor->showbar)
        return;

    /* draw status first so it can be overdrawn by tags later */
    if (monitor == selected_monitor) { /* status is only drawn on selected monitor */
        drw_setscheme(drw, scheme[SchemeNorm]);
        tw = TEXTW(stext) - lrpad + 2; /* 2px right padding */
        drw_text(drw, monitor->ww - tw, 0, tw, bh, 0, stext, 0);
    }

    for (client = monitor->clients; client; client = client->next) {
        occ |= client->tags;
        if (client->isurgent)
            urg |= client->tags;
    }
    x = 0;
    for (i = 0; i < LENGTH(tags); i++) {
        w = TEXTW(tags[i]);
        drw_setscheme(drw, scheme[monitor->tagset[monitor->seltags] & 1 << i ? SchemeSel : SchemeNorm]);
        drw_text(drw, x, 0, w, bh, lrpad / 2, tags[i], urg & 1 << i);
        if (occ & 1 << i)
            drw_rect(drw, x + boxs, boxs, boxw, boxw,
                monitor == selected_monitor && selected_monitor->selected_client && selected_monitor->selected_client->tags & 1 << i,
                urg & 1 << i);
        x += w;
    }
    w = blw = TEXTW(monitor->ltsymbol);
    drw_setscheme(drw, scheme[SchemeNorm]);
    x = drw_text(drw, x, 0, w, bh, lrpad / 2, monitor->ltsymbol, 0);

    if ((w = monitor->ww - tw - x) > bh) {
        if (monitor->selected_client) {
            drw_setscheme(drw, scheme[monitor == selected_monitor ? SchemeSel : SchemeNorm]);
            drw_text(drw, x, 0, w, bh, lrpad / 2, monitor->selected_client->name, 0);
            if (monitor->selected_client->isfloating)
                drw_rect(drw, x + boxs, boxs, boxw, boxw, monitor->selected_client->isfixed, 0);
        } else {
            drw_setscheme(drw, scheme[SchemeNorm]);
            drw_rect(drw, x, 0, w, bh, 1, 1);
        }
    }
    drw_map(drw, monitor->barwin, 0, 0, monitor->ww, bh);
}

void drawbars(void) {
    for (Monitor* monitor = all_monitors; monitor; monitor = monitor->next) {
        drawbar(monitor);
    }
}

void enternotify(XEvent *e) {
    Client *client;
    Monitor *monitor;
    XCrossingEvent *ev = &e->xcrossing;

    if ((ev->mode != NotifyNormal || ev->detail == NotifyInferior) && ev->window != root)
        return;
    client = wintoclient(ev->window);
    monitor = client ? client->mon : wintomon(ev->window);
    if (monitor != selected_monitor) {
        unfocus(selected_monitor->selected_client, 1);
        selected_monitor = monitor;
    } else if (!client || client == selected_monitor->selected_client)
        return;
    focus(client);
}

void expose(XEvent *e) {
    Monitor *monitor;
    XExposeEvent *ev = &e->xexpose;

    if (ev->count == 0 && (monitor = wintomon(ev->window)))
        drawbar(monitor);
}

void focus(Client *client) {
    if (!client || !ISVISIBLE(client))
        for (client = selected_monitor->stack; client && !ISVISIBLE(client); client = client->snext);
    if (selected_monitor->selected_client && selected_monitor->selected_client != client)
        unfocus(selected_monitor->selected_client, 0);
    if (client) {
        if (client->mon != selected_monitor)
            selected_monitor = client->mon;
        if (client->isurgent)
            seturgent(client, 0);
        detachstack(client);
        attachstack(client);
        grabbuttons(client, 1);
        XSetWindowBorder(dpy, client->win, scheme[SchemeSel][ColBorder].pixel);
        setfocus(client);
    } else {
        XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
        XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
    }
    selected_monitor->selected_client = client;
    drawbars();
}

/* there are some broken focus acquiring clients needing extra handling */
void focusin(XEvent *e) {
    XFocusChangeEvent *ev = &e->xfocus;

    if (selected_monitor->selected_client && ev->window != selected_monitor->selected_client->win)
        setfocus(selected_monitor->selected_client);
}

void focusmon(const Arg *arg) {
    Monitor *monitor;

    if (!all_monitors->next)
        return;
    if ((monitor = dirtomon(arg->i)) == selected_monitor)
        return;
    unfocus(selected_monitor->selected_client, 0);
    selected_monitor = monitor;
    focus(NULL);
}

void focusstack(const Arg *arg) {
    Client *client = NULL, *i;

    if (!selected_monitor->selected_client || (selected_monitor->selected_client->isfullscreen && lockfullscreen))
        return;
    if (arg->i > 0) {
        for (client = selected_monitor->selected_client->next; client && !ISVISIBLE(client); client = client->next);
        if (!client)
            for (client = selected_monitor->clients; client && !ISVISIBLE(client); client = client->next);
    } else {
        for (i = selected_monitor->clients; i != selected_monitor->selected_client; i = i->next)
            if (ISVISIBLE(i))
                client = i;
        if (!client)
            for (; i; i = i->next)
                if (ISVISIBLE(i))
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

    if (XGetWindowProperty(dpy, client->win, prop, 0L, sizeof atom, False, XA_ATOM,
        &da, &di, &dl, &dl, &p) == Success && p) {
        atom = *(Atom *)p;
        XFree(p);
    }
    return atom;
}

int getrootptr(int *x, int *y) {
    int di;
    unsigned int dui;
    Window dummy;

    return XQueryPointer(dpy, root, &dummy, &dummy, x, y, &di, &di, &dui);
}

long getstate(Window w) {
    int format;
    long result = -1;
    unsigned char *p = NULL;
    unsigned long n, extra;
    Atom real;

    if (XGetWindowProperty(dpy, w, wmatom[WMState], 0L, 2L, False, wmatom[WMState],
        &real, &format, &n, &extra, (unsigned char **)&p) != Success)
        return -1;
    if (n != 0)
        result = *p;
    XFree(p);
    return result;
}

int gettextprop(Window w, Atom atom, char *text, unsigned int size) {
    char **list = NULL;
    int n;
    XTextProperty name;

    if (!text || size == 0)
        return 0;
    text[0] = '\0';
    if (!XGetTextProperty(dpy, w, &name, atom) || !name.nitems)
        return 0;
    if (name.encoding == XA_STRING)
        strncpy(text, (char *)name.value, size - 1);
    else {
        if (XmbTextPropertyToTextList(dpy, &name, &list, &n) >= Success && n > 0 && *list) {
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
        XUngrabButton(dpy, AnyButton, AnyModifier, client->win);
        if (!focused)
            XGrabButton(dpy, AnyButton, AnyModifier, client->win, False,
                BUTTONMASK, GrabModeSync, GrabModeSync, None, None);
        for (i = 0; i < LENGTH(buttons); i++)
            if (buttons[i].click == ClkClientWin)
                for (j = 0; j < LENGTH(modifiers); j++)
                    XGrabButton(dpy, buttons[i].button,
                        buttons[i].mask | modifiers[j],
                        client->win, False, BUTTONMASK,
                        GrabModeAsync, GrabModeSync, None, None);
    }
}

void grabkeys(void) {
    updatenumlockmask();
    {
        unsigned int i, j;
        unsigned int modifiers[] = { 0, LockMask, numlockmask, numlockmask|LockMask };

        XUngrabKey(dpy, AnyKey, AnyModifier, root);
        for (i = 0; i < LENGTH(keys); i++) {
            KeyCode code = XKeysymToKeycode(dpy, keys[i].keysym);
            if (code != 0) {
                for (j = 0; j < LENGTH(modifiers); j++) {
                    XGrabKey(dpy, code, keys[i].mod | modifiers[j], root, True, GrabModeAsync, GrabModeAsync);
                }
            }
        }
    }
}

void incnmaster(const Arg *arg) {
    selected_monitor->nmaster = MAX(selected_monitor->nmaster + arg->i, 0);
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

void keypress(XEvent *e) {
    XKeyEvent *ev = &e->xkey;
    KeySym keysym = XKeycodeToKeysym(dpy, (KeyCode)ev->keycode, 0);

    unsigned int i = 0;
    for(; i < LENGTH(keys); i++) {
        if (keysym == keys[i].keysym && CLEANMASK(keys[i].mod) == CLEANMASK(ev->state) && keys[i].func) {
            keys[i].func(&(keys[i].arg));
            break;
        }
    }

    if(i >= LENGTH(keys)) {
        // TODO: check to see if a second process (the custom keys process) and its named pipe exist
        // If it does, then open the named pipe and send the bytes to it. If it doesn't then start it
        // and send the bytes over to that process.
    }
}

void killclient(const Arg *arg) {
    if (!selected_monitor->selected_client)
        return;
    if (!sendevent(selected_monitor->selected_client, wmatom[WMDelete])) {
        XGrabServer(dpy);
        XSetErrorHandler(xerrordummy);
        XSetCloseDownMode(dpy, DestroyAll);
        XKillClient(dpy, selected_monitor->selected_client->win);
        XSync(dpy, False);
        XSetErrorHandler(xerror);
        XUngrabServer(dpy);
    }
}

void manage(Window w, XWindowAttributes *wa) {
    Client *client, *t = NULL;
    Window trans = None;
    XWindowChanges wc;

    client = ecalloc(1, sizeof(Client));
    client->win = w;
    /* geometry */
    client->x = client->oldx = wa->x;
    client->y = client->oldy = wa->y;
    client->w = client->oldw = wa->width;
    client->h = client->oldh = wa->height;
    client->oldbw = wa->border_width;

    updatetitle(client);
    if (XGetTransientForHint(dpy, w, &trans) && (t = wintoclient(trans))) {
        client->mon = t->mon;
        client->tags = t->tags;
    } else {
        client->mon = selected_monitor;
        applyrules(client);
    }

    if (client->x + WIDTH(client) > client->mon->mx + client->mon->mw)
        client->x = client->mon->mx + client->mon->mw - WIDTH(client);
    if (client->y + HEIGHT(client) > client->mon->my + client->mon->mh)
        client->y = client->mon->my + client->mon->mh - HEIGHT(client);
    client->x = MAX(client->x, client->mon->mx);
    /* only fix client y-offset, if the client center might cover the bar */
    client->y = MAX(client->y, ((client->mon->by == client->mon->my) && (client->x + (client->w / 2) >= client->mon->wx)
        && (client->x + (client->w / 2) < client->mon->wx + client->mon->ww)) ? bh : client->mon->my);
    client->bw = borderpx;

    wc.border_width = client->bw;
    XConfigureWindow(dpy, w, CWBorderWidth, &wc);
    XSetWindowBorder(dpy, w, scheme[SchemeNorm][ColBorder].pixel);
    configure(client); /* propagates border_width, if size doesn't change */
    updatewindowtype(client);
    updatesizehints(client);
    updatewmhints(client);
    XSelectInput(dpy, w, EnterWindowMask|FocusChangeMask|PropertyChangeMask|StructureNotifyMask);
    grabbuttons(client, 0);
    if (!client->isfloating)
        client->isfloating = client->oldstate = trans != None || client->isfixed;
    if (client->isfloating)
        XRaiseWindow(dpy, client->win);
    attach(client);
    attachstack(client);
    XChangeProperty(dpy, root, netatom[NetClientList], XA_WINDOW, 32, PropModeAppend,
        (unsigned char *) &(client->win), 1);
    XMoveResizeWindow(dpy, client->win, client->x + 2 * sw, client->y, client->w, client->h); /* some windows require this */
    setclientstate(client, NormalState);
    if (client->mon == selected_monitor)
        unfocus(selected_monitor->selected_client, 0);
    client->mon->selected_client = client;
    arrange(client->mon);
    XMapWindow(dpy, client->win);
    focus(NULL);
}

void mappingnotify(XEvent *e) {
    XMappingEvent *ev = &e->xmapping;

    XRefreshKeyboardMapping(ev);
    if (ev->request == MappingKeyboard)
        grabkeys();
}

void maprequest(XEvent *e) {
    static XWindowAttributes wa;
    XMapRequestEvent *ev = &e->xmaprequest;

    if (!XGetWindowAttributes(dpy, ev->window, &wa))
        return;
    if (wa.override_redirect)
        return;
    if (!wintoclient(ev->window))
        manage(ev->window, &wa);
}

void monocle(Monitor *monitor) {
    unsigned int n = 0;
    Client *client;

    for (client = monitor->clients; client; client = client->next)
        if (ISVISIBLE(client))
            n++;
    if (n > 0) /* override layout symbol */
        snprintf(monitor->ltsymbol, sizeof monitor->ltsymbol, "[%d]", n);
    for (client = nexttiled(monitor->clients); client; client = nexttiled(client->next))
        resize(client, monitor->wx, monitor->wy, monitor->ww - 2 * client->bw, monitor->wh - 2 * client->bw, 0);
}

void motionnotify(XEvent *e) {
    static Monitor *mon = NULL;
    XMotionEvent *ev = &e->xmotion;

    if (ev->window != root)
        return;

    Monitor *monitor = recttomon(ev->x_root, ev->y_root, 1, 1);
    if (monitor != mon && mon) {
        unfocus(selected_monitor->selected_client, 1);
        selected_monitor = monitor;
        focus(NULL);
    }
    mon = monitor;
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
    if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
        None, cursor[CurMove]->cursor, CurrentTime) != GrabSuccess)
        return;
    if (!getrootptr(&x, &y))
        return;
    do {
        XMaskEvent(dpy, MOUSEMASK|ExposureMask|SubstructureRedirectMask, &ev);
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
            if (abs(selected_monitor->wx - nx) < snap)
                nx = selected_monitor->wx;
            else if (abs((selected_monitor->wx + selected_monitor->ww) - (nx + WIDTH(client))) < snap)
                nx = selected_monitor->wx + selected_monitor->ww - WIDTH(client);
            if (abs(selected_monitor->wy - ny) < snap)
                ny = selected_monitor->wy;
            else if (abs((selected_monitor->wy + selected_monitor->wh) - (ny + HEIGHT(client))) < snap)
                ny = selected_monitor->wy + selected_monitor->wh - HEIGHT(client);
            if (!client->isfloating && selected_monitor->layouts[selected_monitor->selected_layout]->arrange
            && (abs(nx - client->x) > snap || abs(ny - client->y) > snap))
                togglefloating(NULL);
            if (!selected_monitor->layouts[selected_monitor->selected_layout]->arrange || client->isfloating)
                resize(client, nx, ny, client->w, client->h, 1);
            break;
        }
    } while (ev.type != ButtonRelease);
    XUngrabPointer(dpy, CurrentTime);
    if ((monitor = recttomon(client->x, client->y, client->w, client->h)) != selected_monitor) {
        sendmon(client, monitor);
        selected_monitor = monitor;
        focus(NULL);
    }
}

Client* nexttiled(Client *client) {
    for (; client && (client->isfloating || !ISVISIBLE(client)); client = client->next);
    return client;
}

void pop(Client *client) {
    detach(client);
    attach(client);
    focus(client);
    arrange(client->mon);
}

void propertynotify(XEvent *e) {
    Client *client;
    Window trans;
    XPropertyEvent *ev = &e->xproperty;

    if ((ev->window == root) && (ev->atom == XA_WM_NAME))
        updatestatus();
    else if (ev->state == PropertyDelete)
        return; /* ignore */
    else if ((client = wintoclient(ev->window))) {
        switch(ev->atom) {
        default: break;
        case XA_WM_TRANSIENT_FOR:
            if (!client->isfloating && (XGetTransientForHint(dpy, client->win, &trans)) &&
                (client->isfloating = (wintoclient(trans)) != NULL))
                arrange(client->mon);
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
            if (client == client->mon->selected_client)
                drawbar(client->mon);
        }
        if (ev->atom == netatom[NetWMWindowType])
            updatewindowtype(client);
    }
}

void quit(const Arg *arg) {
    running = 0;
}

Monitor* recttomon(int x, int y, int w, int h) {
    Monitor *monitor, *r = selected_monitor;
    int a, area = 0;

    for (monitor = all_monitors; monitor; monitor = monitor->next)
        if ((a = INTERSECT(x, y, w, h, monitor)) > area) {
            area = a;
            r = monitor;
        }
    return r;
}

void resize(Client *client, int x, int y, int w, int h, int interact) {
    if (applysizehints(client, &x, &y, &w, &h, interact))
        resizeclient(client, x, y, w, h);
}

void resizeclient(Client *client, int x, int y, int w, int h) {
    XWindowChanges wc;

    client->oldx = client->x; client->x = wc.x = x;
    client->oldy = client->y; client->y = wc.y = y;
    client->oldw = client->w; client->w = wc.width = w;
    client->oldh = client->h; client->h = wc.height = h;
    wc.border_width = client->bw;
    XConfigureWindow(dpy, client->win, CWX|CWY|CWWidth|CWHeight|CWBorderWidth, &wc);
    configure(client);
    XSync(dpy, False);
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
    if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
        None, cursor[CurResize]->cursor, CurrentTime) != GrabSuccess)
        return;
    XWarpPointer(dpy, None, client->win, 0, 0, 0, 0, client->w + client->bw - 1, client->h + client->bw - 1);
    do {
        XMaskEvent(dpy, MOUSEMASK|ExposureMask|SubstructureRedirectMask, &ev);
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

            nw = MAX(ev.xmotion.x - ocx - 2 * client->bw + 1, 1);
            nh = MAX(ev.xmotion.y - ocy - 2 * client->bw + 1, 1);
            if (client->mon->wx + nw >= selected_monitor->wx && client->mon->wx + nw <= selected_monitor->wx + selected_monitor->ww
            && client->mon->wy + nh >= selected_monitor->wy && client->mon->wy + nh <= selected_monitor->wy + selected_monitor->wh)
            {
                if (!client->isfloating && selected_monitor->layouts[selected_monitor->selected_layout]->arrange
                && (abs(nw - client->w) > snap || abs(nh - client->h) > snap))
                    togglefloating(NULL);
            }
            if (!selected_monitor->layouts[selected_monitor->selected_layout]->arrange || client->isfloating)
                resize(client, client->x, client->y, nw, nh, 1);
            break;
        }
    } while (ev.type != ButtonRelease);
    XWarpPointer(dpy, None, client->win, 0, 0, 0, 0, client->w + client->bw - 1, client->h + client->bw - 1);
    XUngrabPointer(dpy, CurrentTime);
    while (XCheckMaskEvent(dpy, EnterWindowMask, &ev));
    if ((monitor = recttomon(client->x, client->y, client->w, client->h)) != selected_monitor) {
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
    if (monitor->selected_client->isfloating || !monitor->layouts[monitor->selected_layout]->arrange)
        XRaiseWindow(dpy, monitor->selected_client->win);
    if (monitor->layouts[monitor->selected_layout]->arrange) {
        wc.stack_mode = Below;
        wc.sibling = monitor->barwin;
        for (client = monitor->stack; client; client = client->snext)
            if (!client->isfloating && ISVISIBLE(client)) {
                XConfigureWindow(dpy, client->win, CWSibling|CWStackMode, &wc);
                wc.sibling = client->win;
            }
    }
    XSync(dpy, False);
    while (XCheckMaskEvent(dpy, EnterWindowMask, &ev));
}

void run(void) {
    XEvent ev;
    /* main event loop */
    XSync(dpy, False);
    while (running && !XNextEvent(dpy, &ev))
        if (handler[ev.type])
            handler[ev.type](&ev); /* call handler */
}

void scan(void) {
    unsigned int i, num;
    Window d1, d2, *wins = NULL;
    XWindowAttributes wa;

    if (XQueryTree(dpy, root, &d1, &d2, &wins, &num)) {
        for (i = 0; i < num; i++) {
            if (!XGetWindowAttributes(dpy, wins[i], &wa)
            || wa.override_redirect || XGetTransientForHint(dpy, wins[i], &d1))
                continue;
            if (wa.map_state == IsViewable || getstate(wins[i]) == IconicState)
                manage(wins[i], &wa);
        }
        for (i = 0; i < num; i++) { /* now the transients */
            if (!XGetWindowAttributes(dpy, wins[i], &wa))
                continue;
            if (XGetTransientForHint(dpy, wins[i], &d1)
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
    if (client->mon == monitor)
        return;
    unfocus(client, 1);
    detach(client);
    detachstack(client);
    client->mon = monitor;
    client->tags = monitor->tagset[monitor->seltags]; /* assign tags of target monitor */
    attach(client);
    attachstack(client);
    focus(NULL);
    arrange(NULL);
}

void
setclientstate(Client *client, long state)
{
    long data[] = { state, None };

    XChangeProperty(dpy, client->win, wmatom[WMState], wmatom[WMState], 32,
        PropModeReplace, (unsigned char *)data, 2);
}

int
sendevent(Client *client, Atom proto)
{
    int n;
    Atom *protocols;
    int exists = 0;
    XEvent ev;

    if (XGetWMProtocols(dpy, client->win, &protocols, &n)) {
        while (!exists && n--)
            exists = protocols[n] == proto;
        XFree(protocols);
    }
    if (exists) {
        ev.type = ClientMessage;
        ev.xclient.window = client->win;
        ev.xclient.message_type = wmatom[WMProtocols];
        ev.xclient.format = 32;
        ev.xclient.data.l[0] = proto;
        ev.xclient.data.l[1] = CurrentTime;
        XSendEvent(dpy, client->win, False, NoEventMask, &ev);
    }
    return exists;
}

void
setfocus(Client *client)
{
    if (!client->neverfocus) {
        XSetInputFocus(dpy, client->win, RevertToPointerRoot, CurrentTime);
        XChangeProperty(dpy, root, netatom[NetActiveWindow],
            XA_WINDOW, 32, PropModeReplace,
            (unsigned char *) &(client->win), 1);
    }
    sendevent(client, wmatom[WMTakeFocus]);
}

void
setfullscreen(Client *client, int fullscreen)
{
    if (fullscreen && !client->isfullscreen) {
        XChangeProperty(dpy, client->win, netatom[NetWMState], XA_ATOM, 32,
            PropModeReplace, (unsigned char*)&netatom[NetWMFullscreen], 1);
        client->isfullscreen = 1;
        client->oldstate = client->isfloating;
        client->oldbw = client->bw;
        client->bw = 0;
        client->isfloating = 1;
        resizeclient(client, client->mon->mx, client->mon->my, client->mon->mw, client->mon->mh);
        XRaiseWindow(dpy, client->win);
    } else if (!fullscreen && client->isfullscreen){
        XChangeProperty(dpy, client->win, netatom[NetWMState], XA_ATOM, 32,
            PropModeReplace, (unsigned char*)0, 0);
        client->isfullscreen = 0;
        client->isfloating = client->oldstate;
        client->bw = client->oldbw;
        client->x = client->oldx;
        client->y = client->oldy;
        client->w = client->oldw;
        client->h = client->oldh;
        resizeclient(client, client->x, client->y, client->w, client->h);
        arrange(client->mon);
    }
}

void setlayout(const Arg *arg) {
    if (!arg || !arg->v || arg->v != selected_monitor->layouts[selected_monitor->selected_layout]) {
        selected_monitor->selected_layout ^= 1;
    }

    if (arg && arg->v) {
        selected_monitor->layouts[selected_monitor->selected_layout] = (Layout *)arg->v;
    }

    strncpy(selected_monitor->ltsymbol, selected_monitor->layouts[selected_monitor->selected_layout]->symbol, sizeof(selected_monitor->ltsymbol));
    if (selected_monitor->selected_client) {
        arrange(selected_monitor);
    } else {
        drawbar(selected_monitor);
    }
}

/* arg > 1.0 will set mfact absolutely */
void setmfact(const Arg *arg) {
    float f;

    if (!arg || !selected_monitor->layouts[selected_monitor->selected_layout]->arrange)
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
    screen = DefaultScreen(dpy);
    sw = DisplayWidth(dpy, screen);
    sh = DisplayHeight(dpy, screen);
    root = RootWindow(dpy, screen);
    drw = drw_create(dpy, screen, root, sw, sh);
    if (!drw_fontset_create(drw, fonts, LENGTH(fonts)))
        die("no fonts could be loaded.");
    lrpad = drw->fonts->h;
    bh = drw->fonts->h + 2;
    updategeom();
    /* init atoms */
    utf8string = XInternAtom(dpy, "UTF8_STRING", False);
    wmatom[WMProtocols] = XInternAtom(dpy, "WM_PROTOCOLS", False);
    wmatom[WMDelete] = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    wmatom[WMState] = XInternAtom(dpy, "WM_STATE", False);
    wmatom[WMTakeFocus] = XInternAtom(dpy, "WM_TAKE_FOCUS", False);
    netatom[NetActiveWindow] = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
    netatom[NetSupported] = XInternAtom(dpy, "_NET_SUPPORTED", False);
    netatom[NetWMName] = XInternAtom(dpy, "_NET_WM_NAME", False);
    netatom[NetWMState] = XInternAtom(dpy, "_NET_WM_STATE", False);
    netatom[NetWMCheck] = XInternAtom(dpy, "_NET_SUPPORTING_WM_CHECK", False);
    netatom[NetWMFullscreen] = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
    netatom[NetWMWindowType] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
    netatom[NetWMWindowTypeDialog] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
    netatom[NetClientList] = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
    /* init cursors */
    cursor[CurNormal] = drw_cur_create(drw, XC_left_ptr);
    cursor[CurResize] = drw_cur_create(drw, XC_sizing);
    cursor[CurMove] = drw_cur_create(drw, XC_fleur);
    /* init appearance */
    scheme = ecalloc(LENGTH(colors), sizeof(Clr *));
    for (i = 0; i < LENGTH(colors); i++)
        scheme[i] = drw_scm_create(drw, colors[i], 3);
    /* init bars */
    updatebars();
    updatestatus();
    /* supporting window for NetWMCheck */
    wmcheckwin = XCreateSimpleWindow(dpy, root, 0, 0, 1, 1, 0, 0, 0);
    XChangeProperty(dpy, wmcheckwin, netatom[NetWMCheck], XA_WINDOW, 32,
        PropModeReplace, (unsigned char *) &wmcheckwin, 1);
    XChangeProperty(dpy, wmcheckwin, netatom[NetWMName], utf8string, 8,
        PropModeReplace, (unsigned char *) "dwm", 3);
    XChangeProperty(dpy, root, netatom[NetWMCheck], XA_WINDOW, 32,
        PropModeReplace, (unsigned char *) &wmcheckwin, 1);
    /* EWMH support per view */
    XChangeProperty(dpy, root, netatom[NetSupported], XA_ATOM, 32,
        PropModeReplace, (unsigned char *) netatom, NetLast);
    XDeleteProperty(dpy, root, netatom[NetClientList]);
    /* select events */
    wa.cursor = cursor[CurNormal]->cursor;
    wa.event_mask = SubstructureRedirectMask|SubstructureNotifyMask
        |ButtonPressMask|PointerMotionMask|EnterWindowMask
        |LeaveWindowMask|StructureNotifyMask|PropertyChangeMask;
    XChangeWindowAttributes(dpy, root, CWEventMask|CWCursor, &wa);
    XSelectInput(dpy, root, wa.event_mask);
    grabkeys();
    focus(NULL);
}


void
seturgent(Client *client, int urg)
{
    XWMHints *wmh;

    client->isurgent = urg;
    if (!(wmh = XGetWMHints(dpy, client->win)))
        return;
    wmh->flags = urg ? (wmh->flags | XUrgencyHint) : (wmh->flags & ~XUrgencyHint);
    XSetWMHints(dpy, client->win, wmh);
    XFree(wmh);
}

void showhide(Client *client) {
    if (!client)
        return;
    if (ISVISIBLE(client)) {
        /* show clients top down */
        XMoveWindow(dpy, client->win, client->x, client->y);
        if ((!client->mon->layouts[client->mon->selected_layout]->arrange || client->isfloating) && !client->isfullscreen)
            resize(client, client->x, client->y, client->w, client->h, 0);
        showhide(client->snext);
    } else {
        /* hide clients bottom up */
        showhide(client->snext);
        XMoveWindow(dpy, client->win, WIDTH(client) * -2, client->y);
    }
}

void sigchld(int unused) {
    // TODO: Handle dead app launcher or status bar
    if (signal(SIGCHLD, sigchld) == SIG_ERR)
        die("can't install SIGCHLD handler:");
    while (0 < waitpid(-1, NULL, WNOHANG));
}

void spawn(const Arg *arg) {
    if (arg->v == dmenucmd)
        dmenumon[0] = '0' + selected_monitor->num;
    if (fork() == 0) {
        if (dpy)
            close(ConnectionNumber(dpy));
        setsid();
        execvp(((char **)arg->v)[0], (char **)arg->v);
        fprintf(stderr, "dwm: execvp %s", ((char **)arg->v)[0]);
        perror(" failed");
        exit(EXIT_SUCCESS);
    }
}

void tag(const Arg *arg) {
    if (selected_monitor->selected_client && arg->ui & TAGMASK) {
        selected_monitor->selected_client->tags = arg->ui & TAGMASK;
        focus(NULL);
        arrange(selected_monitor);
    }
}

void
tagmon(const Arg *arg)
{
    if (!selected_monitor->selected_client || !all_monitors->next)
        return;
    sendmon(selected_monitor->selected_client, dirtomon(arg->i));
}

void
tile(Monitor *monitor)
{
    unsigned int i, n, h, mw, my, ty;
    Client *client;

    for (n = 0, client = nexttiled(monitor->clients); client; client = nexttiled(client->next), n++);
    if (n == 0)
        return;

    if (n > monitor->nmaster)
        mw = monitor->nmaster ? monitor->ww * monitor->mfact : 0;
    else
        mw = monitor->ww;
    for (i = my = ty = 0, client = nexttiled(monitor->clients); client; client = nexttiled(client->next), i++)
        if (i < monitor->nmaster) {
            h = (monitor->wh - my) / (MIN(n, monitor->nmaster) - i);
            resize(client, monitor->wx, monitor->wy + my, mw - (2*client->bw), h - (2*client->bw), 0);
            if (my + HEIGHT(client) < monitor->wh)
                my += HEIGHT(client);
        } else {
            h = (monitor->wh - ty) / (n - i);
            resize(client, monitor->wx + mw, monitor->wy + ty, monitor->ww - mw - (2*client->bw), h - (2*client->bw), 0);
            if (ty + HEIGHT(client) < monitor->wh)
                ty += HEIGHT(client);
        }
}

void togglebar(const Arg *arg) {
    selected_monitor->showbar = !selected_monitor->showbar;
    updatebarpos(selected_monitor);
    XMoveResizeWindow(dpy, selected_monitor->barwin, selected_monitor->wx, selected_monitor->by, selected_monitor->ww, bh);
    arrange(selected_monitor);
}

void
togglefloating(const Arg *arg)
{
    if (!selected_monitor->selected_client)
        return;
    if (selected_monitor->selected_client->isfullscreen) /* no support for fullscreen windows */
        return;
    selected_monitor->selected_client->isfloating = !selected_monitor->selected_client->isfloating || selected_monitor->selected_client->isfixed;
    if (selected_monitor->selected_client->isfloating)
        resize(selected_monitor->selected_client, selected_monitor->selected_client->x, selected_monitor->selected_client->y,
            selected_monitor->selected_client->w, selected_monitor->selected_client->h, 0);
    arrange(selected_monitor);
}

void
toggletag(const Arg *arg)
{
    unsigned int newtags;

    if (!selected_monitor->selected_client)
        return;
    newtags = selected_monitor->selected_client->tags ^ (arg->ui & TAGMASK);
    if (newtags) {
        selected_monitor->selected_client->tags = newtags;
        focus(NULL);
        arrange(selected_monitor);
    }
}

void
toggleview(const Arg *arg)
{
    unsigned int newtagset = selected_monitor->tagset[selected_monitor->seltags] ^ (arg->ui & TAGMASK);

    if (newtagset) {
        selected_monitor->tagset[selected_monitor->seltags] = newtagset;
        focus(NULL);
        arrange(selected_monitor);
    }
}

void
unfocus(Client *client, int setfocus)
{
    if (!client)
        return;
    grabbuttons(client, 0);
    XSetWindowBorder(dpy, client->win, scheme[SchemeNorm][ColBorder].pixel);
    if (setfocus) {
        XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
        XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
    }
}

void
unmanage(Client *client, int destroyed)
{
    Monitor *monitor = client->mon;
    XWindowChanges wc;

    detach(client);
    detachstack(client);
    if (!destroyed) {
        wc.border_width = client->oldbw;
        XGrabServer(dpy); /* avoid race conditions */
        XSetErrorHandler(xerrordummy);
        XConfigureWindow(dpy, client->win, CWBorderWidth, &wc); /* restore border */
        XUngrabButton(dpy, AnyButton, AnyModifier, client->win);
        setclientstate(client, WithdrawnState);
        XSync(dpy, False);
        XSetErrorHandler(xerror);
        XUngrabServer(dpy);
    }
    free(client);
    focus(NULL);
    updateclientlist();
    arrange(monitor);
}

void
unmapnotify(XEvent *e)
{
    Client *client;
    XUnmapEvent *ev = &e->xunmap;

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
        monitor->barwin = XCreateWindow(dpy, root, monitor->wx, monitor->by, monitor->ww, bh, 0, DefaultDepth(dpy, screen),
                CopyFromParent, DefaultVisual(dpy, screen),
                CWOverrideRedirect|CWBackPixmap|CWEventMask, &wa);
        XDefineCursor(dpy, monitor->barwin, cursor[CurNormal]->cursor);
        XMapRaised(dpy, monitor->barwin);
        XSetClassHint(dpy, monitor->barwin, &ch);
    }
}

void
updatebarpos(Monitor *monitor)
{
    monitor->wy = monitor->my;
    monitor->wh = monitor->mh;
    if (monitor->showbar) {
        monitor->wh -= bh;
        monitor->by = monitor->topbar ? monitor->wy : monitor->wy + monitor->wh;
        monitor->wy = monitor->topbar ? monitor->wy + bh : monitor->wy;
    } else
        monitor->by = -bh;
}

void
updateclientlist()
{
    Client *client;
    Monitor *monitor;

    XDeleteProperty(dpy, root, netatom[NetClientList]);
    for (monitor = all_monitors; monitor; monitor = monitor->next)
        for (client = monitor->clients; client; client = client->next)
            XChangeProperty(dpy, root, netatom[NetClientList],
                XA_WINDOW, 32, PropModeAppend,
                (unsigned char *) &(client->win), 1);
}

int
updategeom(void)
{
    int dirty = 0;

#ifdef XINERAMA
    if (XineramaIsActive(dpy)) {
        int i, j, n, nn;
        Client *client;
        Monitor *monitor;
        XineramaScreenInfo *info = XineramaQueryScreens(dpy, &nn);
        XineramaScreenInfo *unique = NULL;

        for (n = 0, monitor = all_monitors; monitor; monitor = monitor->next, n++);
        /* only consider unique geometries as separate screens */
        unique = ecalloc(nn, sizeof(XineramaScreenInfo));
        for (i = 0, j = 0; i < nn; i++)
            if (isuniquegeom(unique, j, &info[i]))
                memcpy(&unique[j++], &info[i], sizeof(XineramaScreenInfo));
        XFree(info);
        nn = j;
        if (n <= nn) { /* new monitors available */
            for (i = 0; i < (nn - n); i++) {
                for (monitor = all_monitors; monitor && monitor->next; monitor = monitor->next);
                if (monitor)
                    monitor->next = createmon();
                else
                    all_monitors = createmon();
            }
            for (i = 0, monitor = all_monitors; i < nn && monitor; monitor = monitor->next, i++)
                if (i >= n
                || unique[i].x_org != monitor->mx || unique[i].y_org != monitor->my
                || unique[i].width != monitor->mw || unique[i].height != monitor->mh)
                {
                    dirty = 1;
                    monitor->num = i;
                    monitor->mx = monitor->wx = unique[i].x_org;
                    monitor->my = monitor->wy = unique[i].y_org;
                    monitor->mw = monitor->ww = unique[i].width;
                    monitor->mh = monitor->wh = unique[i].height;
                    updatebarpos(monitor);
                }
        } else { /* less monitors available nn < n */
            for (i = nn; i < n; i++) {
                for (monitor = all_monitors; monitor && monitor->next; monitor = monitor->next);
                while ((client = monitor->clients)) {
                    dirty = 1;
                    monitor->clients = client->next;
                    detachstack(client);
                    client->mon = all_monitors;
                    attach(client);
                    attachstack(client);
                }
                if (monitor == selected_monitor)
                    selected_monitor = all_monitors;
                cleanupmon(monitor);
            }
        }
        free(unique);
    } else
#endif /* XINERAMA */
    { /* default monitor setup */
        if (!all_monitors)
            all_monitors = createmon();
        if (all_monitors->mw != sw || all_monitors->mh != sh) {
            dirty = 1;
            all_monitors->mw = all_monitors->ww = sw;
            all_monitors->mh = all_monitors->wh = sh;
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
    modmap = XGetModifierMapping(dpy);
    for (i = 0; i < 8; i++)
        for (j = 0; j < modmap->max_keypermod; j++)
            if (modmap->modifiermap[i * modmap->max_keypermod + j]
                == XKeysymToKeycode(dpy, XK_Num_Lock))
                numlockmask = (1 << i);
    XFreeModifiermap(modmap);
}

void
updatesizehints(Client *client)
{
    long msize;
    XSizeHints size;

    if (!XGetWMNormalHints(dpy, client->win, &size, &msize))
        /* size is uninitialized, ensure that size.flags aren't used */
        size.flags = PSize;
    if (size.flags & PBaseSize) {
        client->basew = size.base_width;
        client->baseh = size.base_height;
    } else if (size.flags & PMinSize) {
        client->basew = size.min_width;
        client->baseh = size.min_height;
    } else
        client->basew = client->baseh = 0;
    if (size.flags & PResizeInc) {
        client->incw = size.width_inc;
        client->inch = size.height_inc;
    } else
        client->incw = client->inch = 0;
    if (size.flags & PMaxSize) {
        client->maxw = size.max_width;
        client->maxh = size.max_height;
    } else
        client->maxw = client->maxh = 0;
    if (size.flags & PMinSize) {
        client->minw = size.min_width;
        client->minh = size.min_height;
    } else if (size.flags & PBaseSize) {
        client->minw = size.base_width;
        client->minh = size.base_height;
    } else
        client->minw = client->minh = 0;
    if (size.flags & PAspect) {
        client->mina = (float)size.min_aspect.y / size.min_aspect.x;
        client->maxa = (float)size.max_aspect.x / size.max_aspect.y;
    } else
        client->maxa = client->mina = 0.0;
    client->isfixed = (client->maxw && client->maxh && client->maxw == client->minw && client->maxh == client->minh);
}

void updatestatus(void) {
    if (!gettextprop(root, XA_WM_NAME, stext, sizeof(stext)))
        strcpy(stext, "dwm-"VERSION);
    drawbar(selected_monitor);
}

void
updatetitle(Client *client)
{
    if (!gettextprop(client->win, netatom[NetWMName], client->name, sizeof client->name))
        gettextprop(client->win, XA_WM_NAME, client->name, sizeof client->name);
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

void
updatewmhints(Client *client)
{
    XWMHints *wmh;

    if((wmh = XGetWMHints(dpy, client->win)) != NULL) {
        if(client == selected_monitor->selected_client && wmh->flags & XUrgencyHint) {
            wmh->flags &= ~XUrgencyHint;
            XSetWMHints(dpy, client->win, wmh);
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

void
view(const Arg *arg)
{
    if ((arg->ui & TAGMASK) == selected_monitor->tagset[selected_monitor->seltags])
        return;
    selected_monitor->seltags ^= 1; /* toggle sel tagset */
    if (arg->ui & TAGMASK)
        selected_monitor->tagset[selected_monitor->seltags] = arg->ui & TAGMASK;
    focus(NULL);
    arrange(selected_monitor);
}

Client *
wintoclient(Window w)
{
    Client *client;
    Monitor *monitor;

    for (monitor = all_monitors; monitor; monitor = monitor->next)
        for (client = monitor->clients; client; client = client->next)
            if (client->win == w)
                return client;
    return NULL;
}

Monitor *
wintomon(Window w)
{
    int x, y;
    Client *client;
    Monitor *monitor;

    if (w == root && getrootptr(&x, &y))
        return recttomon(x, y, 1, 1);
    for (monitor = all_monitors; monitor; monitor = monitor->next)
        if (w == monitor->barwin)
            return monitor;
    if ((client = wintoclient(w)))
        return client->mon;
    return selected_monitor;
}

/* There's no way to check accesses to destroyed windows, thus those cases are
 * ignored (especially on UnmapNotify's). Other types of errors call Xlibs
 * default error handler, which may call exit. */
int
xerror(Display *dpy, XErrorEvent *ee)
{
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
    return xerrorxlib(dpy, ee); /* may call exit */
}

int xerrordummy(Display *dpy, XErrorEvent *ee) {
    return 0;
}

/* Startup Error handler to check if another window manager is already running. */
int xerrorstart(Display *dpy, XErrorEvent *ee) {
    die("dwm: another window manager is already running");
    return -1;
}

void zoom(const Arg *arg) {
    Client *client = selected_monitor->selected_client;

    if(!selected_monitor->layouts[selected_monitor->selected_layout]->arrange
        || (selected_monitor->selected_client && selected_monitor->selected_client->isfloating))
        return;
    if(client == nexttiled(selected_monitor->clients))
        if(!client || !(client = nexttiled(client->next)))
            return;
    pop(client);
}

int main(int argc, char *argv[]) {
    if (argc == 2 && !strcmp("-v", argv[1]))
        die("dwm-"VERSION);
    else if (argc != 1)
        die("usage: dwm [-v]");
    if (!setlocale(LC_CTYPE, "") || !XSupportsLocale())
        fputs("warning: no locale support\n", stderr);
    if (!(dpy = XOpenDisplay(NULL)))
        die("dwm: cannot open display");
    checkotherwm();
    setup();
#ifdef __OpenBSD__
    if (pledge("stdio rpath proc exec", NULL) == -1)
        die("pledge");
#endif /* __OpenBSD__ */
    scan();
    run();
    cleanup();
    XCloseDisplay(dpy);
    return EXIT_SUCCESS;
}

