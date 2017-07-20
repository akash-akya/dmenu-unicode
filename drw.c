/* See LICENSE file for copyright and license details. */
#include <stdio.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>

#include <X11/Xutil.h>
#include <X11/Xatom.h>

#include <pango/pango.h>
#include <pango/pangoxft.h>
#include <pango/pangofc-fontmap.h>

#include "drw.h"
#include "util.h"

#define UTF_INVALID 0xFFFD
#define UTF_SIZ     4

static const unsigned char utfbyte[UTF_SIZ + 1] = {0x80,    0, 0xC0, 0xE0, 0xF0};
static const unsigned char utfmask[UTF_SIZ + 1] = {0xC0, 0x80, 0xE0, 0xF0, 0xF8};
static const long utfmin[UTF_SIZ + 1] = {       0,    0,  0x80,  0x800,  0x10000};

static const long utfmax[UTF_SIZ + 1] = {0x10FFFF, 0x7F, 0x7FF, 0xFFFF, 0x10FFFF};

Drw *
drw_create(Display *dpy, int screen, Window root, unsigned int w, unsigned int h)
{
	Drw *drw = ecalloc(1, sizeof(Drw));

	drw->dpy = dpy;
	drw->screen = screen;
	drw->root = root;
	drw->w = w;
	drw->h = h;
	drw->drawable = XCreatePixmap(dpy, root, w, h, DefaultDepth(dpy, screen));
	drw->gc = XCreateGC(dpy, root, 0, NULL);
	XSetLineAttributes(dpy, drw->gc, 1, LineSolid, CapButt, JoinMiter);

	return drw;
}

void
drw_resize(Drw *drw, unsigned int w, unsigned int h)
{
	if (!drw)
		return;

	drw->w = w;
	drw->h = h;
	if (drw->drawable)
		XFreePixmap(drw->dpy, drw->drawable);
	drw->drawable = XCreatePixmap(drw->dpy, drw->root, w, h, DefaultDepth(drw->dpy, drw->screen));
}

void
drw_free(Drw *drw)
{
	XFreePixmap(drw->dpy, drw->drawable);
	XFreeGC(drw->dpy, drw->gc);
	free(drw);
}

/* This function is an implementation detail. Library users should use
 * drw_fontset_create instead.
 */
static Fnt *
xfont_create(Drw *drw, const char *fontname, FcPattern *fontpattern)
{
	Fnt *font;
	XftFont *xfont = NULL;
	FcPattern *pattern = NULL;

	if (fontname) {
		/* Using the pattern found at font->xfont->pattern does not yield the
		 * same substitution results as using the pattern returned by
		 * FcNameParse; using the latter results in the desired fallback
		 * behaviour whereas the former just results in missing-character
		 * rectangles being drawn, at least with some fonts. */
		if (!(xfont = XftFontOpenName(drw->dpy, drw->screen, fontname))) {
			fprintf(stderr, "error, cannot load font from name: '%s'\n", fontname);
			return NULL;
		}
		if (!(pattern = FcNameParse((FcChar8 *) fontname))) {
			fprintf(stderr, "error, cannot parse font name to pattern: '%s'\n", fontname);
			XftFontClose(drw->dpy, xfont);
			return NULL;
		}
	} else if (fontpattern) {
		if (!(xfont = XftFontOpenPattern(drw->dpy, fontpattern))) {
			fprintf(stderr, "error, cannot load font from pattern.\n");
			return NULL;
		}
	} else {
		die("no font specified.");
	}

	font = ecalloc(1, sizeof(Fnt));
	font->xfont = xfont;
	font->pattern = pattern;
	font->h = xfont->ascent + xfont->descent;
	font->dpy = drw->dpy;

	return font;
}

static void
xfont_free(Fnt *font)
{
	if (!font)
		return;
	if (font->pattern)
		FcPatternDestroy(font->pattern);
	XftFontClose(font->dpy, font->xfont);
	free(font);
}

Fnt*
drw_fontset_create(Drw* drw, const char *fonts[], size_t fontcount)
{
	Fnt *cur, *ret = NULL;
	size_t i;

	if (!drw || !fonts)
		return NULL;

	for (i = 1; i <= fontcount; i++) {
		if ((cur = xfont_create(drw, fonts[fontcount - i], NULL))) {
			cur->next = ret;
			ret = cur;
		}
	}
	return (drw->fonts = ret);
}

