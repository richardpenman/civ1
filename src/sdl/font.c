/* FONTS.CV: a count, offsets to each font's bitmap; before each bitmap the character
 * widths (one per character) and an 8-byte header: first and last character, bytes per row,
 * ?, height - 1, ...  Bitmaps are row by row across all characters, the high bit leftmost. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "font.h"
#include "gfx.h"

#define MAX_FONTS 9

typedef struct {
    int first, last, bpr, height;
    const uint8_t *widths, *bits;
} Font;

static uint8_t *data;
static Font fonts[MAX_FONTS];
static int nfonts;

int font_load(const char *path)
{
    FILE *f = fopen(path, "rb");
    long len;
    int i, off;

    if (!f)
        return -1;
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = malloc((size_t)len);
    if (!data || fread(data, 1, (size_t)len, f) != (size_t)len) {
        fclose(f);
        return -1;
    }
    fclose(f);
    nfonts = data[0] | data[1] << 8;
    if (nfonts > MAX_FONTS)
        nfonts = MAX_FONTS;
    for (i = 0; i < nfonts; i++) {
        const uint8_t *h;
        off = data[2 + 2 * i] | data[3 + 2 * i] << 8;
        h = data + off - 8;
        fonts[i].first = h[0];
        fonts[i].last = h[1];
        fonts[i].bpr = h[2];
        fonts[i].height = h[4] + 1;
        fonts[i].bits = data + off;
        fonts[i].widths = h - (fonts[i].last - fonts[i].first + 1);
    }
    return 0;
}

int font_height(int font)
{
    return font < nfonts ? fonts[font].height : 8;
}

static int char_width(const Font *f, int c)
{
    if (c < f->first || c > f->last)
        return 0;
    return f->widths[c - f->first];
}

int text_width(int font, const char *s)
{
    int w = 0;

    if (font >= nfonts)
        return 0;
    for (; *s && *s != '\n'; s++)
        if (!((unsigned char)*s & 0x80))
            w += char_width(&fonts[font], (unsigned char)*s) + 1;
    return w;
}

int draw_text(int font, int x, int y, int colour, const char *s)
{
    const Font *f;
    int n, r, i, c, w, bits;

    if (font >= nfonts)
        return x;
    f = &fonts[font];
    n = f->last - f->first + 1;
    for (; *s && *s != '\n'; s++) {
        c = (unsigned char)*s;
        if (c & 0x80) {                /* (the driver: a byte 0x80 + n draws on in colour n) */
            colour = c & 0x7F;
            continue;
        }
        if (c < f->first || c > f->last) {
            x += 3;
            continue;
        }
        w = char_width(f, c);
        for (r = 0; r < f->height; r++) {
            const uint8_t *p = f->bits + (r * n + (c - f->first)) * f->bpr;
            bits = f->bpr == 2 ? (p[0] << 8 | p[1]) : p[0] << 8;
            for (i = 0; i < w; i++)
                if (bits & (0x8000 >> i))
                    gfx_pixel(x + i, y + r, colour);
        }
        x += w + 1;
    }
    return x;
}

void draw_text_centered(int font, int cx, int y, int colour, const char *s)
{
    draw_text(font, cx - text_width(font, s) / 2, y, colour, s);
}

/* f_1f65_008e: the text word-wrapped at chars * 8 pixels from (x, y) in the current font,
 * breaking at ' ', '\n' and '^' while y <= 192; returns the y after the last line. */
int text_wrapped(char *t, int font, int chars, int x, int y, int colour)
{
    int lh = font_height(font), i, n = (int)strlen(t), w = 0, last = 0, start = 0;
    char c, one[2] = { 0, 0 };

    for (i = 0; i < n; i++) {
        one[0] = t[i];
        w += text_width(font, one);
        if (t[i] == ' ' || t[i] == '\n' || t[i] == '^')
            last = i;
        if (t[i] != '\n' && t[i] != '^' && chars * 8 >= w)
            continue;
        c = t[last];
        t[last] = 0;
        draw_text(font, x, y, colour, t + start);
        t[last] = c;
        y += lh;
        w = 0;
        start = last + 1;
        if (t[i] == '^')
            start++;
        i = last;
        if (y > 192)
            return y;
    }
    if (w > 0 && y <= 192) {
        draw_text(font, x, y, colour, t + start);
        y += lh;
    }
    return y;
}

/* o_21_0000: the newspaper over the top of the screen, a crowd (SP257 (176,128) 20x9) below
 * it; city -2 (n->crowd 0) leaves the page drawn for the caller (a new government). */
