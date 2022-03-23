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
 *  - FIX: after making the gap size 0, when you try to make it smaller, only some windows resize
 *  - Create a free-list allocator that uses a tagged structure for buckets. The tag will help in iterating over
 *    valid and invalid entries in the list. Hold onto the struct size in the free-list to make it "generic".
 *    This allocator should be used for clients, monitors and anything else that needs to be allocated and freed.
 *
 *  - Change client list from linked-list to array
 *    - Instead of holding raw pointers to clients, use indices into the array (selected_client and variables like it become integers)
 *    - Client list in monitor should be 2 lists, one for tiled, one for floating, that way nexttiled basically disappears (or becomes next_visible)
 *    - Consider how to sort clients that appear in multiple tags or when viewing multiple tags. Linked lists are the simplest way but there might be something better.
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
#define ButtonMask                 (ButtonPressMask|ButtonReleaseMask)
#define CleanMask(mask)            (mask & ~(numlockmask|LockMask) & (ShiftMask|ControlMask|Mod1Mask|Mod2Mask|Mod3Mask|Mod4Mask|Mod5Mask))
#define IsVisible(Client)          (Client->tags & all_monitors[Client->monitor].selected_tags)
#define ArrayLength(X)             (sizeof(X) / sizeof((X)[0]))
#define MouseMask                  (ButtonMask|PointerMotionMask)
#define GappedClientWidth(Client)  ((Client)->width + 2 * (Client)->border_width + gap_size)
#define GappedClientHeight(Client) ((Client)->height + 2 * (Client)->border_width + gap_size)
#define TagMask                    ((1 << ArrayLength(tags)) - 1)
#define TextWidth(X)               (drw_fontset_getwidth(&drw, (X)) + lrpad)

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
        unsigned char isfixed: 1;
        unsigned char isfloating: 1;
        unsigned char isurgent: 1;
        unsigned char isfullscreen: 1;
        unsigned char neverfocus: 1;
    // };
    int oldstate;
    Client *next;
    Client *next_in_stack;
    int monitor; // index into all_monitors array
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
    void (*arrange)(int monitor_index);
};

struct Monitor {
    int is_valid: 1;
    int showbar: 1;
    int topbar: 1;

    float mfact;
    // int nmaster;
    int num;
    int bar_height;               /* bar geometry */
    int screen_x, screen_y, screen_width, screen_height;   /* screen size */
    int window_x, window_y, window_width, window_height;   /* window area  */
    unsigned int selected_tags;   // bit mask to show selected tags
    unsigned int selected_layout; // index into layouts array

    // int num_clients;