void
drw_fontset_free(Fnt *font)
{
	if (font) {
		drw_fontset_free(font->next);
		xfont_free(font);
	}
}

void
drw_clr_create(Drw *drw, Clr *dest, const char *clrname)
{
	if (!drw || !dest || !clrname)
		return;

	if (!XftColorAllocName(drw->dpy, DefaultVisual(drw->dpy, drw->screen),
	                       DefaultColormap(drw->dpy, drw->screen),
	                       clrname, dest))
		die("error, cannot allocate color '%s'", clrname);
}

/* Wrapper to create color schemes. The caller has to call free(3) on the
 * returned color scheme when done using it. */
Clr *
drw_scm_create(Drw *drw, const char *clrnames[], size_t clrcount)
{
	size_t i;
	Clr *ret;

	/* need at least two colors for a scheme */
	if (!drw || !clrnames || clrcount < 2 || !(ret = ecalloc(clrcount, sizeof(XftColor))))
		return NULL;

	for (i = 0; i < clrcount; i++)
		drw_clr_create(drw, &ret[i], clrnames[i]);
	return ret;
}

void
drw_setfontset(Drw *drw, Fnt *set)
{
	if (drw)
		drw->fonts = set;
}

void
drw_setscheme(Drw *drw, Clr *scm)
{
	if (drw)
		drw->scheme = scm;
}

void
drw_rect(Drw *drw, int x, int y, unsigned int w, unsigned int h, int filled, int invert)
{
	if (!drw || !drw->scheme)
		return;
	XSetForeground(drw->dpy, drw->gc, invert ? drw->scheme[ColBg].pixel : drw->scheme[ColFg].pixel);
	if (filled)
		XFillRectangle(drw->dpy, drw->drawable, drw->gc, x, y, w, h);
	else
		XDrawRectangle(drw->dpy, drw->drawable, drw->gc, x, y, w - 1, h - 1);
}

int
drw_text(Drw *drw, int x, int y, unsigned int w, unsigned int h, unsigned int lpad, const char *text, int invert)
{
	int ty;
	unsigned int ew;
	XftDraw *d = NULL;
	Fnt *usedfont;
    PangoContext *pgo_context;
    PangoFontMap *pgo_fontmap;
	int utf8strlen, render = x || y || w || h;

	if (!drw || (render && !drw->scheme) || !text || !drw->fonts)
		return 0;

	if (!render) {
		w = ~w;
	} else {
		XSetForeground(drw->dpy, drw->gc, drw->scheme[invert ? ColFg : ColBg].pixel);
		XFillRectangle(drw->dpy, drw->drawable, drw->gc, x, y, w, h);
		d = XftDrawCreate(drw->dpy, drw->drawable,
		                  DefaultVisual(drw->dpy, drw->screen),
		                  DefaultColormap(drw->dpy, drw->screen));
		x += lpad;
		w -= lpad;
	}

	usedfont = drw->fonts;
    utf8strlen = strlen(text);

    if (utf8strlen) {
        pgo_fontmap = pango_xft_get_font_map (drw->dpy, drw->screen);
        pgo_context = pango_font_map_create_context (pgo_fontmap);
        
        pango_get_extents(pgo_context, usedfont->xfont, text, &ew);

        if (render) {
            ty = y + (h - usedfont->h) / 2 + usedfont->xfont->ascent;
            x_blit(pgo_context, pgo_fontmap, &drw->scheme[invert ? ColBg : ColFg], usedfont->xfont, d, text, x, ty);
                    
        }
        x += ew;
        w -= ew;
        if (pgo_context) g_object_unref(pgo_context);
    }

	if (d)
		XftDrawDestroy(d);

	return x + (render ? w : 0);
}

void
drw_map(Drw *drw, Window win, int x, int y, unsigned int w, unsigned int h)
{
	if (!drw)
		return;

	XCopyArea(drw->dpy, drw->drawable, win, drw->gc, x, y, w, h, x, y);
	XSync(drw->dpy, False);
}

