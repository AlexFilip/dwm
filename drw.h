/* See LICENSE file for copyright and license details. */

typedef struct Cur Cur;
struct Cur {
	Cursor cursor;
};

typedef struct Fnt Fnt;
struct Fnt {
	Display *display;
	unsigned int height;
	XftFont *xfont;
	FcPattern *pattern;
	struct Fnt *next;
};

enum { ColFg, ColBg, ColBorder }; /* Color scheme index */

typedef struct Drw Drw;
struct Drw {
	unsigned int width, height;
	Display *display;
	int screen;
	Window root;
	Drawable drawable;
	GC gc;
	// XftColor *scheme;
	Fnt *fonts;
};

typedef struct ColorSet ColorSet;
struct ColorSet {
    const char *fg;
    const char *bg;
    const char *border;
};

static const int numColorsInSet = sizeof(ColorSet) / sizeof(const char*);

/* Drawable abstraction */
void drw_init(Drw *drw, Display *display, int screen, Window win, unsigned int w, unsigned int h);
void drw_resize(Drw *drw, unsigned int w, unsigned int h);
void drw_clean(Drw *drw);

/* Fnt abstraction */
Fnt *drw_fontset_create(Drw* drw, const char *fonts[], size_t fontcount);
void drw_fontset_free(Fnt* set);
unsigned int drw_fontset_getwidth(Drw *drw, const char *text);
void drw_font_getexts(Fnt *font, const char *text, unsigned int len, unsigned int *w, unsigned int *h);

/* Colorscheme abstraction */
void drw_clr_create(Drw *drw, XftColor *dest, const char *clrname);
void drw_scm_create(Drw *drw, const ColorSet* colorset, XftColor *xft_color);

/* Cursor abstraction */
Cur drw_cur_create(Drw *drw, int shape);

/* Drawing functions */
void drw_rect(Drw *drw, int x, int y, unsigned int width, unsigned int height, XftColor* scheme, int filled, int invert);
int drw_text(Drw *drw, int x, int y, unsigned int width, unsigned int height, XftColor* scheme, unsigned int lpad, const char *text, int invert);

/* Map functions */
void drw_map(Drw *drw, Window win, int x, int y, unsigned int w, unsigned int h);
