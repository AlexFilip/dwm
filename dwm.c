/* See LICENSE file for copyright and license details.
 *
 * dynamic window manager is designed like any other X client as well. It is
 * driven through handling X events. In contrast to other X clients, a window
 * manager selects for SubstructureRedirectMask on the root window, to receive
 * events about window (dis-)appearance. Only one X connection at a time is
 * allowed to select for this event mask.
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

/*
 * FIX:
 *  - When going from 2 screens to 1 the clients on the second screen don't move to the first. Then when switching back they can't be accessed with Mod+h/Mod+l.
 *  - On startup, the bar does not take the full width of the screen and does not display the full status, especially on laptop.
 *  - After bringing gap size down to 0 then bringing it back up (windows to full size then back down) the master client does not resize until swapped out.
 */

/* TODO:
 *  - Bring status bar into this project
 *    - NOTE: All of the status bar code will go into the "status" folder
 *    - Look at anybar code and see if it's a good idea to use
 *    - Compile both dwm and status bar at the same time
 *    - Path of status bar should be compiled into dwm using -D flag (StatusBarFlag in Makefile)
 *    - Dwm should execute status bar
 *    - If it crashes, dwm should restart it
 *    - Status bar displays status, tags, etc. responds to clicks and communicates to dwm to switch tag
 *    - Status will be like dwm async but with its own window, like polybar
 *
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
 *    - Some shortcuts should be kept in dwm in case the process crashes. That way the user can continue to fix the issue. (probably just vim, the terminal and dmenu are needed)
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
#include <fcntl.h>
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

#define fn static
#define global static

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
enum { SchemeNorm, SchemeSel, SchemeBar, SchemeAppLaunch }; /* color schemes */
enum { NetSupported, NetWMName, NetWMState, NetWMCheck,
       NetWMFullscreen, NetActiveWindow, NetWMWindowType,
       NetWMWindowTypeDialog, NetClientList, NetLast }; /* EWMH atoms */
enum { WMProtocols, WMDelete, WMState, WMTakeFocus, WMLast }; /* default atoms */
enum { ClkTagBar, ClkStatusText, ClkWinTitle,
       ClkClientWin, ClkRootWin, ClkLast }; /* clicks */

typedef struct Array Array;
struct Array {
    void* content;
    int length;
};

enum {
    ModeNormal,
    ModeQuit,
    ModeBrowser,
    ModeSurfBrowser,
};

typedef struct Mode Mode;
struct Mode {
    char* name;
};


typedef union ActionDetails ActionDetails;
union ActionDetails {
    int i;
    unsigned int ui;
    float f;
    const void *v;
};
typedef void Action(const ActionDetails *arg);

typedef struct Button Button;
struct Button {
    unsigned int click;
    unsigned int mask;
    unsigned int button;
    Action *func;
    const ActionDetails arg;
};

typedef struct Key Key;
struct Key {
    unsigned int mod;
    KeySym keysym;
    Action *func;
    const ActionDetails arg;
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

typedef struct Layout Layout;
struct Layout {
    void (*arrange)(int monitor_index);
};

struct Monitor {
    int is_valid: 1;
    int showbar: 1;
    int topbar: 1;

    int mfact;
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

// typedef struct Rule Rule;
// struct Rule {
//     const char *class;
//     const char *instance;
//     const char *title;
//     unsigned int tags;
//     int isfloating;
//     int monitor_number;
// };

fn inline void _unused(void* x, ...){}
#define unused(...) _unused((void*) __VA_ARGS__)

// Layouts
fn void monocle(int monitor_index);
fn void tile(int monitor_index);

// Commands
fn void do_nothing(const ActionDetails *arg);
fn void focusmon(const ActionDetails *arg);
fn void focusstack(const ActionDetails *arg);
fn void killclient(const ActionDetails *arg);
fn void movemouse(const ActionDetails *arg);
fn void quit(const ActionDetails *arg);
fn void resizemouse(const ActionDetails *arg);
fn void toggle_layout(const ActionDetails *arg);
fn void setmfact(const ActionDetails *arg);
fn void sigstatusbar(const ActionDetails *arg);
fn void spawn_action(const ActionDetails *arg);
fn void spawn_dmenu(const ActionDetails *unused);
fn void spawn_brave(const ActionDetails *arg);
fn void spawn_firefox(const ActionDetails *arg);
fn void spawn_surf(const ActionDetails *arg);
fn void tag(const ActionDetails *arg);
fn void tagmon(const ActionDetails *arg);
fn void togglefloating(const ActionDetails *arg);
fn void toggletag(const ActionDetails *arg);
fn void toggleview(const ActionDetails *arg);
fn void resize_window(const ActionDetails *arg);
fn void move_vert(const ActionDetails *arg);
fn void move_horiz(const ActionDetails *arg);
fn void change_window_aspect_ratio(const ActionDetails *arg);
fn void view(const ActionDetails *arg);
fn void make_main_client(const ActionDetails *arg);

fn void push_mode_action(const ActionDetails* arg);
fn void pop_mode_action(const ActionDetails* arg);
// fn void reset_mode_action(const ActionDetails* arg);

/* Variables */
global const char broken[] = "broken";
global char status_text[256];
global int status_width;
global int statussig;
global pid_t statuspid = -1;
global int global_screen;
global int global_screen_width, global_screen_height;           /* X global_display screen geometry width, height */
global int global_bar_height;       /* bar geometry */
global int lrpad;            /* sum of left and right padding for text */
global int (*xerrorxlib)(Display *, XErrorEvent *);
global unsigned int numlockmask = 0;

global Atom wmatom[WMLast], netatom[NetLast];
global int global_running = 1;
global Cur cursor[CurLast];
global Display *global_display;
global Drw drw;
global int monitor_capacity; // capacity of all_monitors array
global Monitor *all_monitors; // , *selected_monitor;
global int selected_monitor;
global Window root, wmcheckwin;

global Mode mode_info[] = {
    [ModeNormal]      = { .name = NULL, },
    [ModeQuit]        = { .name = "Quit?" },
    [ModeBrowser]     = { .name = "Browser", },
    [ModeSurfBrowser] = { .name = "Surf", },
};

// Current mode tracking: I doubt that I will have more than 8 modes stacked
global int mode_stack[8];
global int mode_stack_top;

/* configuration, allows nested code to access above variables */
#include "config.h"

global XftColor *scheme_color_buffer;
global XftColor *scheme[ArrayLength(colors)];

/* compile-time check if all tags fit into an unsigned int bit array. */
struct NumTags { char limitexceeded[ArrayLength(tags) > 31 ? -1 : 1]; };

/* function implementations */
// fn void applyrules(Client *client) {
    // XClassHint ch = { NULL, NULL };