    Client *clients;
    Client *selected_client;
    Client *stack;
    Window barwin;
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
static int  applysizehints(Client *client, int *x, int *y, int *width, int *height, int interact);
static void arrange(int monitor_index);
static void arrangemon(int monitor_index);
static void attach(Client *client);
static void attachstack(Client *client);
static void checkotherwm(void);
static void cleanup_monitors(int monitor_index);
static void configure(Client *client);
static int  createmon(void);
static void detach(Client *client);
static void detachstack(Client *client);
static int  dirtomon(int dir);
static void drawbar(int monitor_index);
static void drawbars(void);
static void focus(Client *client);
static Atom getatomprop(Client *client, Atom prop);
static int  getrootptr(int *x, int *y);
static long getstate(Window window);
static pid_t getstatusbarpid();
static int  gettextprop(Window window, Atom atom, char *text, unsigned int size);
static void grabbuttons(Client *client, int focused);
static void grabkeys(void);
static void manage(Window window, XWindowAttributes *wa);
static void monocle(int monitor_index);
static Client *nexttiled(Client *client);
static void pop(Client *);
static int  recttomon(int x, int y, int width, int height);
static void resize(Client *client, int x, int y, int width, int height, int interact);
static void resizeclient(Client *client, int x, int y, int width, int height);
static void restack(int monitor_index);
static int  sendevent(Client *client, Atom proto);
static void sendmon(Client *client, int monitor_index);
static void setclientstate(Client *client, long state);
static void setfocus(Client *client);
static void setfullscreen(Client *client, int fullscreen);
static void setup(void);
static void seturgent(Client *client, int urg);
static void showhide(Client *client);
static void sigchld(int unused);
static void tile(int monitor_index);

static void unfocus(Client *client, int setfocus);
static void unmanage(Client *client, int destroyed);
static void updatebarpos(int monitor_index);
static void updatebars(void);
static void updateclientlist(void);
static int  updategeom(void);
static void updatenumlockmask(void);
static void updatesizehints(Client *client);
static void updatestatus(void);
static void updatetitle(Client *client);
static void updatewindowtype(Client *client);
static void updatewmhints(Client *client);
static Client *wintoclient(Window window);
static int  wintomon(Window window);
static int  xerror(Display *display, XErrorEvent *ee);
static int  xerrordummy(Display *display, XErrorEvent *ee);
static int  xerrorstart(Display *display, XErrorEvent *ee);
static int  next_valid_monitor(int start_index);

// Commands
static void focusmon(const Arg *arg);
static void focusstack(const Arg *arg);
static void killclient(const Arg *arg);
static void movemouse(const Arg *arg);
static void quit(const Arg *arg);
static void resizemouse(const Arg *arg);
static void toggle_layout(const Arg *arg);
static void setmfact(const Arg *arg);
static void sigstatusbar(const Arg *arg);
static void spawn(const Arg *arg);
static void tag(const Arg *arg);
static void tagmon(const Arg *arg);
static void togglefloating(const Arg *arg);
static void toggletag(const Arg *arg);
static void toggleview(const Arg *arg);
static void resize_window(const Arg *arg);
static void move_vert(const Arg *arg);
static void move_horiz(const Arg *arg);
static void change_window_aspect_ratio(const Arg *arg);
static void view(const Arg *arg);
static void zoom(const Arg *arg);
// static void setlayout(const Arg *arg);
// static void rotate(const Arg *arg);

// The only 3 event functions that are still necessary
static void configurerequest(XEvent *event);
static void expose(XEvent *event);
static void maprequest(XEvent *event);

/* variables */
static const char broken[] = "broken";
static char stext[256];
static int statusw;
static int statussig;
static pid_t statuspid = -1;
static int screen;
static int screen_width, screen_height;           /* X global_display screen geometry width, height */
static int bar_height, blw = 0;      /* bar geometry */
static int lrpad;            /* sum of left and right padding for text */
static int (*xerrorxlib)(Display *, XErrorEvent *);
static unsigned int numlockmask = 0;

// typedef void Handler(XEvent*);
// static Handler* handler[LASTEvent] = {
//     [ButtonPress] = buttonpress,
//     [ClientMessage] = clientmessage,
//     [ConfigureRequest] = configurerequest,
//     [ConfigureNotify] = configurenotify,
//     [DestroyNotify] = destroynotify,
//     [EnterNotify] = enternotify,
//     [Expose] = expose,
//     [FocusIn] = focusin,
//     [KeyPress] = keypress,
//     [MappingNotify] = mappingnotify,
//     [MapRequest] = maprequest,
//     [MotionNotify] = motionnotify,
//     [PropertyNotify] = propertynotify,
//     [UnmapNotify] = unmapnotify
// };

static Atom wmatom[WMLast], netatom[NetLast];
static int global_running = 1;
static Cur cursor[CurLast];
static Display *global_display;
static Drw drw;
static int monitor_capacity; // capacity of all_monitors array
static Monitor *all_monitors; // , *selected_monitor;
static int selected_monitor;
static Window root, wmcheckwin;

// inline void _unused(void* x, ...) {}
// #define unused(...) _unused((void*)__VA_ARGS__)

/* configuration, allows nested code to access above variables */
#include "config.h"

static XftColor *scheme_color_buffer; // the 
static XftColor *scheme[ArrayLength(colors)];

/* compile-time check if all tags fit into an unsigned int bit array. */
struct NumTags { char limitexceeded[ArrayLength(tags) > 31 ? -1 : 1]; };

/* function implementations */
void applyrules(Client *client) {
    XClassHint ch = { NULL, NULL };

    /* rule matching */
    client->isfloating = 0;
    client->tags = 0;
    XGetClassHint(global_display, client->window, &ch);
    const char *class    = ch.res_class ? ch.res_class : broken;
    const char *instance = ch.res_name  ? ch.res_name  : broken;

    for (unsigned int i = 0; i < ArrayLength(rules); i++) {
        const Rule *rule = &rules[i];
        if ((!rule->title || strstr(client->name, rule->title))
        && (!rule->class || strstr(class, rule->class))
        && (!rule->instance || strstr(instance, rule->instance))) {
            client->isfloating = rule->isfloating;
            client->tags |= rule->tags;

            for(int monitor_index = 0; monitor_index < monitor_capacity; ++monitor_index) {
                Monitor *monitor = &all_monitors[monitor_index];
                if(monitor->is_valid && monitor->num == rule->monitor_number) {
                    client->monitor = monitor_index;
                    break;
                }
            }
        }
    }
    if (ch.res_class)
        XFree(ch.res_class);
    if (ch.res_name)
        XFree(ch.res_name);
    client->tags = client->tags & TagMask ? client->tags & TagMask : all_monitors[client->monitor].selected_tags;
}

int applysizehints(Client *client, int *x, int *y, int *width, int *height, int interact) {
    int baseismin;

    /* set minimum possible */
    *width = Maximum(1, *width);
    *height = Maximum(1, *height);

    if (interact) {
        if (*x > screen_width)
            *x = screen_width - GappedClientWidth(client);
        if (*y > screen_height)
            *y = screen_height - GappedClientHeight(client);
        if (*x + *width + 2 * client->border_width < 0)
            *x = 0;
        if (*y + *height + 2 * client->border_width < 0)
            *y = 0;
    } else {
        Monitor *monitor = &all_monitors[client->monitor];
        if (*x >= monitor->window_x + monitor->window_width)
            *x = monitor->window_x + monitor->window_width - GappedClientWidth(client);
        if (*y >= monitor->window_y + monitor->window_height)
            *y = monitor->window_y + monitor->window_height - GappedClientHeight(client);
        if (*x + *width + 2 * client->border_width <= monitor->window_x)
            *x = monitor->window_x;
        if (*y + *height + 2 * client->border_width <= monitor->window_y)
            *y = monitor->window_y;
    }

    if (*height < bar_height)
        *height = bar_height;
    if (*width < bar_height)
        *width = bar_height;

    if (resizehints || client->isfloating) {
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

void arrange_monitor(int monitor_index) {
    Monitor* monitor = &all_monitors[monitor_index];
    if(monitor->is_valid) {
        showhide(monitor->stack);
        arrangemon(monitor_index);
        restack(monitor_index);
    }
}

void arrange(int monitor_index) {
    if (Between(monitor_index, 0, monitor_capacity - 1)) {
        arrange_monitor(monitor_index);
    } else {
        for(int monitor_index = 0; monitor_index < monitor_capacity; ++monitor_index) {
            if(all_monitors[monitor_index].is_valid) {
                arrange_monitor(monitor_index);
            }
        }
    }
}

void arrangemon(int monitor_index) {
    layouts[all_monitors[monitor_index].selected_layout].arrange(monitor_index);
}

int next_valid_monitor(int start_index) {
    int result = start_index;
    if(all_monitors[start_index].is_valid) {
        return start_index;
    }
    ++result;
    for (;result != start_index; ++result) {
        if(result == monitor_capacity) {
            result = 0;
        }

        if(all_monitors[result].is_valid) {
            break;
        }
    }
    return result;
}

void attach(Client *client) {
    Monitor* monitor = &all_monitors[client->monitor];
    client->next = monitor->clients;
    monitor->clients = client;
}

void attachstack(Client *client) {
    Monitor* monitor = &all_monitors[client->monitor];
    client->next_in_stack = monitor->stack;
    monitor->stack = client;
}

void buttonpress(XEvent *event) {
    unsigned int i, x;
    Arg arg = {0};
    Client *client;
    XButtonPressedEvent *ev = &event->xbutton;
 	char *text, *s, ch;

    unsigned int click = ClkRootWin;
    /* focus monitor if necessary */
    int monitor_index = wintomon(ev->window);
    if (monitor_index && monitor_index != selected_monitor) {
        unfocus(all_monitors[selected_monitor].selected_client, 1);
        selected_monitor = monitor_index;
        focus(NULL);
    }

    if (ev->window == all_monitors[selected_monitor].barwin) {
        i = x = 0;

        int occupied = 0;
        for (client = all_monitors[monitor_index].clients; client; client = client->next) {
            occupied |= client->tags;
        }

        do {
            if (occupied & (1 << i)) { 
                x += TextWidth(tags[i]);
            }
        } while (ev->x >= x && ++i < ArrayLength(tags));
        if (i < ArrayLength(tags)) {
            click = ClkTagBar;
            arg.ui = 1 << i;
        } else if (ev->x < x + blw) {
            click = ClkLtSymbol;
        } else if (ev->x > all_monitors[selected_monitor].window_width - statusw) {
            x = all_monitors[selected_monitor].window_width - statusw;
            click = ClkStatusText;
            statussig = 0;
            for (text = s = stext; *s && x <= ev->x; s++) {
                if ((unsigned char)(*s) < ' ') {
                    ch = *s;
                    *s = '\0';
                    x += TextWidth(text) - lrpad;
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

void cleanup_monitors(int monitor_index) {
    Monitor *monitor = &all_monitors[monitor_index];
    XUnmapWindow(global_display, monitor->barwin);
    XDestroyWindow(global_display, monitor->barwin);

    Monitor null_monitor = {0};
    *monitor = null_monitor;
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
        if (client != all_monitors[selected_monitor].selected_client && !client->isurgent)
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

void configurerequest(XEvent *event) {
    Client *client;
    XConfigureRequestEvent *ev = &event->xconfigurerequest;
    XWindowChanges wc;

    if ((client = wintoclient(ev->window))) {
        if (ev->value_mask & CWBorderWidth)
            client->border_width = ev->border_width;
        else if (client->isfloating) {
            Monitor *monitor = &all_monitors[client->monitor];
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
                client->x = monitor->screen_x + (monitor->screen_width / 2 - GappedClientWidth(client) / 2); /* center in x direction */

            if ((client->y + client->height) > monitor->screen_y + monitor->screen_height && client->isfloating)
                client->y = monitor->screen_y + (monitor->screen_height / 2 - GappedClientHeight(client) / 2); /* center in y direction */

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

int createmon(void) {
    Monitor *monitor = NULL;
    int result_index;

    if(all_monitors) {
        int monitor_index = 0;
        for(; monitor_index < monitor_capacity; ++monitor_index) {
            Monitor *current_monitor = &all_monitors[monitor_index];
            if(!current_monitor->is_valid) {
                monitor = current_monitor;
                result_index = monitor_index;
                break;
            }
        }

        if(monitor_index == monitor_capacity) {
            // expand array, all monitors up to end must be valid to reach this point
            int new_capacity = monitor_capacity << 1;
            Monitor *new_buffer = ecalloc(new_capacity, sizeof(Monitor));
            for(monitor_index = 0; monitor_index < monitor_capacity; ++monitor_index) {
                new_buffer[monitor_index] = all_monitors[monitor_index];
            }

            // new monitor will be at the end of the new buffer (remember all before this are valid)
            result_index = monitor_capacity;
            monitor = &new_buffer[monitor_capacity];

            free(all_monitors);
            all_monitors = new_buffer;
            monitor_capacity = new_capacity;
        }

    } else {
        monitor_capacity = 2;
        all_monitors = ecalloc(monitor_capacity, sizeof(Monitor));
        monitor = all_monitors;
        result_index = 0;
    }


    monitor->selected_tags = 1;
    monitor->mfact = mfact;
    monitor->showbar = showbar;
    monitor->topbar = topbar;
    monitor->is_valid = 1;

    return result_index;
}

void destroynotify(XEvent *event) {
    Client *client;
    XDestroyWindowEvent *ev = &event->xdestroywindow;

    if ((client = wintoclient(ev->window)))
        unmanage(client, 1);
}

void detach(Client *client) {
    Client **tc;

    for (tc = &all_monitors[client->monitor].clients; *tc && *tc != client; tc = &(*tc)->next);
    *tc = client->next;
}

void detachstack(Client *client) {
    Client **tc, *t;

    for (tc = &all_monitors[client->monitor].stack; *tc && *tc != client; tc = &(*tc)->next_in_stack);
    *tc = client->next_in_stack;

    if (client == all_monitors[client->monitor].selected_client) {
        for (t = all_monitors[client->monitor].stack; t && !IsVisible(t); t = t->next_in_stack);
        all_monitors[client->monitor].selected_client = t;
    }
}

int dirtomon(int dir) {
    int monitor_index = selected_monitor;
    int increment, end_point, loop_point;

    if(dir > 0) {
        increment = 1;
        end_point = monitor_capacity;
        loop_point = 0;
    } else {
        increment = -1;
        end_point = -1;
        loop_point = monitor_capacity - 1;
    }

    for(;;) {
        monitor_index += increment;

        if(monitor_index == end_point) {
            monitor_index = loop_point;
        }

        if(monitor_index == selected_monitor) {
            break;
        } else if(monitor_index == end_point) {
            monitor_index = loop_point;
        }

        if(all_monitors[monitor_index].is_valid) {
            break;
        }
    }

    return monitor_index;
}

void drawbar(int monitor_index) {
    int x, width, text_width = 0;
    int boxs = drw.fonts->height / 9;
    int boxw = drw.fonts->height / 6 + 2;
    unsigned int i, occupied = 0, urg = 0;
    Client *client;

    Monitor *monitor = &all_monitors[monitor_index];
    if (!monitor->showbar)
        return;

    /* draw status first so it can be overdrawn by tags later */
    if (monitor_index == selected_monitor) { /* status is only drawn on selected monitor */
        char *text, ch;
        drw.scheme = scheme[SchemeNorm];

        x = 0;
        for (text = stext; *text; text++) {
            if ((unsigned char)(*text) < ' ') {
                ch = *text;
                *text = '\0';
                text_width = TextWidth(text) - lrpad;
                drw_text(&drw, monitor->window_width - statusw + x, 0, text_width, bar_height, 0, text, 0);
                x += text_width;
                *text = ch;
            }
        }
        text_width = TextWidth(text) - lrpad + 2;
        drw_text(&drw, monitor->window_width - statusw + x, 0, text_width, bar_height, 0, text, 0);
        text_width = statusw;
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
            width = TextWidth(tags[i]);
            drw.scheme = scheme[monitor_is_selected ? SchemeSel : SchemeNorm];
            drw_text(&drw, x, 0, width, bar_height, lrpad / 2, tags[i], urg & 1 << i);

            x += width;
        }
    }

    if ((width = monitor->window_width - text_width - x) > bar_height) {
        if (monitor->selected_client) {
            drw.scheme = scheme[SchemeNorm];
            drw_text(&drw, x, 0, width, bar_height, lrpad / 2, monitor->selected_client->name, 0);
            if (monitor->selected_client->isfloating)
                drw_rect(&drw, x + boxs, boxs, boxw, boxw, monitor->selected_client->isfixed, 0);
        } else {
            drw.scheme = scheme[SchemeNorm];
            drw_rect(&drw, x, 0, width, bar_height, 1, 1);
        }
    }
    drw_map(&drw, monitor->barwin, 0, 0, monitor->window_width, bar_height);
}

void drawbars(void) {
    for (int monitor_index = 0; monitor_index < monitor_capacity; ++monitor_index) {
        if(all_monitors[monitor_index].is_valid) {
            drawbar(monitor_index);
        }
    }
}

void enternotify(XEvent *event) {
}

void expose(XEvent *event) {
    int monitor_index;
    XExposeEvent *ev = &event->xexpose;

    if (ev->count == 0 && (monitor_index = wintomon(ev->window)))
        drawbar(monitor_index);
}

void focus(Client *client) {
    if (!client || !IsVisible(client))
        for (client = all_monitors[selected_monitor].stack; client && !IsVisible(client); client = client->next_in_stack) {
        }

    Client *selected_client = all_monitors[selected_monitor].selected_client;
    if (selected_client && selected_client != client)
        unfocus(selected_client, 0);

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
    all_monitors[selected_monitor].selected_client = client;
    drawbars();
}

void set_current_monitor(int monitor_index) {
    unfocus(all_monitors[selected_monitor].selected_client, 0);
    selected_monitor = monitor_index;
    focus(NULL);
}

void focusmon(const Arg *arg) {

    // if (!all_monitors->next)
    //     return;

    int monitor_index;
    if ((monitor_index = dirtomon(arg->i)) == selected_monitor)
        return;

    set_current_monitor(monitor_index);
}

void focusstack(const Arg *arg) {
    Client *client = NULL, *i;

    Monitor *monitor = &all_monitors[selected_monitor];
    if (!monitor->selected_client || (monitor->selected_client->isfullscreen && lockfullscreen))
        return;

    if (arg->i > 0) {
        for (client = monitor->selected_client->next; client && !IsVisible(client); client = client->next);
        if (!client)
            for (client = monitor->clients; client && !IsVisible(client); client = client->next);
    } else {
        for (i = monitor->clients; i != monitor->selected_client; i = i->next)
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

// void incnmaster(const Arg *arg) {
//     selected_monitor->nmaster = Maximum(selected_monitor->nmaster + arg->i, 0);
//     arrange(selected_monitor);
// }

#ifdef XINERAMA
static int isuniquegeom(XineramaScreenInfo *unique, size_t n, XineramaScreenInfo *info) {
    while (n--)
        if(unique[n].x_org == info->x_org && unique[n].y_org  == info->y_org
           && unique[n].width == info->width && unique[n].height == info->height)
            return 0;
    return 1;
}
#endif /* XINERAMA */

void killclient(const Arg *arg) {
    Client *selected_client = all_monitors[selected_monitor].selected_client;
    if (!selected_client)
        return;
    if (!sendevent(selected_client, wmatom[WMDelete])) {
        XGrabServer(global_display);
        XSetErrorHandler(xerrordummy);
        XSetCloseDownMode(global_display, DestroyAll);
        XKillClient(global_display, selected_client->window);
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

    Monitor *monitor = &all_monitors[client->monitor];
    if (client->x + GappedClientWidth(client) > monitor->screen_x + monitor->screen_width)
        client->x = monitor->screen_x + monitor->screen_width - GappedClientWidth(client);
    if (client->y + GappedClientHeight(client) > monitor->screen_y + monitor->screen_height)
        client->y = monitor->screen_y + monitor->screen_height - GappedClientHeight(client);
    client->x = Maximum(client->x, monitor->screen_x);
    /* only fix client y-offset, if the client center might cover the bar */
    client->y = Maximum(client->y, ((monitor->bar_height == monitor->screen_y) && (client->x + (client->width / 2) >= monitor->window_x)
                                && (client->x + (client->width / 2) < monitor->window_x + monitor->window_width)) ? bar_height : monitor->screen_y);
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
    XMoveResizeWindow(global_display, client->window, client->x + 2 * screen_width, client->y, client->width, client->height); /* some windows require this */
    setclientstate(client, NormalState);

    if (client->monitor == selected_monitor)
        unfocus(all_monitors[selected_monitor].selected_client, 0);

    monitor->selected_client = client;
    arrange(client->monitor);
    XMapWindow(global_display, client->window);
    focus(NULL);
}

void mappingnotify(XEvent *event) {
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
void monocle(int monitor_index) {
    Client *client;
    Monitor* monitor = &all_monitors[monitor_index];
    // assume the monitor is valid? It wasn't checked for NULL before
    for (client = nexttiled(monitor->clients); client; client = nexttiled(client->next)) {
        resize(client,
               monitor->window_x,
               monitor->window_y,
               monitor->window_width,
               monitor->window_height,
               0);
    }
}

void tile(int monitor_index) {
    unsigned int num_clients;
    Monitor* monitor = &all_monitors[monitor_index];
    Client *client = nexttiled(monitor->clients);


    for (num_clients = 0; client; client = nexttiled(client->next), num_clients++) {
    }

    if (num_clients == 0)
        return;

    client = nexttiled(monitor->clients);

    if(num_clients == 1) {
        // draw master window on the full screen, basically monocle
        resize(client,
               monitor->window_x,
               monitor->window_y,
               monitor->window_width,
               monitor->window_height,
               0);
    } else {
        unsigned int ty = 0;
        unsigned int master_width = monitor->window_width * monitor->mfact;

        // draw master window on left
        resize(client,
               monitor->window_x,
               monitor->window_y,
               master_width - (2*client->border_width),
               monitor->window_height - (2*client->border_width),
               0);

        client = nexttiled(client->next);
        master_width -= gap_size;

        // draw remaining windows on right
        unsigned int height = (monitor->window_height - gap_size) / (num_clients - 1);
        for (; client != NULL; client = nexttiled(client->next)) {
            resize(client,
                   monitor->window_x + master_width,
                   monitor->window_y + ty,
                   monitor->window_width - master_width - (2*client->border_width),
                   height - (2*client->border_width) + gap_size,
                   0);

            unsigned int new_ty = ty + height;
            if (new_ty < monitor->window_height) {
                ty = new_ty;
            }
        }
    }
}

// --

void movemouse(const Arg *arg) {
    Client *client;
    XEvent ev;
    Time lasttime = 0;

    if (!(client = all_monitors[selected_monitor].selected_client))
        return;

    if (client->isfullscreen) /* no support moving fullscreen windows by mouse */
        return;

    restack(selected_monitor);
    int ocx = client->x;
    int ocy = client->y;
    if (XGrabPointer(global_display, root, False, MouseMask, GrabModeAsync, GrabModeAsync,
                     None, cursor[CurMove].cursor, CurrentTime) != GrabSuccess)
        return;

    int x, y;
    if (!getrootptr(&x, &y))
        return;

    Monitor *monitor = &all_monitors[selected_monitor];
    do {
        XMaskEvent(global_display, MouseMask|ExposureMask|SubstructureRedirectMask, &ev);
        switch(ev.type) {
            case ConfigureRequest:
                configurerequest(&ev);
                break;

            case Expose:
                expose(&ev);
                break;

            case MapRequest:
                maprequest(&ev);
                break;

            case MotionNotify:
                if ((ev.xmotion.time - lasttime) <= (1000 / 60))
                    continue;

                lasttime = ev.xmotion.time;
                int nx = ocx + (ev.xmotion.x - x);
                int ny = ocy + (ev.xmotion.y - y);

                if (abs(monitor->window_x - nx) < snap)
                    nx = monitor->window_x;
                else if (abs((monitor->window_x + monitor->window_width) - (nx + GappedClientWidth(client))) < snap)
                    nx = monitor->window_x + monitor->window_width - GappedClientWidth(client);

                if (abs(monitor->window_y - ny) < snap)
                    ny = monitor->window_y;
                else if (abs((monitor->window_y + monitor->window_height) - (ny + GappedClientHeight(client))) < snap)
                    ny = monitor->window_y + monitor->window_height - GappedClientHeight(client);

                if (!client->isfloating && (abs(nx - client->x) > snap || abs(ny - client->y) > snap))
                    togglefloating(NULL);

                if (client->isfloating)
                    resize(client, nx, ny, client->width, client->height, 1);

                break;
        }
    } while (ev.type != ButtonRelease);
    XUngrabPointer(global_display, CurrentTime);

    int monitor_index = recttomon(client->x, client->y, client->width, client->height);
    if (monitor_index != selected_monitor) {
        sendmon(client, monitor_index);
        selected_monitor = monitor_index;
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
}

void quit(const Arg *arg) {
    global_running = 0;
}

int recttomon(int x, int y, int width, int height) {
    int maximum_area = 0;

    int result = selected_monitor;
    for (int monitor_index = 0; monitor_index < monitor_capacity; ++monitor_index) {
        Monitor *monitor = &all_monitors[monitor_index];
        if(monitor->is_valid) {
            int x_intersection = Maximum(0, Minimum(x + width,  monitor->window_x + monitor->window_width)  - Maximum(x, monitor->window_x));
            int y_intersection = Maximum(0, Minimum(y + height, monitor->window_y + monitor->window_height) - Maximum(y, monitor->window_y));
            int intersection_area = x_intersection * y_intersection;
            if (intersection_area > maximum_area) {
                maximum_area = intersection_area;
                result = monitor_index;
            }
        }
    }
    return result;
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

    wc.border_width = client->border_width;

    /* Get number of clients for the client's monitor */
    for (n = 0, nbc = nexttiled(all_monitors[client->monitor].clients); nbc; nbc = nexttiled(nbc->next), n++);

    /* Do nothing if layout is floating */
    if (client->isfloating) {
        gapincr = gapoffset = 0;
    } else {
        /* Remove border and gap if layout is monocle or only one client */
        if (all_monitors[client->monitor].selected_layout == monocle_index || n == 1) {
            wc.border_width = 0;
        }

        gapincr = 2 * gap_size;
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
    XEvent ev;
    Time lasttime = 0;

    if (!(client = all_monitors[selected_monitor].selected_client))
        return;

    if (client->isfullscreen) /* no support resizing fullscreen windows by mouse */
        return;

    restack(selected_monitor);
    ocx = client->x;
    ocy = client->y;
    if (XGrabPointer(global_display, root, False, MouseMask, GrabModeAsync, GrabModeAsync,
                     None, cursor[CurResize].cursor, CurrentTime) != GrabSuccess)
        return;

    XWarpPointer(global_display, None, client->window, 0, 0, 0, 0, client->width + client->border_width - 1, client->height + client->border_width - 1);

    Monitor *client_monitor = &all_monitors[client->monitor];
    Monitor *monitor = &all_monitors[selected_monitor];
    do {
        XMaskEvent(global_display, MouseMask|ExposureMask|SubstructureRedirectMask, &ev);
        switch(ev.type) {
            case ConfigureRequest:
                configurerequest(&ev);
                break;

            case Expose:
                expose(&ev);
                break;

            case MapRequest:
                maprequest(&ev);
                break;

            case MotionNotify:
                if ((ev.xmotion.time - lasttime) <= (1000 / 60))
                    continue;
                lasttime = ev.xmotion.time;

                nw = Maximum(ev.xmotion.x - ocx - 2 * client->border_width + 1, 1);
                nh = Maximum(ev.xmotion.y - ocy - 2 * client->border_width + 1, 1);
                if (client_monitor->window_x + nw >= monitor->window_x && client_monitor->window_x + nw <= monitor->window_x + monitor->window_width
                    && client_monitor->window_y + nh >= monitor->window_y && client_monitor->window_y + nh <= monitor->window_y + monitor->window_height)
                {
                    if (!client->isfloating && (abs(nw - client->width) > snap || abs(nh - client->height) > snap))
                        togglefloating(NULL);
                }

                if (client->isfloating)
                    resize(client, client->x, client->y, nw, nh, 1);
                break;
        }
    } while (ev.type != ButtonRelease);

    XWarpPointer(global_display, None, client->window, 0, 0, 0, 0, client->width + client->border_width - 1, client->height + client->border_width - 1);
    XUngrabPointer(global_display, CurrentTime);

    while (XCheckMaskEvent(global_display, EnterWindowMask, &ev));

    int monitor_index = recttomon(client->x, client->y, client->width, client->height);
    if (monitor_index != selected_monitor) {
        sendmon(client, monitor_index);
        selected_monitor = monitor_index;
        focus(NULL);
    }
}

void restack(int monitor_index) {
    Client *client;
    XEvent ev;
    XWindowChanges wc;

    drawbar(monitor_index);
    Monitor *monitor = &all_monitors[monitor_index];
    if (!monitor->selected_client)
        return;

    if (monitor->selected_client->isfloating) {
        XRaiseWindow(global_display, monitor->selected_client->window);
    }

    wc.stack_mode = Below;
    wc.sibling = monitor->barwin;
    for (client = monitor->stack; client; client = client->next_in_stack) {
        if (!client->isfloating && IsVisible(client)) {
            XConfigureWindow(global_display, client->window, CWSibling|CWStackMode, &wc);
            wc.sibling = client->window;
        }
    }
    XSync(global_display, False);
    while (XCheckMaskEvent(global_display, EnterWindowMask, &ev));
}

void sendmon(Client *client, int monitor_index) {
    if (client->monitor == monitor_index)
        return;

    unfocus(client, 1);
    detach(client);
    detachstack(client);
    client->monitor = monitor_index;
    client->tags = all_monitors[monitor_index].selected_tags; /* assign tags of target monitor */
    attach(client);
    attachstack(client);
    focus(NULL);
    arrange(-1);
}

void setclientstate(Client *client, long state) {
    long data[] = { state, None };

    XChangeProperty(global_display, client->window, wmatom[WMState], wmatom[WMState], 32,
                    PropModeReplace, (unsigned char *)data, 2);
}

int sendevent(Client *client, Atom proto) {
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

        Monitor* client_monitor = &all_monitors[client->monitor];
        resizeclient(client, client_monitor->screen_x, client_monitor->screen_y, client_monitor->screen_width, client_monitor->screen_height);
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

// void setlayout(const Arg *arg) {
//     if (!arg || !arg->v || arg->v != &layouts[selected_monitor->selected_layout]) {
//         selected_monitor->selected_layout ^= 1;
//     }
// 
//     if (arg && arg->v) {
//         // layouts[selected_monitor->selected_layout] = (Layout *)arg->v;
//     }
// 
//     if (selected_monitor->selected_client) {
//         arrange(selected_monitor);
//     } else {
//         drawbar(selected_monitor);
//     }
// }

void toggle_layout(const Arg *arg) {
    all_monitors[selected_monitor].selected_layout ^= 1;
    if (all_monitors[selected_monitor].selected_client) {
        arrange(selected_monitor);
    } else {
        drawbar(selected_monitor);
    }
}

/* arg > 1.0 will set mfact absolutely */
void setmfact(const Arg *arg) {
    float f;

    if (!arg)
        return;
    f = arg->f < 1.0 ? arg->f + all_monitors[selected_monitor].mfact : arg->f - 1.0;
    if (f < 0.05 || f > 0.95)
        return;
    all_monitors[selected_monitor].mfact = f;
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
    screen_width = DisplayWidth(global_display, screen);
    screen_height = DisplayHeight(global_display, screen);
    root = RootWindow(global_display, screen);
    drw_init(&drw, global_display, screen, root, screen_width, screen_height);
    if (!drw_fontset_create(&drw, fonts, ArrayLength(fonts)))
        die("no fonts could be loaded.");
    lrpad = drw.fonts->height;
    bar_height = drw.fonts->height + 2;
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
    cursor[CurNormal] = drw_cur_create(&drw, XC_left_ptr);
    cursor[CurResize] = drw_cur_create(&drw, XC_sizing);
    cursor[CurMove] = drw_cur_create(&drw, XC_fleur);
    /* init appearance */

    // TODO: the hard-coded 3 is for foreground, background and border from ColorSet. Find a better way to do this. Find a better way to do this
    scheme_color_buffer = ecalloc(3 * ArrayLength(colors), sizeof(XftColor));
    for (i = 0; i < ArrayLength(colors); i++) {
        XftColor *xft_color = &scheme_color_buffer[i * 3];
        drw_scm_create(&drw, &colors[i], xft_color);
        scheme[i] = xft_color;
    }
    
    for (i = 0; i < ArrayLength(colors); i++)

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
    wa.cursor = cursor[CurNormal].cursor;
    wa.event_mask = SubstructureRedirectMask|SubstructureNotifyMask
      |ButtonPressMask|PointerMotionMask|EnterWindowMask
      |LeaveWindowMask|StructureNotifyMask|PropertyChangeMask;
    XChangeWindowAttributes(global_display, root, CWEventMask|CWCursor, &wa);
    XSelectInput(global_display, root, wa.event_mask);
    grabkeys();
    focus(NULL);
}


void seturgent(Client *client, int urg) {
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
        if (client->isfloating && !client->isfullscreen) {
            resize(client, client->x, client->y, client->width, client->height, 0);
        }
        showhide(client->next_in_stack);
    } else {
        /* hide clients bottom up */
        showhide(client->next_in_stack);
        XMoveWindow(global_display, client->window, GappedClientWidth(client) * -2, client->y);
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
        dmenumon[0] = '0' + all_monitors[selected_monitor].num;
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
    Client *selected_client = all_monitors[selected_monitor].selected_client;
    if (selected_client && arg->ui & TagMask) {
        selected_client->tags = arg->ui & TagMask;
        focus(NULL);
        arrange(selected_monitor);
    }
}

void tagmon(const Arg *arg) {
    Client *selected_client = all_monitors[selected_monitor].selected_client;
    if (!selected_client)
        return;

    int next_monitor = dirtomon(arg->i);
    if(next_monitor != selected_monitor) {
        sendmon(selected_client, next_monitor);
    }
}

void togglefloating(const Arg *arg) {
    Monitor *monitor = &all_monitors[selected_monitor];
    Client *selected_client = monitor->selected_client;

    if (selected_client && !selected_client->isfullscreen) {
        /* no support for fullscreen windows */

        selected_client->isfloating = !selected_client->isfloating || selected_client->isfixed;
        if (selected_client->isfloating) {
            int centered_x = monitor->window_width/2 - selected_client->width/2;
            int centered_y = monitor->window_height/2 - selected_client->height/2;

            resize(selected_client, centered_x, centered_y,
                   selected_client->width, selected_client->height, 0);
        }

        arrange(selected_monitor);
    }
}

void toggletag(const Arg *arg) {
    unsigned int newtags;

    Client *selected_client = all_monitors[selected_monitor].selected_client;
    if (!selected_client)
        return;
    newtags = selected_client->tags ^ (arg->ui & TagMask);
    if (newtags) {
        selected_client->tags = newtags;
        focus(NULL);
        arrange(selected_monitor);
    }
}

void toggleview(const Arg *arg) {
    unsigned int newtagset = all_monitors[selected_monitor].selected_tags ^ (arg->ui & TagMask);
    if (newtagset) {
        all_monitors[selected_monitor].selected_tags = newtagset;
        focus(NULL);
        arrange(selected_monitor);
    }
}

void resize_window(const Arg *arg) {
    Client *selected_client = all_monitors[selected_monitor].selected_client;
    int resize_amount = arg->i > 0 ? 5 : -5;

    if (selected_client && selected_client->isfloating) {
        resize(selected_client,
               selected_client->x + resize_amount,
               selected_client->y + resize_amount,
               selected_client->width  - 2*resize_amount,
               selected_client->height - 2*resize_amount,
               0);
    } else {
        int new_gap_size = gap_size + resize_amount;
        if(new_gap_size >= 0) {
            gap_size = new_gap_size;
            arrange(-1);
        }
    }
}

void move_vert(const Arg *arg) {
    Client *selected_client = all_monitors[selected_monitor].selected_client;

    int move_amount = arg->i > 0 ? 5 : -5;
    if(selected_client && selected_client->isfloating) {
        resize(selected_client,
               selected_client->x,
               selected_client->y + move_amount,
               selected_client->width,
               selected_client->height,
               0);
    }
}

void move_horiz(const Arg *arg) {
    Client *selected_client = all_monitors[selected_monitor].selected_client;

    int move_amount = arg->i > 0 ? 5 : -5;
    if(selected_client && selected_client->isfloating) {
        resize(selected_client, selected_client->x + move_amount, selected_client->y, selected_client->width, selected_client->height, 0);
    }
}

void change_window_aspect_ratio(const Arg *arg) {
    Client *selected_client = all_monitors[selected_monitor].selected_client;

    int resize_amount = arg->i > 0 ? 5 : -5;
    if(selected_client && selected_client->isfloating) {
        resize(selected_client,
               selected_client->x - resize_amount,
               selected_client->y + resize_amount,
               selected_client->width  + 2*resize_amount,
               selected_client->height - 2*resize_amount,
               0);
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
    int monitor = client->monitor;
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

void updatebars(void) {
    XSetWindowAttributes wa = {
        .override_redirect = True,
        .background_pixmap = ParentRelative,
        .event_mask = ButtonPressMask|ExposureMask
    };
    XClassHint ch = {"dwm", "dwm"};
    for (int monitor_index = 0; monitor_index < monitor_capacity; ++monitor_index) {
        Monitor *monitor = &all_monitors[monitor_index];
        if(monitor->is_valid) {
            if (monitor->barwin)
                continue;
            monitor->barwin = XCreateWindow(global_display, root, monitor->window_x, monitor->bar_height, monitor->window_width, bar_height, 0, DefaultDepth(global_display, screen),
                                            CopyFromParent, DefaultVisual(global_display, screen),
                                            CWOverrideRedirect|CWBackPixmap|CWEventMask, &wa);
            XDefineCursor(global_display, monitor->barwin, cursor[CurNormal].cursor);
            XMapRaised(global_display, monitor->barwin);
            XSetClassHint(global_display, monitor->barwin, &ch);
        }
    }
}

void updatebarpos(int monitor_index) {
    Monitor *monitor = &all_monitors[monitor_index];

    monitor->window_y = monitor->screen_y;
    monitor->window_height = monitor->screen_height;
    if (monitor->showbar) {
        monitor->window_height -= bar_height;
        monitor->bar_height = monitor->topbar ? monitor->window_y : monitor->window_y + monitor->window_height;
        monitor->window_y = monitor->topbar ? monitor->window_y + bar_height : monitor->window_y;
    } else
        monitor->bar_height = -bar_height;
}

void updateclientlist() {
    XDeleteProperty(global_display, root, netatom[NetClientList]);
    for (int monitor_index = 0; monitor_index < monitor_capacity; ++monitor_index) {
        Monitor *monitor = &all_monitors[monitor_index];
        if(monitor->is_valid) {
            for (Client *client = monitor->clients; client; client = client->next)
                XChangeProperty(global_display, root, netatom[NetClientList],
                                XA_WINDOW, 32, PropModeAppend,
                                (unsigned char *) &(client->window), 1);
        }
    }
}

int updategeom(void) {
    int dirty = 0;

#ifdef XINERAMA
    if (XineramaIsActive(global_display)) {
        int i, j, num_screens;
        Client *client;
        XineramaScreenInfo *info = XineramaQueryScreens(global_display, &num_screens);
        XineramaScreenInfo *unique = NULL;

        int monitor_count = 0;
        for (int monitor_index = 0; monitor_index < monitor_capacity; ++monitor_index) {
            Monitor *monitor = &all_monitors[monitor_index];
            monitor_count += (monitor->is_valid != 0);
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
                createmon();
            }

            for (int i = 0, monitor_index = 0; i < num_screens;) {
                Monitor *current_monitor = &all_monitors[monitor_index];

                if(current_monitor->is_valid) {
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
                        updatebarpos(monitor_index);
                    }
                    i++;
                }
                ++monitor_index;
            }
        } else { /* less monitors available num_screens < monitor_count */
            for (i = num_screens; i < monitor_count; i++) {
                int monitor_index = monitor_capacity - 1;
                for (; Between(monitor_index, 0, monitor_capacity - 1) && !all_monitors[monitor_index].is_valid; --monitor_index);

                Monitor *monitor = &all_monitors[monitor_index];

                while ((client = monitor->clients)) {
                    dirty = 1;
                    monitor->clients = client->next;
                    monitor->is_valid = 0;

                    detachstack(client);
                    client->monitor = next_valid_monitor(client->monitor + 1);
                    attach(client);
                    attachstack(client);
                }
                if (monitor_index == selected_monitor) {
                    selected_monitor = next_valid_monitor(0);
                }

                cleanup_monitors(monitor_index);
            }
        }
        free(unique);
    } else
#endif /* XINERAMA */
    { /* default monitor setup */
        if (!all_monitors)
            createmon();

        int first_monitor_index = next_valid_monitor(0);
        Monitor *first_monitor = &all_monitors[first_monitor_index];
        if (first_monitor->screen_width != screen_width || first_monitor->screen_height != screen_height) {
            dirty = 1;
            first_monitor->screen_width  = first_monitor->window_width = screen_width;
            first_monitor->screen_height = first_monitor->window_height = screen_height;

            updatebarpos(first_monitor_index);
        }
    }
    if (dirty) {
        // wintomon will return selected_monitor as a fallback
        selected_monitor = next_valid_monitor(0);
        selected_monitor = wintomon(root);
    }

    return dirty;
}

void updatenumlockmask(void) {
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

void updatesizehints(Client *client) {
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
        statusw = TextWidth(stext) - lrpad + 2;
    } else {
        char *text, *s, ch;

        statusw  = 0;
        for (text = s = stext; *s; s++) {
            if ((unsigned char)(*s) < ' ') {
                ch = *s;
                *s = '\0';
                statusw += TextWidth(text) - lrpad;
                *s = ch;
                text = s + 1;
            }
        }
        statusw += TextWidth(text) - lrpad + 2;
    }
    drawbar(selected_monitor);
}

void updatetitle(Client *client) {
    if (!gettextprop(client->window, netatom[NetWMName], client->name, sizeof(client->name)))
        gettextprop(client->window, XA_WM_NAME, client->name, sizeof(client->name));
    if (client->name[0] == '\0') /* hack to mark broken clients */
        strcpy(client->name, broken);
}

void updatewindowtype(Client *client) {
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
        if(client == all_monitors[selected_monitor].selected_client && wmh->flags & XUrgencyHint) {
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
    Monitor *monitor = &all_monitors[selected_monitor];
    if (new_tags != monitor->selected_tags) {
        if (new_tags)
            monitor->selected_tags = new_tags;

        focus(NULL);
        arrange(selected_monitor);
    }
}

// void rotate(const Arg *arg) {
//     // INCOMPLETE
//     Monitor *monitor = &all_monitors[selected_monitor];
// 
//     if(monitor->stack && monitor->stack->next) {
//         Client *bottom = monitor->stack, *second_from_bottom = NULL;
//         for(bottom = monitor->stack;
//             bottom->next;
//             bottom = bottom->next) {
//             second_from_bottom = bottom;
//         }
// 
//         if(arg->i > 0) {
//             // rotate right
//             second_from_bottom->next = NULL;
//             bottom->next = monitor->stack;
//             monitor->stack = bottom;
//         } else {
//             // rotate left
//             Client *top = monitor->stack;
//             monitor->stack = top->next;
//             bottom->next = top;
//             top->next = NULL;
//         }
// 
//         focus(monitor->stack);
//         arrange(selected_monitor);
//     }
// }

Client *wintoclient(Window window) {
    for(int monitor_index = 0; monitor_index < monitor_capacity; ++monitor_index) {
        Monitor* monitor = &all_monitors[monitor_index];
        if(monitor->is_valid) {
            for (Client *client = monitor->clients; client; client = client->next)
                if (client->window == window)
                    return client;
        }
    }
    return NULL;
}

int wintomon(Window window) {
    int x = 0, y = 0;
    if (window == root && getrootptr(&x, &y))
        return recttomon(x, y, 1, 1);

    for(int monitor_index = 0; monitor_index < monitor_capacity; ++monitor_index) {
        Monitor *monitor = &all_monitors[monitor_index];
        if (monitor->is_valid && window == monitor->barwin) {
            return monitor_index;
        }
    }

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
    Client *selected_client = all_monitors[selected_monitor].selected_client;

    if(selected_client && selected_client->isfloating)
        return;

    if(selected_client == nexttiled(all_monitors[selected_monitor].clients))
        if(!selected_client || !(selected_client = nexttiled(selected_client->next)))
            return;

    pop(selected_client);
}

int main(int argc, char *argv[]) {
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

    // setup
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

    // run
    XEvent event;
    /* main event loop */
    XSync(global_display, False);
    while (global_running && !XNextEvent(global_display, &event)) {
        switch (event.type) {
            case ButtonPress: {
                buttonpress(&event);
                break;
            }

            case ClientMessage: {
                clientmessage(&event);
                break;
            }

            case ConfigureRequest: {
                configurerequest(&event);
                break;
            }

            case ConfigureNotify: {
                Client *client;
                XConfigureEvent *ev = &event.xconfigure;
                int dirty;

                /* TODO: updategeom handling sucks, needs to be simplified */
                if (ev->window == root) {
                    dirty = (screen_width != ev->width || screen_height != ev->height);
                    screen_width = ev->width;
                    screen_height = ev->height;

                    if (updategeom() || dirty) {
                        drw_resize(&drw, screen_width, bar_height);
                        updatebars();
                        for (int monitor_index = 0; monitor_index < monitor_capacity; ++monitor_index) {
                            Monitor *monitor = &all_monitors[monitor_index];
                            if(monitor->is_valid) {
                                for (client = monitor->clients; client; client = client->next) {
                                    if (client->isfullscreen) {
                                        resizeclient(client, monitor->screen_x, monitor->screen_y, monitor->screen_width, monitor->screen_height);
                                    }
                                }
                                XMoveResizeWindow(global_display, monitor->barwin, monitor->window_x, monitor->bar_height, monitor->window_width, bar_height);
                            }
                        }
                        focus(NULL);
                        arrange(-1);
                    }
                }
                break;
            }

            case DestroyNotify: {
                destroynotify(&event);
                break;
            }

            case EnterNotify: {
                Client *client;
                XCrossingEvent *ev = &event.xcrossing;

                if ((ev->mode == NotifyNormal && ev->detail != NotifyInferior) || ev->window == root) {
                    client = wintoclient(ev->window);
                    int monitor_index = client ? client->monitor : wintomon(ev->window);
                    if (monitor_index != selected_monitor) {
                        unfocus(all_monitors[selected_monitor].selected_client, 1);
                        selected_monitor = monitor_index;
                    } else if (client && client != all_monitors[selected_monitor].selected_client) {
                        focus(client);
                    }
                }
                break;
            }

            case Expose: {
                expose(&event);
                break;
            }

            case FocusIn: {
                /* there are some broken focus acquiring clients needing extra handling */
                XFocusChangeEvent *ev = &event.xfocus;

                Client *selected_client = all_monitors[selected_monitor].selected_client;
                if (selected_client && ev->window != selected_client->window)
                    setfocus(selected_client);
                break;
            }

            case KeyPress: {
                XKeyEvent *ev = &event.xkey;
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
                break;
            }

            case MappingNotify: {
                XMappingEvent *ev = &event.xmapping;

                XRefreshKeyboardMapping(ev);
                if (ev->request == MappingKeyboard)
                    grabkeys();
                break;
            }

            case MapRequest: {
                maprequest(&event);
                break;
            }

            case MotionNotify: {
                static int previous_monitor_index = -1;
                XMotionEvent *ev = &event.xmotion;

                if (ev->window == root) {
                    int monitor_index = recttomon(ev->x_root, ev->y_root, 1, 1);
                    if (monitor_index != previous_monitor_index && Between(previous_monitor_index, 0, monitor_capacity - 1)) {
                        unfocus(all_monitors[selected_monitor].selected_client, 1);
                        selected_monitor = monitor_index;
                        focus(NULL);
                    }

                    previous_monitor_index = monitor_index;
                }
                break;
            }

            case PropertyNotify: {
                Client *client;
                Window trans;
                XPropertyEvent *ev = &event.xproperty;

                if ((ev->window == root) && (ev->atom == XA_WM_NAME)) {
                    updatestatus();
                } else if (ev->state == PropertyDelete) {
                    /* ignore */
                } else if ((client = wintoclient(ev->window))) {
                    switch(ev->atom) {
                        default: break;
                        case XA_WM_TRANSIENT_FOR:
                                 if (!client->isfloating && (XGetTransientForHint(global_display, client->window, &trans)) &&
                                     (client->isfloating = (wintoclient(trans) != NULL)))
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
                        if (client == all_monitors[client->monitor].selected_client)
                            drawbar(client->monitor);
                    }
                    if (ev->atom == netatom[NetWMWindowType])
                        updatewindowtype(client);
                }
                break;
            }

            case UnmapNotify: {
                Client *client;
                XUnmapEvent *unmap_event = &event.xunmap;

                if ((client = wintoclient(unmap_event->window))) {
                    if (unmap_event->send_event)
                        setclientstate(client, WithdrawnState);
                    else
                        unmanage(client, 0);
                }
                break;
            }
        }

        // if (handler[event.type]) {
        //     handler[event.type](&event); /* call handler */
        // }
    }

    // cleanup
    Arg a = { .ui = ~0 };

    view(&a);

    for (int monitor_index = 0; monitor_index < monitor_capacity; ++monitor_index) {
        Monitor *monitor = &all_monitors[monitor_index];
        if(monitor->is_valid) {
            while (monitor->stack)
                unmanage(monitor->stack, 0);
        }
    }

    XUngrabKey(global_display, AnyKey, AnyModifier, root);
    for(int monitor_index = 0; monitor_index < monitor_capacity; ++monitor_index) {
        if(all_monitors[monitor_index].is_valid) {
            cleanup_monitors(monitor_index);
        }
    }

    for (size_t i = 0; i < CurLast; i++)
        XFreeCursor(drw.display, cursor[i].cursor);

    free(scheme_color_buffer);

    XDestroyWindow(global_display, wmcheckwin);
    drw_clean(&drw);
    XSync(global_display, False);
    XSetInputFocus(global_display, PointerRoot, RevertToPointerRoot, CurrentTime);
    XDeleteProperty(global_display, root, netatom[NetActiveWindow]);
    XCloseDisplay(global_display);

    return EXIT_SUCCESS;
}

