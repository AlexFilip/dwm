/* See LICENSE file for copyright and license details. */

/* appearance */
static const unsigned int borderpx  = 2;        /* border pixel of windows */
static const unsigned int snap      = 32;       /* snap pixel */
static const int showbar            = 1;        /* 0 means no bar */
static const int topbar             = 1;        /* 0 means bottom bar */
static int gap_size                 = 10;       /* gaps between windows */


static const char *fonts[]     = { "monospace:size=12", "Hack:size=11" };
static const char dmenufont[]  = "monospace:size=12";
static const char col_gray1[]  = "#222222";
static const char col_gray2[]  = "#444444";
static const char col_gray3[]  = "#bbbbbb";
static const char col_gray4[]  = "#eeeeee";
static const char col_cyan[]   = "#005577";
// static const char col_bar[]    = "#3630f2";
static const char col_app_bg[] = "#11750a";

static const ColorSet colors[]      = {
    [SchemeNorm]      = { .fg = col_gray3, .bg = col_gray1,  .border = col_gray2 },
    [SchemeSel]       = { .fg = col_gray4, .bg = col_gray2,  .border = col_cyan  },
    [SchemeBar]       = { .fg = col_cyan,  .bg = col_gray1,  .border = col_gray2 },
    [SchemeAppLaunch] = { .fg = col_gray3, .bg = col_app_bg, .border = col_gray2 },
};

/* tagging */
static const char *tags[] = { "Main", ">_", "3", "4", "5", "6", "7", "8", "9" };

// static const Rule rules[] = {
//     /* xprop(1):
//      *    WM_CLASS(STRING) = instance, class
//      *    WM_NAME(STRING) = title
//      */
//     // { .class = "Gimp",    .instance = NULL, .title = NULL, .tags = 0,      .isfloating = 1, .monitor_number = -1 },
//     // { .class = "Firefox", .instance = NULL, .title = NULL, .tags = 1 << 8, .isfloating = 0, .monitor_number = -1 },
// };

/* layout(s) */
static const int mfact = 55; /* factor of master area size [5..95] [0.05..0.95] */

enum {
    tile_index,
    monocle_index,
};

static const Layout layouts[] = {
    // NOTE: NULL is not a supported state in this fork of dwm
    [tile_index]    = { .arrange = tile },    /* first entry is default */
    [monocle_index] = { .arrange = monocle },
};

/* key definitions */
#ifdef DEBUG
// Use Alt
#define MODKEY Mod1Mask
#else
// Use Windows/Command/etc.
#define MODKEY Mod4Mask
#endif

#define TAGKEYS(KEY) \
    { MODKEY,                       KEY,      view,           { .ui = 1 << (KEY - XK_1) } }, \
    { MODKEY|ControlMask,           KEY,      toggleview,     { .ui = 1 << (KEY - XK_1) } }, \
    { MODKEY|ShiftMask,             KEY,      tag,            { .ui = 1 << (KEY - XK_1) } }, \
    { MODKEY|ControlMask|ShiftMask, KEY,      toggletag,      { .ui = 1 << (KEY - XK_1) } }

// TODO: Make all commands use this
#define Command(...) { .v = (const char*[]){ __VA_ARGS__, NULL } }

/* helper for spawning shell commands in the pre dwm-5.0 fashion */
#define ShellCommand(cmd) { .v = (const char*[]){ "/bin/sh", "-c", cmd, NULL } }

// #define STATUSBAR "dwmblocks"
#define STATUSBAR "spoon"

static const char* volume_up[]   = { "/usr/bin/pactl", "set-sink-volume", "@DEFAULT_SINK@",   "+5%",    NULL };
static const char* volume_down[] = { "/usr/bin/pactl", "set-sink-volume", "@DEFAULT_SINK@",   "-5%",    NULL };
static const char* volume_mute[] = { "/usr/bin/pactl", "set-sink-mute",   "@DEFAULT_SINK@",   "toggle", NULL };
static const char* mic_mute[]    = { "/usr/bin/pactl", "set-source-mute", "@DEFAULT_SOURCE@", "toggle", NULL };

/* commands */
static char const term[] = "st";
#define TermCommand(...) { .v = (const char*[]){ term, __VA_ARGS__, NULL } }

