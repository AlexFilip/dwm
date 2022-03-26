/* See LICENSE file for copyright and license details. */

/* appearance */
static const unsigned int borderpx  = 2;        /* border pixel of windows */
static const unsigned int snap      = 32;       /* snap pixel */
static const int showbar            = 1;        /* 0 means no bar */
static const int topbar             = 1;        /* 0 means bottom bar */
static const char *fonts[]          = { "monospace:size=12" };
static const char dmenufont[]       = "monospace:size=12";
static const char col_gray1[]       = "#222222";
static const char col_gray2[]       = "#444444";
static const char col_gray3[]       = "#bbbbbb";
static const char col_gray4[]       = "#eeeeee";
static const char col_cyan[]        = "#005577";
static const char col_selected[]    = "#fa2106";
static const char col_app_bg[]      = "#11750a";

static int gap_size                 = 6;        /* gaps between windows */

static const ColorSet colors[]      = {
    /*               fg             bg         border   */
    [SchemeNorm]      = { .fg = col_gray3,     .bg = col_gray1,   .border = col_gray2 },
    [SchemeSel]       = { .fg = col_selected,  .bg = col_gray1,   .border = col_cyan  },
    [SchemeAppLaunch] = { .fg = col_gray3,     .bg = col_app_bg,  .border = col_gray2 },
};

/* tagging */
static const char *tags[] = { "Main", ">_", "3", "4", "5", "6", "7", "8", "9" };

static const Rule rules[] = {
    /* xprop(1):
     *    WM_CLASS(STRING) = instance, class
     *    WM_NAME(STRING) = title
     */
    /* class     instance  title   tags mask   isfloating   monitor */
    { "Gimp",    NULL,     NULL,   0,          1,           -1 },
    { "Firefox", NULL,     NULL,   1 << 8,     0,           -1 },
};

/* layout(s) */
static const int mfact = 55; /* factor of master area size [5..95] [0.05..0.95] */

enum {
    tile_index = 0,
    monocle_index = 1,
};

static const Layout layouts[] = {
    // NOTE: NULL is not a supported state in this fork of dwm
    [tile_index]    = { .arrange = tile },    /* first entry is default */
    [monocle_index] = { .arrange = monocle },
};

/* key definitions */
#ifdef DEBUG
#define MODKEY Mod1Mask
#else
#define MODKEY Mod4Mask
#endif

#define TAGKEYS(KEY,TAG) \
    { MODKEY,                       ModeNormal,  KEY,      view,           { .ui = 1 << TAG } }, \
    { MODKEY|ControlMask,           ModeNormal,  KEY,      toggleview,     { .ui = 1 << TAG } }, \
    { MODKEY|ShiftMask,             ModeNormal,  KEY,      tag,            { .ui = 1 << TAG } }, \
    { MODKEY|ControlMask|ShiftMask, ModeNormal,  KEY,      toggletag,      { .ui = 1 << TAG } }

/* helper for spawning shell commands in the pre dwm-5.0 fashion */
#define SHCMD(cmd) { .v = (const char*[]){ "/bin/sh", "-c", cmd, NULL } }

// #define STATUSBAR "dwmblocks"
#define STATUSBAR "spoon"

static const char* volume_up[]   = { "/usr/bin/pactl", "set-sink-volume", "@DEFAULT_SINK@",   "+5%",    NULL };
static const char* volume_down[] = { "/usr/bin/pactl", "set-sink-volume", "@DEFAULT_SINK@",   "-5%",    NULL };
static const char* volume_mute[] = { "/usr/bin/pactl", "set-sink-mute",   "@DEFAULT_SINK@",   "toggle", NULL };
static const char* mic_mute[]    = { "/usr/bin/pactl", "set-source-mute", "@DEFAULT_SOURCE@", "toggle", NULL };

static const char* brightness_up[]   = { "/home/alex/bin/backlight" "+1", NULL };
static const char* brightness_down[] = { "/home/alex/bin/backlight" "-1", NULL };

/* commands */
static char          dmenumon[2] = "0"; /* component of dmenucmd, manipulated in spawn() */
static char const*   dmenucmd[ ] = { "dmenu_run", "-m", dmenumon, "-fn", dmenufont, "-nb", col_gray1, "-nf", col_gray3, "-sb", col_cyan, "-sf", col_gray4, NULL };
static char const*    termcmd[ ] = { "st", NULL };
static char const*  editorcmd[ ] = { "st", "nvim", NULL };
static char const*    htopcmd[ ] = { "st", "htop", NULL };

