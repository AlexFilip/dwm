/* See LICENSE file for copyright and license details. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>

#include "drw.h"
#include "util.h"

#define UTF_INVALID 0xFFFD
#define UTF_SIZ     4

static const unsigned char utfbyte[UTF_SIZ + 1] = {     0x80,    0,  0xC0,   0xE0,     0xF0 };
static const unsigned char utfmask[UTF_SIZ + 1] = {     0xC0, 0x80,  0xE0,   0xF0,     0xF8 };
static const          long  utfmin[UTF_SIZ + 1] = {        0,    0,  0x80,  0x800,  0x10000 };
static const          long  utfmax[UTF_SIZ + 1] = { 0x10FFFF, 0x7F, 0x7FF, 0xFFFF, 0x10FFFF };

static long utf8decodebyte(const char c, size_t *i) {
    for (*i = 0; *i < (UTF_SIZ + 1); ++(*i))
        if (((unsigned char)c & utfmask[*i]) == utfbyte[*i])
            return (unsigned char)c & ~utfmask[*i];
    return 0;
}

static size_t utf8validate(long *u, size_t i) {
    if (!Between(*u, utfmin[i], utfmax[i]) || Between(*u, 0xD800, 0xDFFF))
        *u = UTF_INVALID;
    for (i = 1; *u > utfmax[i]; ++i)
        ;
    return i;
}

static size_t utf8decode(const char *c, long *u, size_t clen) {
    size_t i, j, len, type;
    long udecoded;

    *u = UTF_INVALID;
    if (!clen)
        return 0;

    udecoded = utf8decodebyte(c[0], &len);
    if (!Between(len, 1, UTF_SIZ))
        return 1;

    for (i = 1, j = 1; i < clen && j < len; ++i, ++j) {
        udecoded = (udecoded << 6) | utf8decodebyte(c[i], &type);
        if (type)
            return j;
    }

    if (j < len)
        return 0;

    *u = udecoded;
    utf8validate(u, len);

    return len;
}

void drw_init(Drw* drw, Display *display, int screen, Window root, unsigned int width, unsigned int height) {
    Drw null_drw = {0};
    *drw = null_drw;

    drw->display  = display;
    drw->screen   = screen;
    drw->root     = root;
    drw->width    = width;
    drw->height   = height;
    drw->drawable = XCreatePixmap(display, root, height, height, DefaultDepth(display, screen));
    drw->gc       = XCreateGC(display, root, 0, NULL);
    XSetLineAttributes(display, drw->gc, 1, LineSolid, CapButt, JoinMiter);
}

void drw_resize(Drw *drw, unsigned int width, unsigned int height) {
    drw->width  = width;
    drw->height = height;

    if (drw->drawable)
        XFreePixmap(drw->display, drw->drawable);

    drw->drawable = XCreatePixmap(drw->display, drw->root, width, height, DefaultDepth(drw->display, drw->screen));
}

void drw_clean(Drw *drw) {
    XFreePixmap(drw->display, drw->drawable);
    XFreeGC(drw->display, drw->gc);
    drw_fontset_free(drw->fonts);
}

/* This function is an implementation detail. Library users should use
 * drw_fontset_create instead.
 */
static Fnt *xfont_create(Drw *drw, const char *fontname, FcPattern *fontpattern) {
    Fnt *font;
    XftFont *xfont = NULL;
    FcPattern *pattern = NULL;

    if (fontname) {
        /* Using the pattern found at font->xfont->pattern does not yield the
         * same substitution results as using the pattern returned by
         * FcNameParse; using the latter results in the desired fallback
         * behaviour whereas the former just results in missing-character
         * rectangles being drawn, at least with some fonts. */
        xfont = XftFontOpenName(drw->display, drw->screen, fontname);
        if (!xfont) {
            fprintf(stderr, "error, cannot load font from name: '%s'\n", fontname);
            return NULL;
        }
        pattern = FcNameParse((FcChar8 *) fontname);
        if (!pattern) {
            fprintf(stderr, "error, cannot parse font name to pattern: '%s'\n", fontname);
            XftFontClose(drw->display, xfont);
            return NULL;
        }
    } else if (fontpattern) {
        xfont = XftFontOpenPattern(drw->display, fontpattern);
        if (!xfont) {
            fprintf(stderr, "error, cannot load font from pattern.\n");
            return NULL;
        }
    } else {
        die("no font specified.");
    }

    /* Do not allow using color fonts. This is a workaround for a BadLength
     * error from Xft with color glyphs. Modelled on the Xterm workaround. See
     * https://bugzilla.redhat.com/show_bug.cgi?id=1498269
     * https://lists.suckless.org/dev/1701/30932.html
     * https://bugs.debian.org/cgi-bin/bugreport.cgi?bug=916349
     * and lots more all over the internet.
     */
    FcBool iscol;
    if(FcPatternGetBool(xfont->pattern, FC_COLOR, 0, &iscol) == FcResultMatch && iscol) {
        XftFontClose(drw->display, xfont);
        return NULL;
    }

    font = ecalloc(1, sizeof(Fnt));
    font->xfont = xfont;
    font->pattern = pattern;
    font->height = xfont->ascent + xfont->descent;
    font->display = drw->display;

    return font;
}