    /* rule matching */
    // client->isfloating = 0;
    // client->tags = 0;
    // XGetClassHint(global_display, client->window, &ch);
    // const char *class    = ch.res_class ? ch.res_class : broken;
    // const char *instance = ch.res_name  ? ch.res_name  : broken;

    // for (unsigned int i = 0; i < ArrayLength(rules); i++) {
    //     const Rule *rule = &rules[i];
    //     if ((!rule->title || strstr(client->name, rule->title))
    //     && (!rule->class || strstr(class, rule->class))
    //     && (!rule->instance || strstr(instance, rule->instance))) {
    //         client->isfloating = rule->isfloating;
    //         client->tags |= rule->tags;

    //         for(int monitor_index = 0; monitor_index < monitor_capacity; ++monitor_index) {
    //             Monitor *monitor = &all_monitors[monitor_index];
    //             if(monitor->is_valid && monitor->num == rule->monitor_number) {
    //                 client->monitor = monitor_index;
    //                 break;
    //             }
    //         }
    //     }
    // }

    // if (ch.res_class)
    //     XFree(ch.res_class);

    // if (ch.res_name)
    //     XFree(ch.res_name);

    // client->tags = client->tags & TagMask ? client->tags & TagMask : all_monitors[client->monitor].selected_tags;
// }

fn int applysizehints(Client *client, int *x, int *y, int *width, int *height, int interact) {
    int baseismin;

    /* set minimum possible */
    *width = Maximum(1, *width);
    *height = Maximum(1, *height);

    if (interact) {
        if (*x > global_screen_width)
            *x = global_screen_width - GappedClientWidth(client);
        if (*y > global_screen_height)
            *y = global_screen_height - GappedClientHeight(client);
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

    if (*height < global_bar_height)
        *height = global_bar_height;

    if (*width < global_bar_height)
        *width = global_bar_height;

    if (client->isfloating) {
        /* see last two sentences in ICCCM 4.1.2.3 */
        baseismin = client->base_width == client->min_width && client->base_height == client->min_height;
        if (!baseismin) { /* temporarily remove base dimensions */
            *width  -= client->base_width;
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

// fn void arrange_monitor(int monitor_index) {
//     Monitor* monitor = &all_monitors[monitor_index];
//     if(monitor->is_valid) {
//     }
// }

fn Client *nexttiled(Client *client) {
    for (; client && (client->isfloating || !IsVisible(client)); client = client->next);
    return client;
}

fn void configure(Client *client) {
    XConfigureEvent configure_event = {0};

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

fn void resizeclient(Client *client, int x, int y, int width, int height) {
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

fn void resize(Client *client, int x, int y, int width, int height, int interact) {
    if (applysizehints(client, &x, &y, &width, &height, interact))
        resizeclient(client, x, y, width, height);
}

fn void showhide(Client *client) {
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

fn void drawbar(int monitor_index) {
    int bar_height = global_bar_height;
    int bottom_bar_height = bar_height/10;
    int text_height = global_bar_height - bottom_bar_height;

    Monitor *monitor = &all_monitors[monitor_index];
    if (!monitor->showbar)
        return;

    int window_width = monitor->window_width;

    drw_rect(&drw, 0, 0, window_width, bar_height, scheme[SchemeNorm], 1, 1);

    /* draw status first so it can be overdrawn by tags later */
    if (monitor_index == selected_monitor) {
        /* status is only drawn on selected monitor */

        // char *s, *text;
        // char ch;
        // for (text = s = status_text; *s; s++) {
        //     if ((unsigned char)(*s) < ' ') {
        //         ch = *s;
        //         *s = '\0';
        //         status_width += TextWidth(text) - lrpad;
        //         *s = ch;
        //         text = s + 1;
        //     }
        // }

        status_width = TextWidth(status_text) - lrpad + 2;
        drw_text(&drw, window_width - status_width, 0, status_width, text_height, scheme[SchemeNorm], 0, status_text, 0);
    }

    // Create occupancy mask
    unsigned int occupied = 0, urg = 0;
    for (Client* client = monitor->clients; client; client = client->next) {
        occupied |= client->tags;
        if (client->isurgent)
            urg |= client->tags;
    }

    // Draw tags
    int x = 0;
    for (unsigned int i = 0; i < ArrayLength(tags); i++) {
        int tag_is_selected = monitor->selected_tags & (1 << i);
        if (occupied & (1 << i) || tag_is_selected) {
            int text_width = TextWidth(tags[i]);
            drw_text(&drw, x, 0, text_width, text_height, scheme[tag_is_selected ? SchemeSel : SchemeNorm], lrpad / 2, tags[i], urg & (1 << i));
            if(tag_is_selected) {
                drw_rect(&drw, x, bar_height - bottom_bar_height, text_width, bottom_bar_height, scheme[SchemeBar], 1, 0);
            }
            x += text_width;
        }
    }

    // Draw things after tags (current client name, mode, etc.)
    int width = window_width - status_width - x;
    if (width > bar_height) {
        int current_mode = mode_stack[mode_stack_top];

        // Maybe this should be (current_mode != ModeNormal)
        const char *current_mode_name = mode_info[current_mode].name;
        if (current_mode_name) {
            int text_width = TextWidth(current_mode_name);
            drw_text(&drw, x, 0, width, text_height, scheme[SchemeAppLaunch], lrpad / 2, current_mode_name, 0);
            x += text_width;

            drw_rect(&drw, x, 0, width, bar_height, scheme[SchemeNorm], 1, 1);
        } else if (monitor->selected_client) {
            drw_text(&drw, x, 0, width, text_height, scheme[SchemeNorm], lrpad / 2, monitor->selected_client->name, 0);
            if (monitor->selected_client->isfloating) {
                // Box to indicate floating window
                int boxw = drw.fonts->height / 6 + 2;
                int boxs = drw.fonts->height / 9;
                drw_rect(&drw, x + boxs, boxs, boxw, boxw, scheme[SchemeNorm], monitor->selected_client->isfixed, 0);
            }
        // } else {
        //     drw_rect(&drw, x, 0, width, bar_height, scheme[SchemeNorm], 1, 1);
        }
    }

    drw_map(&drw, monitor->barwin, 0, 0, window_width, bar_height);
}

fn void drawbars(void) {
    for (int monitor_index = 0; monitor_index < monitor_capacity; ++monitor_index) {
        if(all_monitors[monitor_index].is_valid) {
            drawbar(monitor_index);
        }
    }
}

fn void restack(int monitor_index) {
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

fn void arrange(int monitor_index) {
    if (Between(monitor_index, 0, monitor_capacity - 1)) {
        Monitor* monitor = &all_monitors[monitor_index];
        showhide(monitor->stack);
        layouts[monitor->selected_layout].arrange(monitor_index);
        restack(monitor_index);
    } else {
        for(int monitor_index = 0; monitor_index < monitor_capacity; ++monitor_index) {
            Monitor* monitor = &all_monitors[monitor_index];
            if(monitor->is_valid) {
                showhide(monitor->stack);
                layouts[monitor->selected_layout].arrange(monitor_index);
                restack(monitor_index);
            }
        }
    }
}

fn int next_valid_monitor(int start_index) {
    int result = start_index;
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

fn void attach(Client *client) {
    Monitor* monitor = &all_monitors[client->monitor];
    client->next = monitor->clients;
    monitor->clients = client;
}

fn void attachstack(Client *client) {
    Monitor* monitor = &all_monitors[client->monitor];
    client->next_in_stack = monitor->stack;
    monitor->stack = client;
}

fn Client *wintoclient(Window window) {
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

fn int getrootptr(int *x, int *y) {
    int di;
    unsigned int dui;
    Window dummy;

    return XQueryPointer(global_display, root, &dummy, &dummy, x, y, &di, &di, &dui);
}

fn int recttomon(int x, int y, int width, int height) {
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

fn int wintomon(Window window) {
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

// 
fn void updatenumlockmask(void) {
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

fn void grabbuttons(Client *client, int focused) {
    updatenumlockmask();
    {
        unsigned int i, j;
        unsigned int modifiers[] = { 0, LockMask, numlockmask, numlockmask|LockMask };
        XUngrabButton(global_display, AnyButton, AnyModifier, client->window);
        if (!focused)
            XGrabButton(global_display, AnyButton, AnyModifier, client->window, False,
                        ButtonMask, GrabModeSync, GrabModeSync, None, None);
        for (i = 0; i < ArrayLength(buttons); i++) {
            if (buttons[i].click == ClkClientWin) {
                for (j = 0; j < ArrayLength(modifiers); j++) {
                    XGrabButton(global_display, buttons[i].button,
                                buttons[i].mask | modifiers[j],
                                client->window, False, ButtonMask,
                                GrabModeAsync, GrabModeSync, None, None);
                }
            }
        }
    }
}

fn void grabkeys(void) {
    updatenumlockmask();
    {
        unsigned int modifiers[] = { 0, LockMask, numlockmask, numlockmask|LockMask };

        XUngrabKey(global_display, AnyKey, AnyModifier, root);

        int current_mode = mode_stack[mode_stack_top];
        Key* key_array = (Key*) keys[current_mode].content;
        int  length    = keys[current_mode].length;
        for(unsigned int key_index = 0; key_index < length; ++key_index) {
            KeyCode code = XKeysymToKeycode(global_display, key_array[key_index].keysym);
            if (code != 0) {
                for (unsigned int mod_index = 0; mod_index < ArrayLength(modifiers); mod_index++) {
                    XGrabKey(global_display, code, key_array[key_index].mod | modifiers[mod_index], root, True, GrabModeAsync, GrabModeAsync);
                }
            }
        }
    }
}

// 

fn void seturgent(Client *client, int urg) {
    XWMHints *wmh;

    client->isurgent = urg;
    if (!(wmh = XGetWMHints(global_display, client->window)))
        return;
    wmh->flags = urg ? (wmh->flags | XUrgencyHint) : (wmh->flags & ~XUrgencyHint);
    XSetWMHints(global_display, client->window, wmh);
    XFree(wmh);
}

fn void detachstack(Client *client) {
    Client **tc, *t;

    for (tc = &all_monitors[client->monitor].stack; *tc && *tc != client; tc = &(*tc)->next_in_stack);
    *tc = client->next_in_stack;

    if (client == all_monitors[client->monitor].selected_client) {
        for (t = all_monitors[client->monitor].stack; t && !IsVisible(t); t = t->next_in_stack);
        all_monitors[client->monitor].selected_client = t;
    }
}

fn int sendevent(Client *client, Atom proto) {
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

fn void setfocus(Client *client) {
    if (!client->neverfocus) {
        XSetInputFocus(global_display, client->window, RevertToPointerRoot, CurrentTime);
        XChangeProperty(global_display, root, netatom[NetActiveWindow],
                        XA_WINDOW, 32, PropModeReplace,
                        (unsigned char *) &(client->window), 1);
    }
    sendevent(client, wmatom[WMTakeFocus]);
}

fn void unfocus(Client *client, int setfocus) {
    // client can be NULL when no windows are visible
    if (!client) return;

    grabbuttons(client, 0);
    XSetWindowBorder(global_display, client->window, scheme[SchemeNorm][ColBorder].pixel);
    if (setfocus) {
        XSetInputFocus(global_display, root, RevertToPointerRoot, CurrentTime);
        XDeleteProperty(global_display, root, netatom[NetActiveWindow]);
    }
}

fn void focus(Client *client) {
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

fn void buttonpress(XEvent *event) {
    ActionDetails arg = {0};
    Client *client;
    XButtonPressedEvent *ev = &event->xbutton;

    /* focus monitor if necessary */
    int monitor_index = wintomon(ev->window);
    if (monitor_index >= 0 && monitor_index != selected_monitor) {
        unfocus(all_monitors[selected_monitor].selected_client, 1);
        selected_monitor = monitor_index;
        focus(NULL);
    }

    unsigned int click = ClkRootWin;
    Monitor *monitor = &all_monitors[selected_monitor];
    if (ev->window == monitor->barwin) {
        unsigned int i = 0, x = 0;

        int occupied = 0;
        for (client = all_monitors[monitor_index].clients; client; client = client->next) {
            occupied |= client->tags;
        }

        do {
            int tag_mask = (1 << i);
            if (occupied & tag_mask || tag_mask & monitor->selected_tags & tag_mask) { 
                x += TextWidth(tags[i]);
            }
        } while (ev->x >= x && ++i < ArrayLength(tags));

        if (i < ArrayLength(tags)) {
            click = ClkTagBar;
            arg.ui = 1 << i;
        } else if (ev->x > all_monitors[selected_monitor].window_width - status_width) {
            x = all_monitors[selected_monitor].window_width - status_width;
            click = ClkStatusText;
            statussig = 0;
            char* s;
            for (char *text = s = status_text; *s && x <= ev->x; s++) {
                if ((unsigned char)(*s) < ' ') {
                    char ch = *s;
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

    for (int i = 0; i < ArrayLength(buttons); i++) {
        if (click == buttons[i].click &&
            buttons[i].func &&
            buttons[i].button == ev->button &&
            CleanMask(buttons[i].mask) == CleanMask(ev->state)) {
            buttons[i].func((click == ClkTagBar && buttons[i].arg.i == 0) ? &arg : &buttons[i].arg);
            break;
        }
    }
}

fn void cleanup_monitor(int monitor_index) {
    Monitor *monitor = &all_monitors[monitor_index];


    XUnmapWindow(global_display, monitor->barwin);
    XDestroyWindow(global_display, monitor->barwin);

    Monitor null_monitor = {0};
    *monitor = null_monitor;
}

fn int createmon(void) {
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

fn void detach(Client *client) {
    Client **tc;

    for (tc = &all_monitors[client->monitor].clients; *tc && *tc != client; tc = &(*tc)->next);
    *tc = client->next;
}

fn int dirtomon(int dir) {
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

fn void set_current_monitor(int monitor_index) {
    unfocus(all_monitors[selected_monitor].selected_client, 0);
    selected_monitor = monitor_index;
    focus(NULL);
}

fn Atom getatomprop(Client *client, Atom prop) {
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

fn pid_t getstatusbarpid() {
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

fn long getstate(Window window) {
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

fn int gettextprop(Window window, Atom atom, char *text, unsigned int size) {
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

fn int xerrordummy(Display *display, XErrorEvent *ee) {
    return 0;
}

/* There's no way to check accesses to destroyed windows, thus those cases are
 * ignored (especially on UnmapNotify's). Other types of errors call Xlibs
 * default error handler, which may call exit. */
fn int xerror(Display *display, XErrorEvent *ee) {
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

fn void updatetitle(Client *client) {
    if (!gettextprop(client->window, netatom[NetWMName], client->name, sizeof(client->name)))
        gettextprop(client->window, XA_WM_NAME, client->name, sizeof(client->name));
    if (client->name[0] == '\0') /* hack to mark broken clients */
        strcpy(client->name, broken);
}

fn void setfullscreen(Client *client, int fullscreen) {
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

fn void updatewindowtype(Client *client) {
    Atom state = getatomprop(client, netatom[NetWMState]);
    Atom wtype = getatomprop(client, netatom[NetWMWindowType]);

    if (state == netatom[NetWMFullscreen])
        setfullscreen(client, 1);
    if (wtype == netatom[NetWMWindowTypeDialog])
        client->isfloating = 1;
}

fn void updatesizehints(Client *client) {
    long msize;
    XSizeHints size;

    if (!XGetWMNormalHints(global_display, client->window, &size, &msize)) {
        /* size is uninitialized, ensure that size.flags aren't used */
        size.flags = PSize;
    }

    if (size.flags & PBaseSize) {
        client->base_width = size.base_width;
        client->base_height = size.base_height;
    } else if (size.flags & PMinSize) {
        client->base_width = size.min_width;
        client->base_height = size.min_height;
    } else {
        client->base_width = client->base_height = 0;
    }

    if (size.flags & PResizeInc) {
        client->inc_width = size.width_inc;
        client->inc_height = size.height_inc;
    } else {
        client->inc_width = client->inc_height = 0;
    }

    if (size.flags & PMaxSize) {
        client->max_width = size.max_width;
        client->max_height = size.max_height;
    } else {
        client->max_width = client->max_height = 0;
    }

    if (size.flags & PMinSize) {
        client->min_width = size.min_width;
        client->min_height = size.min_height;
    } else if (size.flags & PBaseSize) {
        client->min_width = size.base_width;
        client->min_height = size.base_height;
    } else {
        client->min_width = client->min_height = 0;
    }

    if (size.flags & PAspect) {
        client->min_aspect = (float)size.min_aspect.y / size.min_aspect.x;
        client->max_aspect = (float)size.max_aspect.x / size.max_aspect.y;
    } else {
        client->max_aspect = client->min_aspect = 0.0;
    }

    client->isfixed = (client->max_width && client->max_height && client->max_width == client->min_width && client->max_height == client->min_height);
}

fn void updatewmhints(Client *client) {
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

fn void setclientstate(Client *client, long state) {
    long data[] = { state, None };

    XChangeProperty(global_display, client->window, wmatom[WMState], wmatom[WMState], 32,
                    PropModeReplace, (unsigned char *)data, 2);
}

fn void manage(Window window, XWindowAttributes *wa) {
    Client *client, *t = NULL;
    Window trans = None;
    XWindowChanges wc;

    client = ecalloc(1, sizeof(Client));
    client->window = window;

    /* geometry */
    client->x      = client->oldx = wa->x;
    client->y      = client->oldy = wa->y;
    client->width  = client->old_width = wa->width;
    client->height = client->old_height = wa->height;
    client->old_border_width = wa->border_width;

    updatetitle(client);
    if (XGetTransientForHint(global_display, window, &trans) && (t = wintoclient(trans))) {
        client->monitor = t->monitor;
        client->tags = t->tags;
    } else {
        client->monitor = selected_monitor;
        // applyrules(client);
        //
        // NOTE: This was the last line of applyrules
        int suggested_tags = client->tags & TagMask;
        client->tags = suggested_tags ? suggested_tags : all_monitors[selected_monitor].selected_tags;
    }

    Monitor *monitor = &all_monitors[client->monitor];
    if (client->x + GappedClientWidth(client) > monitor->screen_x + monitor->screen_width)
        client->x = monitor->screen_x + monitor->screen_width - GappedClientWidth(client);
    if (client->y + GappedClientHeight(client) > monitor->screen_y + monitor->screen_height)
        client->y = monitor->screen_y + monitor->screen_height - GappedClientHeight(client);
    client->x = Maximum(client->x, monitor->screen_x);
    /* only fix client y-offset, if the client center might cover the bar */
    client->y = Maximum(client->y, ((monitor->bar_height == monitor->screen_y) && (client->x + (client->width / 2) >= monitor->window_x)
                                    && (client->x + (client->width / 2) < monitor->window_x + monitor->window_width)) ? global_bar_height : monitor->screen_y);
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
    XMoveResizeWindow(global_display, client->window, client->x + 2 * global_screen_width, client->y, client->width, client->height); /* some windows require this */
    setclientstate(client, NormalState);

    if (client->monitor == selected_monitor)
        unfocus(all_monitors[selected_monitor].selected_client, 0);

    monitor->selected_client = client;
    arrange(client->monitor);
    XMapWindow(global_display, client->window);
    focus(NULL);
}

// Layouts
fn void monocle(int monitor_index) {
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

fn void tile(int monitor_index) {
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
        float f_fact = ((float)monitor->mfact) / 100.0f;
        unsigned int master_width = monitor->window_width * f_fact;

        // draw master window on left
        resize(client,
               monitor->window_x + monitor->window_width - master_width,
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
                   monitor->window_x, //  + master_width,
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

fn void sendmon(Client *client, int monitor_index) {
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

fn void pop_client(Client *client) {
    detach(client);
    attach(client);
    focus(client);
    arrange(client->monitor);
}

fn void unmanage(Client *client, int destroyed) {
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

    // update client list
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

    arrange(monitor);
}

fn void updatebars(void) {
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
            monitor->barwin = XCreateWindow(global_display, root, monitor->window_x, monitor->bar_height, monitor->window_width, global_bar_height, 0, DefaultDepth(global_display, global_screen),
                                            CopyFromParent, DefaultVisual(global_display, global_screen),
                                            CWOverrideRedirect|CWBackPixmap|CWEventMask, &wa);
            XDefineCursor(global_display, monitor->barwin, cursor[CurNormal].cursor);
            XMapRaised(global_display, monitor->barwin);
            XSetClassHint(global_display, monitor->barwin, &ch);
        }
    }
}

fn void updatebarpos(int monitor_index) {
    Monitor *monitor = &all_monitors[monitor_index];

    monitor->window_y = monitor->screen_y;
    monitor->window_height = monitor->screen_height;
    if (monitor->showbar) {
        monitor->window_height -= global_bar_height;
        monitor->bar_height = monitor->topbar ? monitor->window_y : monitor->window_y + monitor->window_height;
        monitor->window_y = monitor->topbar ? monitor->window_y + global_bar_height : monitor->window_y;
    } else
        monitor->bar_height = -global_bar_height;
}

fn int updategeom(void) {
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
        for (i = 0, j = 0; i < num_screens; i++) {
            int isuniquegeom = 1;
            size_t n = j;
            XineramaScreenInfo *screen_info = &info[i];

            while (n--) {
                if(unique[n].x_org == screen_info->x_org && unique[n].y_org  == screen_info->y_org
                   && unique[n].width == screen_info->width && unique[n].height == screen_info->height) {
                    isuniquegeom = 1;
                    break;
                }
            }

            if (isuniquegeom) {
                memcpy(&unique[j++], &info[i], sizeof(XineramaScreenInfo));
            }
        }
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

                cleanup_monitor(monitor_index);
            }
        }
        free(unique);
    } else
#endif /* XINERAMA */
    { /* default monitor setup */
        if (!all_monitors) {
            createmon();
        }

        int first_monitor_index = next_valid_monitor(0);
        Monitor *first_monitor = &all_monitors[first_monitor_index];
        if (first_monitor->screen_width != global_screen_width || first_monitor->screen_height != global_screen_height) {
            dirty = 1;
            first_monitor->screen_width  = first_monitor->window_width = global_screen_width;
            first_monitor->screen_height = first_monitor->window_height = global_screen_height;

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

fn void updatestatus(void) {
    if (!gettextprop(root, XA_WM_NAME, status_text, sizeof(status_text))) {
        strcpy(status_text, "dwm-"VERSION);
        status_width = TextWidth(status_text) - lrpad + 2;
    } else {
        char *text, *s, ch;

        status_width  = 0;
        for (text = s = status_text; *s; s++) {
            if ((unsigned char)(*s) < ' ') {
                ch = *s;
                *s = '\0';
                status_width += TextWidth(text) - lrpad;
                *s = ch;
                text = s + 1;
            }
        }
        status_width += TextWidth(text) - lrpad + 2;
    }
    drawbar(selected_monitor);
}

// Event functions
fn void configurerequest(XEvent *event) {
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

fn void expose(XEvent *event) {
    int monitor_index;
    XExposeEvent *ev = &event->xexpose;

    if (ev->count == 0 && (monitor_index = wintomon(ev->window)))
        drawbar(monitor_index);
}

fn void maprequest(XEvent *event) {
    static XWindowAttributes wa;
    XMapRequestEvent *ev = &event->xmaprequest;

    if (!XGetWindowAttributes(global_display, ev->window, &wa))
        return;

    if (wa.override_redirect)
        return;

    if (!wintoclient(ev->window))
        manage(ev->window, &wa);
}

// Mode control

fn void push_mode(int mode_info_index) {
    if(mode_stack_top < ArrayLength(mode_stack)) {
        ++mode_stack_top;
        mode_stack[mode_stack_top] = mode_info_index;
        grabkeys();
        arrange(selected_monitor);
    }
}

fn void pop_mode() {
    if(mode_stack_top > 0) {
        --mode_stack_top;
        grabkeys();
    }
}

fn void reset_mode() {
    mode_stack_top = 0;
    grabkeys();
}

// Process control
static void closepipe(int pipe[2]) {
    close(pipe[0]);
    close(pipe[1]);
}

typedef struct ChildProcess ChildProcess;
struct ChildProcess {
    pid_t pid;
    int std_output;
    int std_input;
};

fn ChildProcess spawn(char const **command, int from_command) {
    ChildProcess result = {0};
    int wm_to_process[2] = {0};
    int process_to_wm[2] = {0};

    pipe(wm_to_process);
    pipe(process_to_wm);

    pid_t process_bar_pid = fork();
    if(process_bar_pid == 0) {
        close(wm_to_process[1]);
        close(process_to_wm[0]);

        if (from_command) {
            if (global_display)
                close(ConnectionNumber(global_display));
            setsid();
        }
        execvp(command[0], (char **)command);
        fprintf(stderr, "dwm: execvp %s", command[0]);
        perror(" failed");
        exit(EXIT_SUCCESS);
    }

    if(process_bar_pid < 0) {
        // Error when forking
        closepipe(wm_to_process);
        closepipe(process_to_wm);
    } else {
        result.std_output  = process_to_wm[0];
        result.std_input = wm_to_process[1];
        close(wm_to_process[0]);
        close(process_to_wm[1]);
    }

    result.pid = process_bar_pid;

    return result;
}

fn void spawn_and_reset_mode(char const **arg) {
    reset_mode();
    spawn(arg, 1);
    focus(NULL);
}

// Commands
fn void do_nothing(const ActionDetails *arg) {
    // This is just a test command
    // printf("Doing nothing...except printing\n");
}

fn void make_main_client(const ActionDetails *arg) {
    Client *selected_client = all_monitors[selected_monitor].selected_client;

    if(selected_client && selected_client->isfloating)
        return;

    if(selected_client == nexttiled(all_monitors[selected_monitor].clients))
        if(!selected_client || !(selected_client = nexttiled(selected_client->next)))
            return;

    pop_client(selected_client);
}

fn void movemouse(const ActionDetails *arg) {
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

fn void quit(const ActionDetails *arg) {
    global_running = 0;
}

fn void resizemouse(const ActionDetails *arg) {
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

// void setlayout(const ActionDetails *arg) {
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

fn void toggle_layout(const ActionDetails *arg) {
    all_monitors[selected_monitor].selected_layout ^= 1;
    if (all_monitors[selected_monitor].selected_client) {
        arrange(selected_monitor);
    } else {
        drawbar(selected_monitor);
    }
}

fn void setmfact(const ActionDetails *arg) {
    int new_fact = all_monitors[selected_monitor].mfact + arg->i;

    if(!Between(new_fact, 5, 95)) {
        return;
    }

    all_monitors[selected_monitor].mfact = new_fact;
    arrange(selected_monitor);
}

fn void sigstatusbar(const ActionDetails *arg) {
    union sigval sv;

    if (!statussig)
        return;

    sv.sival_int = arg->i;
    if ((statuspid = getstatusbarpid()) <= 0)
        return;

    sigqueue(statuspid, SIGRTMIN + statussig, sv);
}

fn void spawn_action(const ActionDetails *arg) {
    spawn((char const **)arg->v, 1);
}

fn void spawn_dmenu(const ActionDetails *unused) {
    char dmenu_monitor[] = {'0' + all_monitors[selected_monitor].num, 0 };
    char const *dmenucmd[] = {
        "dmenu_run",
        "-m",  dmenu_monitor,
        "-fn", dmenufont,
        "-nb", col_gray1,
        "-nf", col_gray3,
        "-sb", col_cyan,
        "-sf", col_gray4,
        NULL
    };

    spawn(dmenucmd, 1);
}

fn void spawn_brave(const ActionDetails *arg) {
    char const* browser_command[] = { "brave-browser", arg->v, NULL };
    spawn_and_reset_mode(browser_command);
}

fn void spawn_firefox(const ActionDetails *arg) {
    char const* browser_command[] = { "firefox", "-P", arg->v, NULL };
    spawn_and_reset_mode(browser_command);
}

fn void spawn_surf(const ActionDetails *arg) {
    char const* surf_command[] = {
        "tabbed", "-r", "5", // 5 is the index of the argument to switch out with the window id
        "firejail", "--noprofile", "--hosts-file=~/.surf/blocked-hosts.txt",
        "surf", "-e", "", "-c", arg->v, "~/.surf/new_tab_page.html",
        NULL
    };

    spawn_and_reset_mode(surf_command);
}

fn void tag(const ActionDetails *arg) {
    Client *selected_client = all_monitors[selected_monitor].selected_client;
    if (selected_client && arg->ui & TagMask) {
        selected_client->tags = arg->ui & TagMask;
        focus(NULL);
        arrange(selected_monitor);
    }
}

fn void tagmon(const ActionDetails *arg) {
    Client *selected_client = all_monitors[selected_monitor].selected_client;
    if (!selected_client)
        return;

    int next_monitor = dirtomon(arg->i);
    if(next_monitor != selected_monitor) {
        sendmon(selected_client, next_monitor);
    }
}

fn void togglefloating(const ActionDetails *arg) {
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

fn void toggletag(const ActionDetails *arg) {
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

fn void toggleview(const ActionDetails *arg) {
    unsigned int newtagset = all_monitors[selected_monitor].selected_tags ^ (arg->ui & TagMask);
    if (newtagset) {
        all_monitors[selected_monitor].selected_tags = newtagset;
        focus(NULL);
        arrange(selected_monitor);
    }
}

fn void resize_window(const ActionDetails *arg) {
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

fn void move_vert(const ActionDetails *arg) {
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

fn void move_horiz(const ActionDetails *arg) {
    Client *selected_client = all_monitors[selected_monitor].selected_client;

    int move_amount = arg->i > 0 ? 5 : -5;
    if(selected_client && selected_client->isfloating) {
        resize(selected_client, selected_client->x + move_amount, selected_client->y, selected_client->width, selected_client->height, 0);
    }
}

fn void change_window_aspect_ratio(const ActionDetails *arg) {
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

fn void killclient(const ActionDetails *arg) {
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

fn void view(const ActionDetails *arg) {
    int new_tags = arg->ui & TagMask;
    Monitor *monitor = &all_monitors[selected_monitor];
    if (new_tags != monitor->selected_tags) {
        if (new_tags)
            monitor->selected_tags = new_tags;

        focus(NULL);
        arrange(selected_monitor);
    }
}

fn void focusmon(const ActionDetails *arg) {
    int monitor_index;
    if ((monitor_index = dirtomon(arg->i)) == selected_monitor)
        return;

    set_current_monitor(monitor_index);
}

fn void focusstack(const ActionDetails *arg) {
    Client *client = NULL, *i;

    Monitor *monitor = &all_monitors[selected_monitor];
    if (monitor->selected_client && !monitor->selected_client->isfullscreen) {
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
}

fn void push_mode_action(const ActionDetails *arg) {
    push_mode(arg->i);
}

fn void pop_mode_action(const ActionDetails *arg) {
    pop_mode();
}

// fn void reset_mode_action(const ActionDetails *arg) {
//     reset_mode();
// }

// void rotate(const ActionDetails *arg) {
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

// Status bar
fn ChildProcess start_status_bar(void) {
    // If the status bar handles its own click events, why do we need wm_to_status?
    // wm_to_status tells the status bar which tags to show
    // status_to_wm tells dwm what was clicked
    char const* args[] = { STATUSBAR, NULL };
    ChildProcess result = spawn(args, 0);
    return result;
}

/* Startup Error handler to check if another window manager is already running. */
fn int xerrorstart(Display *display, XErrorEvent *ee) {
    die("dwm: another window manager is already running");
    return -1;
}

fn void sigchld(int unused) {
    // TODO: Handle dead app launcher or status bar
    if (signal(SIGCHLD, sigchld) == SIG_ERR)
        die("can't install SIGCHLD handler:");

    while (0 < waitpid(-1, NULL, WNOHANG)) {
        // 
    }
}

fn void setup(Display *display) {
    int i;
    XSetWindowAttributes wa;
    Atom utf8string;

    /* clean up any zombies immediately */
    sigchld(0);

    /* init screen */
    global_screen = DefaultScreen(display);
    global_screen_width = DisplayWidth(display, global_screen);
    global_screen_height = DisplayHeight(display, global_screen);
    root = RootWindow(display, global_screen);
    drw_init(&drw, display, global_screen, root, global_screen_width, global_screen_height);

    if (!drw_fontset_create(&drw, fonts, ArrayLength(fonts)))
        die("no fonts could be loaded.");

    lrpad = drw.fonts->height;
    global_bar_height = drw.fonts->height + 10;
    updategeom();
    /* init atoms */
    utf8string                     = XInternAtom(display, "UTF8_STRING", False);
    wmatom[WMProtocols]            = XInternAtom(display, "WM_PROTOCOLS", False);
    wmatom[WMDelete]               = XInternAtom(display, "WM_DELETE_WINDOW", False);
    wmatom[WMState]                = XInternAtom(display, "WM_STATE", False);
    wmatom[WMTakeFocus]            = XInternAtom(display, "WM_TAKE_FOCUS", False);
    netatom[NetActiveWindow]       = XInternAtom(display, "_NET_ACTIVE_WINDOW", False);
    netatom[NetSupported]          = XInternAtom(display, "_NET_SUPPORTED", False);
    netatom[NetWMName]             = XInternAtom(display, "_NET_WM_NAME", False);
    netatom[NetWMState]            = XInternAtom(display, "_NET_WM_STATE", False);
    netatom[NetWMCheck]            = XInternAtom(display, "_NET_SUPPORTING_WM_CHECK", False);
    netatom[NetWMFullscreen]       = XInternAtom(display, "_NET_WM_STATE_FULLSCREEN", False);
    netatom[NetWMWindowType]       = XInternAtom(display, "_NET_WM_WINDOW_TYPE", False);
    netatom[NetWMWindowTypeDialog] = XInternAtom(display, "_NET_WM_WINDOW_TYPE_DIALOG", False);
    netatom[NetClientList]         = XInternAtom(display, "_NET_CLIENT_LIST", False);

    /* init cursors */
    cursor[CurNormal]              = drw_cur_create(&drw, XC_left_ptr);
    cursor[CurResize]              = drw_cur_create(&drw, XC_sizing);
    cursor[CurMove]                = drw_cur_create(&drw, XC_fleur);

    /* init appearance */
    scheme_color_buffer = ecalloc(numColorsInSet * ArrayLength(colors), sizeof(XftColor));
    for (i = 0; i < ArrayLength(colors); i++) {
        XftColor *xft_color = &scheme_color_buffer[i * 3];
        drw_scm_create(&drw, &colors[i], xft_color);
        scheme[i] = xft_color;
    }

    /* init bars */
    unused(start_status_bar);
    // start_status_bar();
    updatebars();
    updatestatus();
    /* supporting window for NetWMCheck */
    wmcheckwin = XCreateSimpleWindow(display, root, 0, 0, 1, 1, 0, 0, 0);
    XChangeProperty(display, wmcheckwin, netatom[NetWMCheck], XA_WINDOW, 32,
                    PropModeReplace, (unsigned char *) &wmcheckwin, 1);
    XChangeProperty(display, wmcheckwin, netatom[NetWMName], utf8string, 8,
                    PropModeReplace, (unsigned char *) "dwm", 3);
    XChangeProperty(display, root, netatom[NetWMCheck], XA_WINDOW, 32,
                    PropModeReplace, (unsigned char *) &wmcheckwin, 1);
    /* EWMH support per view */
    XChangeProperty(display, root, netatom[NetSupported], XA_ATOM, 32,
                    PropModeReplace, (unsigned char *) netatom, NetLast);
    XDeleteProperty(display, root, netatom[NetClientList]);
    /* select events */
    wa.cursor = cursor[CurNormal].cursor;
    wa.event_mask = SubstructureRedirectMask|SubstructureNotifyMask
      |ButtonPressMask|PointerMotionMask|EnterWindowMask
      |LeaveWindowMask|StructureNotifyMask|PropertyChangeMask;
    XChangeWindowAttributes(display, root, CWEventMask|CWCursor, &wa);
    XSelectInput(display, root, wa.event_mask);
    grabkeys();
    focus(NULL);
}

int main(int argc, char *argv[]) {
    if (argc == 2 && !strcmp("-v", argv[1])) {
        die("dwm-"VERSION);
    } else if (argc != 1) {
        die("usage: dwm [-v]");
    }

    if (!setlocale(LC_CTYPE, "") || !XSupportsLocale())
        fputs("warning: no locale support\n", stderr);

    global_display = XOpenDisplay(NULL);
    if (!global_display)
        die("dwm: cannot open global_display");

    // check other wm
    xerrorxlib = XSetErrorHandler(xerrorstart);
    /* this causes an error if some other window manager is running */
    XSelectInput(global_display, DefaultRootWindow(global_display), SubstructureRedirectMask);
    XSync(global_display, False);
    XSetErrorHandler(xerror);
    XSync(global_display, False);

    setup(global_display);

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

    /* main event loop */
    XEvent event;
    XSync(global_display, False);
    while (global_running && !XNextEvent(global_display, &event)) {
        switch (event.type) {
            case ButtonPress: {
                buttonpress(&event);
                break;
            }

            case ClientMessage: {
                XClientMessageEvent *cme = &event.xclient;
                Client *client = wintoclient(cme->window);

                if (client) {
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
                    dirty = (global_screen_width != ev->width || global_screen_height != ev->height);
                    global_screen_width = ev->width;
                    global_screen_height = ev->height;

                    if (updategeom() || dirty) {
                        drw_resize(&drw, global_screen_width, global_bar_height);
                        updatebars();
                        for (int monitor_index = 0; monitor_index < monitor_capacity; ++monitor_index) {
                            Monitor *monitor = &all_monitors[monitor_index];
                            if(monitor->is_valid) {
                                for (client = monitor->clients; client; client = client->next) {
                                    if (client->isfullscreen) {
                                        resizeclient(client, monitor->screen_x, monitor->screen_y, monitor->screen_width, monitor->screen_height);
                                    }
                                }
                                XMoveResizeWindow(global_display, monitor->barwin, monitor->window_x, monitor->bar_height, monitor->window_width, global_bar_height);
                            }
                        }
                        focus(NULL);
                        arrange(-1);
                    }
                }
                break;
            }

            case DestroyNotify: {
                Client *client;
                XDestroyWindowEvent *ev = &event.xdestroywindow;

                if ((client = wintoclient(ev->window)))
                    unmanage(client, 1);
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
                int current_mode = mode_stack[mode_stack_top];
                Key* key_array = (Key*)keys[current_mode].content;
                int length = keys[current_mode].length;

                for(; i < length; i++) {
                    Key* key = &key_array[i];
                    if (keysym == key->keysym &&
                        CleanMask(key->mod) == CleanMask(ev->state)) {
                        key->func(&(key->arg));
                        break;
                    }
                }

                break;
            }

            case MappingNotify: {
                XMappingEvent *ev = &event.xmapping;

                XRefreshKeyboardMapping(ev);
                if (ev->request == MappingKeyboard) {
                    grabkeys();
                }

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
    ActionDetails a = { .ui = ~0 };

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
            cleanup_monitor(monitor_index);
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