static Key keys[] = {
    /* modifier                     mode          key        function          argument */
    { MODKEY,                       ModeNormal,   XK_space,  spawn,            {.v = dmenucmd} },
    { MODKEY,                       ModeNormal,   XK_t,      spawn,            {.v = termcmd} },
    { MODKEY,                       ModeNormal,   XK_e,      spawn,            {.v = editorcmd} },
    { MODKEY,                       ModeNormal,   XK_b,      push_mode,        {.i = ModeBrowser} },

    // TODO: Consider putting these in a separate array and not checking modifier if mode == Normal
    { MODKEY,                       ModeBrowser,  XK_b,      spawn_browser,    {.v = "--profile-directory=Personal"} },
    { MODKEY,                       ModeBrowser,  XK_p,      spawn_browser,    {.v = "--profile-directory=Play"}     },
    { MODKEY,                       ModeBrowser,  XK_m,      spawn_browser,    {.v = "--profile-directory=Music"}    },
    { MODKEY,                       ModeBrowser,  XK_r,      spawn_browser,    {.v = "--profile-directory=Research"} },

    { MODKEY,                       ModeBrowser,  XK_Escape, pop_mode,         {0} },

    { MODKEY,                       ModeNormal,   XK_p,      spawn,            {.v = htopcmd} },
    { MODKEY,                       ModeNormal,   XK_f,      toggle_layout,    {0} },

    { MODKEY,                       ModeNormal,   XK_h,      focusstack,       {.i = -1} },
    { MODKEY,                       ModeNormal,   XK_l,      focusstack,       {.i = +1} },
    { MODKEY,                       ModeNormal,   XK_j,      setmfact,         {.i = -5} },
    { MODKEY,                       ModeNormal,   XK_k,      setmfact,         {.i = +5} },

    { MODKEY|ShiftMask,             ModeNormal,   XK_j,      move_vert,        {.i = +1} },
    { MODKEY|ShiftMask,             ModeNormal,   XK_k,      move_vert,        {.i = -1} },

    { MODKEY|ShiftMask,             ModeNormal,   XK_h,      move_horiz,       {.i = -1} },
    { MODKEY|ShiftMask,             ModeNormal,   XK_l,      move_horiz,       {.i = +1} },

    { MODKEY,                       ModeNormal,   XK_slash,  togglefloating,   {0} },

    { MODKEY,                       ModeNormal,   XK_Return, make_main_client, {0} },
    { MODKEY,                       ModeNormal,   XK_Tab,    view,             {0} },
    { MODKEY,                       ModeNormal,   XK_w,      killclient,       {0} },

    { MODKEY,                       ModeNormal,   XK_comma,  focusmon,         {.i = -1} },
    { MODKEY,                       ModeNormal,   XK_period, focusmon,         {.i = +1} },

    { MODKEY|ShiftMask,             ModeNormal,   XK_comma,  tagmon,           {.i = -1} },
    { MODKEY|ShiftMask,             ModeNormal,   XK_period, tagmon,           {.i = +1} },

    { MODKEY,                       ModeNormal,   XK_0,      view,             {.ui = ~0} },
    { MODKEY|ShiftMask,             ModeNormal,   XK_0,      tag,              {.ui = ~0} },

    // TAGKEYS(                        XK_1,                      0)
    // TAGKEYS(                        XK_2,                      1)
    // TAGKEYS(                        XK_3,                      2)
    // TAGKEYS(                        XK_4,                      3)
    // TAGKEYS(                        XK_5,                      4)
    // TAGKEYS(                        XK_6,                      5)
    // TAGKEYS(                        XK_7,                      6)
    // TAGKEYS(                        XK_8,                      7)
    // TAGKEYS(                        XK_9,                      8)

    { MODKEY|ShiftMask,             ModeNormal,   XK_q,      quit,             {0} },
    { MODKEY,                       ModeNormal,   XK_y,      resize_window,    { .i = +1 } },
    { MODKEY|ShiftMask,             ModeNormal,   XK_y,      resize_window,    { .i = -1 } },
    { MODKEY|ControlMask,           ModeNormal,   XK_y,      change_window_aspect_ratio,     { .i = -1 } },
    { MODKEY|ControlMask|ShiftMask, ModeNormal,   XK_y,      change_window_aspect_ratio,     { .i = +1 } },

    // Volume controls
    { 0, ModeNormal,   XF86XK_AudioRaiseVolume,   spawn, { .v = volume_up   } },
    { 0, ModeNormal,   XF86XK_AudioLowerVolume,   spawn, { .v = volume_down } },
    { 0, ModeNormal,   XF86XK_AudioMute,          spawn, { .v = volume_mute } },
    { 0, ModeNormal,   XF86XK_AudioMicMute,       spawn, { .v = mic_mute    } },

    // Screen brightness controls
    // NOTE: This doesn't work because of the '~' in the path. I'll fix it later.
    { 0, ModeNormal, XF86XK_MonBrightnessUp,   spawn, { .v = brightness_up   } },
    { 0, ModeNormal, XF86XK_MonBrightnessDown, spawn, { .v = brightness_down } },
};

/* button definitions */
/* click can be ClkTagBar, ClkLtSymbol, ClkStatusText, ClkWinTitle, ClkClientWin, or ClkRootWin */
static Button buttons[] = {
    /* click                event mask      button          function        argument */
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