static void xfont_free(Fnt *font) {
    if (!font)
        return;

    if (font->pattern)
        FcPatternDestroy(font->pattern);

    XftFontClose(font->display, font->xfont);
    free(font);
}

Fnt* drw_fontset_create(Drw* drw, const char *fonts[], size_t fontcount) {
    Fnt *cur, *ret = NULL;
    size_t i;

    if (!fonts)
        return NULL;

    for (i = 1; i <= fontcount; i++) {
        if ((cur = xfont_create(drw, fonts[fontcount - i], NULL))) {
            cur->next = ret;
            ret = cur;
        }
    }
    return (drw->fonts = ret);
}

void drw_fontset_free(Fnt *font) {
    if (font) {
        drw_fontset_free(font->next);
        xfont_free(font);
    }
}

void drw_clr_create(Drw *drw, XftColor *dest, const char *clrname) {
    if (!XftColorAllocName(drw->display, DefaultVisual(drw->display, drw->screen),
                           DefaultColormap(drw->display, drw->screen),
                           clrname, dest))
        die("error, cannot allocate color '%s'", clrname);
}

/* Wrapper to create color schemes. The caller has to call free(3) on the
 * returned color scheme when done using it. */
void drw_scm_create(Drw *drw, const ColorSet* colorset, XftColor *xft_color) {
    drw_clr_create(drw, &xft_color[0], colorset->fg);
    drw_clr_create(drw, &xft_color[1], colorset->bg);
    drw_clr_create(drw, &xft_color[2], colorset->border);
}

void drw_rect(Drw *drw, int x, int y, unsigned int width, unsigned int height, XftColor* scheme, int filled, int invert) {
    if (!scheme)
        return;

    XSetForeground(drw->display, drw->gc, invert ? scheme[ColBg].pixel : scheme[ColFg].pixel);

    if (filled)
        XFillRectangle(drw->display, drw->drawable, drw->gc, x, y, width, height);
    else
        XDrawRectangle(drw->display, drw->drawable, drw->gc, x, y, width - 1, height - 1);
}