static const Key normal_mode_keys[] = {
    /* modifier                     key        function          argument */
    { MODKEY,                       XK_space,  spawn_dmenu,     {0} },
    { MODKEY,                       XK_t,      spawn_action,    TermCommand(NULL) },
    { MODKEY,                       XK_e,      spawn_action,    TermCommand("nvim") },

    { MODKEY,                       XK_p,      spawn_action,    TermCommand("htop") },
    { MODKEY,                       XK_d,      spawn_action,    Command("thunar")   },

    { MODKEY,                       XK_f,      toggle_layout,    {0} },

    { MODKEY,                       XK_b,      push_mode_action, {.i = ModeBrowser} },
    { MODKEY,                       XK_s,      push_mode_action, {.i = ModeSurfBrowser} },

    { MODKEY,                       XK_h,      focusstack,       {.i = +1} },
    { MODKEY,                       XK_l,      focusstack,       {.i = -1} },
    { MODKEY,                       XK_j,      setmfact,         {.i = +5} },
    { MODKEY,                       XK_k,      setmfact,         {.i = -5} },

    // Floating windows
    { MODKEY,                       XK_slash,  togglefloating,   {0} },
    { MODKEY|ShiftMask,             XK_j,      move_vert,        {.i = +1} },
    { MODKEY|ShiftMask,             XK_k,      move_vert,        {.i = -1} },
    { MODKEY|ShiftMask,             XK_h,      move_horiz,       {.i = -1} },
    { MODKEY|ShiftMask,             XK_l,      move_horiz,       {.i = +1} },

    // 
    { MODKEY,                       XK_Return, make_main_client, {0} },
    { MODKEY,                       XK_Tab,    view,             {0} },
    { MODKEY,                       XK_w,      killclient,       {0} },

    { MODKEY,                       XK_comma,  focusmon,         {.i = -1} },
    { MODKEY,                       XK_period, focusmon,         {.i = +1} },

    { MODKEY|ShiftMask,             XK_comma,  tagmon,           {.i = -1} },
    { MODKEY|ShiftMask,             XK_period, tagmon,           {.i = +1} },

    { MODKEY,                       XK_0,      view,             {.ui = ~0} },
    { MODKEY|ShiftMask,             XK_0,      tag,              {.ui = ~0} },

    // Tag switching
    TAGKEYS(XK_1),
    TAGKEYS(XK_2),
    TAGKEYS(XK_3),
    TAGKEYS(XK_4),
    TAGKEYS(XK_5),
    TAGKEYS(XK_6),
    TAGKEYS(XK_7),
    TAGKEYS(XK_8),
    TAGKEYS(XK_9),

    { MODKEY|ShiftMask,             XK_q,      push_mode_action,               { .i = ModeQuit } },
    { MODKEY,                       XK_y,      resize_window,                  { .i = +1 } },
    { MODKEY|ShiftMask,             XK_y,      resize_window,                  { .i = -1 } },
    { MODKEY|ControlMask,           XK_y,      change_window_aspect_ratio,     { .i = -1 } },
    { MODKEY|ControlMask|ShiftMask, XK_y,      change_window_aspect_ratio,     { .i = +1 } },

    // Screenshots
    // TODO: Make a proper take_screenshot button that just takes the screenshot depending on the option passed in.
    //       The current problem is the current window. I need to check if it's the same as client->window.
    { MODKEY,             XK_a, spawn_action, ShellCommand("maim                                     $HOME/screenshots/$(date +%Y-%m-%d_%H:%M:%S).png") },
    { MODKEY|ControlMask, XK_a, spawn_action, ShellCommand("maim --window $(xdotool getactivewindow) $HOME/screenshots/$(date +%Y-%m-%d_%H:%M:%S).png") },
    { MODKEY|ShiftMask,   XK_a, spawn_action, ShellCommand("maim --select                            $HOME/screenshots/$(date +%Y-%m-%d_%H:%M:%S).png") },

    // Volume controls
    { 0, XF86XK_AudioRaiseVolume,   spawn_action, { .v = volume_up   } },
    { 0, XF86XK_AudioLowerVolume,   spawn_action, { .v = volume_down } },
    { 0, XF86XK_AudioMute,          spawn_action, { .v = volume_mute } },
    { 0, XF86XK_AudioMicMute,       spawn_action, { .v = mic_mute    } },

    // Screen brightness controls
    // TODO: Find out how to modify this from C
    { 0, XF86XK_MonBrightnessUp,   spawn_action, ShellCommand("~/bin/backlight +1") },
    { 0, XF86XK_MonBrightnessDown, spawn_action, ShellCommand("~/bin/backlight -1") },
};