unsigned int
drw_fontset_getwidth(Drw *drw, const char *text)
{
	if (!drw || !drw->fonts || !text)
		return 0;
	return drw_text(drw, 0, 0, 0, 0, 0, text, 0);
}

void
drw_font_getexts(Fnt *font, const char *text, unsigned int len, unsigned int *w, unsigned int *h)
{
	XGlyphInfo ext;

	if (!font || !text)
		return;

	XftTextExtentsUtf8(font->dpy, font->xfont, (XftChar8 *)text, len, &ext);
    
	if (w)
		*w = ext.xOff;
	if (h)
		*h = font->h;
}

void
get_text_width (PangoLayout *layout, unsigned int *width)
{
    pango_layout_get_size (layout, (int *)width, NULL);
    /* Divide by pango scale to get dimensions in pixels. */
    *width /= PANGO_SCALE;
}

void
pango_get_extents(PangoContext *pgo_context, XftFont *xfont, const char *TextStr, unsigned int *width)
{
    PangoFontDescription *fontdes;
    PangoLayout          *layout;
 
    if ((fontdes = pango_fc_font_description_from_pattern (xfont->pattern, TRUE)) == 0)
        {
            fprintf(stderr, "Failed to load font, exiting.");
            exit(-1);
        }

    pango_context_set_font_description(pgo_context, fontdes);

    layout = pango_layout_new (pgo_context);
    pango_layout_set_text (layout, TextStr, -1);
    pango_layout_set_font_description (layout, fontdes);
    
    /* Get text dimensions and create a context to render to */
    get_text_width (layout, width);
    g_object_unref (layout);
}

void
x_blit(PangoContext *pgo_context, PangoFontMap *pgo_fontmap, XftColor *xftcol, XftFont *xfont, XftDraw *xftdraw, const char *TextStr, int x, int y)
{
    PangoFontDescription *fontdes;
    PangoFont            *font;

    if ((fontdes = pango_fc_font_description_from_pattern (xfont->pattern, TRUE)) == 0)
        {
            fprintf(stderr, "Failed to load font, exiting.");
            exit(-1);
        }

    pango_context_set_font_description(pgo_context, fontdes);

    if ((font = pango_font_map_load_font (pgo_fontmap,
                                          pgo_context, fontdes)) == NULL)
        {
            fprintf(stderr, "Failed to load font, exiting.");
            exit(-1);
        }

    char *str = NULL;
    GList *items_head = NULL, *items = NULL;
    PangoAttrList *attr_list = NULL;


    attr_list = pango_attr_list_new (); /* no markup - empty attributes */
    str       = strdup(TextStr);

    /* analyse string, breaking up into items */
    items_head = items = pango_itemize (pgo_context, str, 
                                        0, strlen(TextStr),
                                        attr_list, NULL);

    while (items)
        {
            PangoItem        *this   = (PangoItem *)items->data;
            PangoGlyphString *glyphs = pango_glyph_string_new ();
            PangoRectangle    rect;
       
            /* shape current item into run of glyphs */
            pango_shape  (&str[this->offset], this->length, 
                          &this->analysis, glyphs);

            
            /* render the glyphs */
            pango_xft_render (xftdraw, 
                              xftcol,
                              this->analysis.font,
                              glyphs,
                              x, y);

            /* calculate rendered area */
            pango_glyph_string_extents (glyphs,
                                        this->analysis.font,
                                        &rect,
                                        NULL);

            x += ( rect.x + rect.width ) / PANGO_SCALE;
       
            pango_item_free (this);
            pango_glyph_string_free (glyphs);

            items = items->next;
        }

    
    if (attr_list)  pango_attr_list_unref (attr_list);
    if (str)        free(str); 
    if (items_head) g_list_free (items_head);
}

Cur *
drw_cur_create(Drw *drw, int shape)
{
	Cur *cur;

	if (!drw || !(cur = ecalloc(1, sizeof(Cur))))
		return NULL;

	cur->cursor = XCreateFontCursor(drw->dpy, shape);

	return cur;
}

void
drw_cur_free(Drw *drw, Cur *cursor)
{
	if (!cursor)
		return;

	XFreeCursor(drw->dpy, cursor->cursor);
	free(cursor);
}