int drw_text(Drw *drw, int x, int y, unsigned int start_width, unsigned int height, XftColor* scheme, unsigned int lpad, const char *text, int invert) {
    unsigned int width = start_width;
    char buf[1024];
    unsigned int ew;
    XftDraw *d = NULL;
    int utf8charlen, render = x || y || width || height;
    long utf8codepoint = 0;
    FcCharSet *fccharset;
    FcPattern *fcpattern;
    FcPattern *match;
    XftResult result;
    int charexists = 0;

    if ((render && !scheme) || !text || !drw->fonts)
        return 0;

    if (!render) {
        width = ~width;
    } else {
        XSetForeground(drw->display, drw->gc, scheme[invert ? ColFg : ColBg].pixel);
        XFillRectangle(drw->display, drw->drawable, drw->gc, x, y, width, height);
        d = XftDrawCreate(drw->display, drw->drawable,
                          DefaultVisual(drw->display, drw->screen),
                          DefaultColormap(drw->display, drw->screen));
        x += lpad;
        width -= lpad;
    }

    Fnt *usedfont = drw->fonts;
    while (1) {
        int utf8strlen = 0;
        const char *utf8str = text;
        Fnt *nextfont = NULL;
        while (*text) {
            utf8charlen = utf8decode(text, &utf8codepoint, UTF_SIZ);
            for (Fnt *curfont = drw->fonts; curfont; curfont = curfont->next) {
                charexists = charexists || XftCharExists(drw->display, curfont->xfont, utf8codepoint);
                if (charexists) {
                    if (curfont == usedfont) {
                        utf8strlen += utf8charlen;
                        text += utf8charlen;
                    } else {
                        nextfont = curfont;
                    }
                    break;
                }
            }

            if (!charexists || nextfont)
                break;
            else
                charexists = 0;
        }

        if (utf8strlen) {
            drw_font_getexts(usedfont, utf8str, utf8strlen, &ew, NULL);
            /* shorten text if necessary */
            size_t len;
            for (len = Minimum(utf8strlen, sizeof(buf) - 1); len && ew > width; len--)
                drw_font_getexts(usedfont, utf8str, len, &ew, NULL);

            if (len) {
                memcpy(buf, utf8str, len);
                buf[len] = '\0';
                if (len < utf8strlen)
                    for (size_t i = len; i && i > len - 3; buf[--i] = '.')
                        ; /* NOP */

                if (render) {
                    int ty = y + (height - usedfont->height) / 2 + usedfont->xfont->ascent;
                    XftDrawStringUtf8(d, &scheme[invert ? ColBg : ColFg],
                                      usedfont->xfont, x, ty, (XftChar8 *)buf, len);
                }
                x += ew;
                width -= ew;
            }
        }

        if (!*text) {
            break;
        } else if (nextfont) {
            charexists = 0;
            usedfont = nextfont;
        } else {
            /* Regardless of whether or not a fallback font is found, the
             * character must be drawn. */
            charexists = 1;

            fccharset = FcCharSetCreate();
            FcCharSetAddChar(fccharset, utf8codepoint);

            if (!drw->fonts->pattern) {
                /* Refer to the comment in xfont_create for more information. */
                die("the first font in the cache must be loaded from a font string.");
            }

            fcpattern = FcPatternDuplicate(drw->fonts->pattern);
            FcPatternAddCharSet(fcpattern, FC_CHARSET, fccharset);
            FcPatternAddBool(fcpattern, FC_SCALABLE, FcTrue);
            FcPatternAddBool(fcpattern, FC_COLOR, FcFalse);

            FcConfigSubstitute(NULL, fcpattern, FcMatchPattern);
            FcDefaultSubstitute(fcpattern);
            match = XftFontMatch(drw->display, drw->screen, fcpattern, &result);

            FcCharSetDestroy(fccharset);
            FcPatternDestroy(fcpattern);

            if (match) {
                usedfont = xfont_create(drw, NULL, match);
                if (usedfont && XftCharExists(drw->display, usedfont->xfont, utf8codepoint)) {
                    Fnt *curfont;
                    for (curfont = drw->fonts; curfont->next; curfont = curfont->next)
                        ; /* NOP */
                    curfont->next = usedfont;
                } else {
                    xfont_free(usedfont);
                    usedfont = drw->fonts;
                }
            }
        }
    }

    if (d)
        XftDrawDestroy(d);

    return x + (render ? width : 0);
}

void drw_map(Drw *drw, Window win, int x, int y, unsigned int width, unsigned int height) {
    XCopyArea(drw->display, drw->drawable, win, drw->gc, x, y, width, height, x, y);
    XSync(drw->display, False);
}

unsigned int drw_fontset_getwidth(Drw *drw, const char *text) {
    if (!drw->fonts)
        return 0;

    return drw_text(drw, 0, 0, 0, 0, NULL, 0, text, 0);
}

void drw_font_getexts(Fnt *font, const char *text, unsigned int len, unsigned int *width, unsigned int *height) {
    XGlyphInfo ext;

    if (!font || !text)
        return;

    XftTextExtentsUtf8(font->display, font->xfont, (XftChar8 *)text, len, &ext);
    
    // The only places that call this function pass in NULL for the height an
    // a stack pointer for the width.

    // if (width)
    *width = ext.xOff;

    // if (height)
    //     *height = font->height;
}

Cur drw_cur_create(Drw *drw, int shape) {
    Cur result = {0};

    result.cursor = XCreateFontCursor(drw->display, shape);

    return result;
}