static const Key quit_mode_keys[] = {
    { MODKEY, XK_Escape, pop_mode_action, {0} },
    {      0, XK_Escape, pop_mode_action, {0} },

    { MODKEY, XK_n,      pop_mode_action, {0} },
    {      0, XK_n,      pop_mode_action, {0} },

    { MODKEY, XK_y, quit, {0} },
    {      0, XK_y, quit, {0} },
};

static const Key browser_mode_keys[] = {
    { MODKEY, XK_Escape, pop_mode_action,         {0} },
    {      0, XK_Escape, pop_mode_action,         {0} },

    { MODKEY, XK_b, spawn_brave, {.v = "--profile-directory=Personal"} },
    { MODKEY, XK_p, spawn_brave, {.v = "--profile-directory=Play"}     },
    { MODKEY, XK_m, spawn_brave, {.v = "--profile-directory=Music"}    },
    { MODKEY, XK_r, spawn_brave, {.v = "--profile-directory=Research"} },
    { MODKEY, XK_w, spawn_brave, {.v = "--profile-directory=Work"}     },

    {      0, XK_b, spawn_brave, {.v = "--profile-directory=Personal"} },
    {      0, XK_p, spawn_brave, {.v = "--profile-directory=Play"}     },
    {      0, XK_m, spawn_brave, {.v = "--profile-directory=Music"}    },
    {      0, XK_r, spawn_brave, {.v = "--profile-directory=Research"} },
    {      0, XK_w, spawn_brave, {.v = "--profile-directory=Work"}     },

    { MODKEY, XK_f, spawn_firefox, {.v = "Main" } },
    {      0, XK_f, spawn_firefox, {.v = "Main" } },
};

static const Key surf_mode_keys[] = {
    { MODKEY, XK_Escape, pop_mode_action,         {0} },
    {      0, XK_Escape, pop_mode_action,         {0} },

    { MODKEY, XK_s, spawn_surf, {.v = "~/.surf/cookies-personal.txt"} },
    {      0, XK_s, spawn_surf, {.v = "~/.surf/cookies-personal.txt"} },

    { MODKEY, XK_p, spawn_surf, {.v = "/dev/null"} },
    {      0, XK_p, spawn_surf, {.v = "/dev/null"} },
};

static const Array keys[] = {
    [ModeNormal]      = { (void*)normal_mode_keys,  ArrayLength(normal_mode_keys) },
    [ModeQuit]        = { (void*)quit_mode_keys,    ArrayLength(quit_mode_keys) },
    [ModeBrowser]     = { (void*)browser_mode_keys, ArrayLength(browser_mode_keys) },
    [ModeSurfBrowser] = { (void*)surf_mode_keys, ArrayLength(surf_mode_keys) }
};

/* button definitions */
/* click can be ClkTagBar, ClkLtSymbol, ClkStatusText, ClkWinTitle, ClkClientWin, or ClkRootWin */
static Button buttons[] = {
    /* click                event mask      button          function        argument */
    { ClkRootWin,           0,              Button1,        do_nothing,       {0} },
    { ClkWinTitle,          0,              Button2,        make_main_client, {0} },
    { ClkStatusText,        0,              Button1,        sigstatusbar,     {.i = 1} },
    { ClkStatusText,        0,              Button2,        sigstatusbar,     {.i = 2} },
    { ClkStatusText,        0,              Button3,        sigstatusbar,     {.i = 3} },
    { ClkClientWin,         MODKEY,         Button1,        movemouse,        {0} },
    // { ClkClientWin,         MODKEY,         Button2,        togglefloating, {0} },
    { ClkClientWin,         MODKEY,         Button3,        resizemouse,      {0} },
    { ClkTagBar,            0,              Button1,        view,             {0} },
    { ClkTagBar,            0,              Button3,        toggleview,       {0} },
    { ClkTagBar,            MODKEY,         Button1,        tag,              {0} },
    { ClkTagBar,            MODKEY,         Button3,        toggletag,        {0} },
};

