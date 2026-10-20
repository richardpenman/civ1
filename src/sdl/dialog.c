/* Text sections (KING.TXT and friends) and the dialog boxes of the SDL front end. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "dialog.h"
#include "gfx.h"
#include "font.h"
#include "mapview.h"
#include "assets.h"
#include "../game.h"

char text_us[32], text_them[32], text_bucks[16];
int dialog_default = -1;         /* g_2f4a: the option highlighted first (once) */
int dialog_checks;               /* g_e712: options shown with a check mark (once) */
int dialog_texture;              /* g_2f48: grey boxes are stone (from the game's start on) */
int dialog_advisor = -1;         /* g_2f4e: who speaks (once): 0-2 a report, 3-6 an advisor */
int dialog_help;                 /* g_2f50: "(HELP AVAILABLE)" (once) */

/* g_2f56: the speakers; 0-2 add " report:" */
static const char *const speakers[7] = { "Spies", "Diplomats", "Travelers", "Defense Minister:",
                                         "Domestic Advisor:", "Foreign Minister:", "Science Advisor:" };
int dialog_font;

/* ------------------------------------------------------------------------------------- */
/* Text files. */

static char *slurp(const char *name, long *len)
{
    char path[512];
    FILE *f;
    char *buf;

    snprintf(path, sizeof path, "%s/%s", data_dir, name);
    f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    *len = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = malloc((size_t)*len + 1);
    if (buf && fread(buf, 1, (size_t)*len, f) != (size_t)*len) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    if (buf)
        buf[*len] = 0;
    return buf;
}

static const char *const text_files[] = { "KING.TXT", "HELP.TXT", "ERROR.TXT", "BLURB0.TXT", "BLURB1.TXT",
                                           "BLURB2.TXT", "BLURB3.TXT", "BLURB4.TXT", "PRODUCE.TXT" };
#define NFILES (int)(sizeof text_files / sizeof text_files[0])
static char *files[NFILES];
static long file_len[NFILES];

static void load_files(void)
{
    int i;

    if (files[0])
        return;
    for (i = 0; i < NFILES; i++)
        files[i] = slurp(text_files[i], &file_len[i]);
}

/* f_1f65_01b3: the section `key` ("*NAME", a whole line) of text file `name` read the way the
 * original reads it, each line with "%[^\n]\n" (so blank lines and the next line's leading
 * blanks go) and appended to out with a ' ' after it.  The number of lines, or -1. */
int text_section_lines(const char *name, const char *key, char *out, size_t n)
{
    const char *d = NULL, *p, *e, *end;
    size_t kl = strlen(key), o = 0;
    int f, lines = 0, found = 0;

    load_files();
    out[0] = 0;
    for (f = 0; f < NFILES; f++)
        if (!strcasecmp(text_files[f], name))
            d = files[f];
    if (!d)
        return -1;
    end = d + strlen(d);
    for (p = d; p < end; p = e) {
        for (e = p; e < end && *e != '\n' && *e != '\r'; e++)
            ;
        if (found) {
            if (*p == '*')
                break;
            while (p < e && o + 2 < n)
                out[o++] = *p++;
            if (o + 1 < n)
                out[o++] = ' ';
            lines++;
        } else if ((size_t)(e - p) == kl && !memcmp(p, key, kl))
            found = 1;
        while (e < end && (*e == '\n' || *e == '\r' || *e == ' ' || *e == '\t'))
            e++;
    }
    out[o] = 0;
    return found ? lines : -1;
}

/* Replace every `key` in s by `val`. */
static void replace(char *s, size_t n, const char *key, const char *val)
{
    char tmp[4096], *p;
    size_t kl = strlen(key);

    while ((p = strstr(s, key)) != NULL) {
        snprintf(tmp, sizeof tmp, "%.*s%s%s", (int)(p - s), s, val, p + kl);
        snprintf(s, n, "%s", tmp);
    }
}

void text_substitute(char *s, size_t n, const char *a, const char *b)
{
    replace(s, n, "$RPLC1", a ? a : ui_rplc[0]);
    replace(s, n, "$RPLC2", b ? b : ui_rplc[1]);
    replace(s, n, "$US", text_us[0] ? text_us : game.s.civ_names[game.s.human_civ]);
    replace(s, n, "$THEM", text_them);
    replace(s, n, "$BUCKS0", text_bucks);
    replace(s, n, "$BUCKS", text_bucks);
}

/* strstr over binary data (KING.TXT starts with an index containing zeros) */
static const char *find(const char *p, const char *end, const char *key, size_t kl)
{
    for (; p + kl <= end; p++)
        if (*p == key[0] && !memcmp(p, key, kl))
            return p;
    return NULL;
}

/* load_text_section: the lines of section `key` ("*NAME"), joined with spaces; '^' ends a
 * line.  Returns the index of the file it came from, -1 if there is none. */
int text_section_raw(const char *key, char *out, size_t n)
{
    int f;
    size_t kl = strlen(key), o = 0;

    load_files();
    out[0] = 0;
    for (f = 0; f < NFILES; f++) {
        const char *d = files[f], *p;
        if (!d)
            continue;
        for (p = d; (p = find(p, d + file_len[f], key, kl)) != NULL; p += kl) {
            if ((p != d && p[-1] != '\n') || (p[kl] != '\r' && p[kl] != '\n'))
                continue;
            p += kl;
            while (*p == '\r' || *p == '\n')
                p++;
            /* until the next section */
            while (p < d + file_len[f] && *p && *p != '*' && *p != 0x1A) {
                const char *e = p;
                while (*e && *e != '\n')
                    e++;
                if (o == 0 || out[o - 1] == '\n')
                    while (p < e && *p == ' ')
                        p++;              /* (text lines never start with a space: options do) */
                while (p < e && o + 3 < n) {
                    if (*p == '^') {
                        out[o++] = '\n';
                        p++;
                        while (p < e && (*p == ' ' || *p == '\r'))
                            p++;
                        continue;
                    }
                    if (*p != '\r')
                        out[o++] = *p;
                    p++;
                }
                if (o > 0 && out[o - 1] != '\n' && o + 1 < n)
                    out[o++] = ' ';
                p = *e ? e + 1 : e;
            }
            while (o > 0 && (out[o - 1] == ' ' || out[o - 1] == '\n'))
                o--;
            if (o + 1 < n)
                out[o++] = '\n';
            out[o] = 0;
            return f;
        }
    }
    return -1;
}

int text_file_of(const char *key)
{
    char tmp[2048];
    int f = text_section_raw(key, tmp, sizeof tmp);
    return f < 0 ? -1 : f;
}

const char *text_file_name(int f)
{
    return f >= 0 && f < NFILES ? text_files[f] : "";
}

/* A message: "*KEY" names a section; options are written "_text" in the files. */
void text_get(const char *text, char *out, size_t n, const char *a, const char *b)
{
    char *p;

    if (text[0] == '*' && text[1] && text[1] != '*' && text_section_raw(text, out, n) >= 0)
        ;
    else
        snprintf(out, n, "%s", text);
    text_substitute(out, n, a, b);
    for (p = out; *p; p++)
        if (*p == '_' && (p == out || p[-1] == '\n'))
            *p = ' ';
}

/* ------------------------------------------------------------------------------------- */
/* Dialogs. */

#define MAXL 40
typedef struct {
    char text[MAXL][100];
    int option[MAXL];        /* option number, -1 for a plain line */
    int n, options;
} Lines;

static void split(const char *text, Lines *L, int maxw, int font)
{
    const char *p = text;
    char line[256];
    int len, opt;

    L->n = L->options = 0;
    while (*p && L->n < MAXL) {
        len = 0;
        while (*p && *p != '\n' && len < 255)
            line[len++] = *p++;
        line[len] = 0;
        if (*p == '\n')
            p++;
        opt = line[0] == ' ' || line[0] == '_';
        if (opt && !*p && p[-1] != '\n')
            break;                             /* (an unfinished last line is not an option) */
        if (opt) {                             /* (kept whole: the original draws the space) */
            snprintf(L->text[L->n], sizeof L->text[L->n], "%s", line);
            L->option[L->n++] = L->options++;
            continue;
        }
        /* word-wrap plain lines */
        {
            char *s = line;
            while (*s && L->n < MAXL) {
                int cut = (int)strlen(s), i;
                char save;
                while (cut > 0) {
                    save = s[cut];
                    s[cut] = 0;
                    if (text_width(font, s) <= maxw) {
                        s[cut] = save;
                        break;
                    }
                    s[cut] = save;
                    for (i = cut - 1; i > 0 && s[i] != ' '; i--)
                        ;
                    cut = i > 0 ? i : cut - 1;
                }
                if (cut <= 0)
                    cut = (int)strlen(s);
                snprintf(L->text[L->n], sizeof L->text[L->n], "%.*s", cut, s);
                L->option[L->n++] = -1;
                s += cut;
                while (*s == ' ')
                    s++;
            }
            if (!line[0] && L->n < MAXL) {
                L->text[L->n][0] = 0;
                L->option[L->n++] = -1;
            }
        }
    }
    while (L->n > 0 && L->option[L->n - 1] == -1 && !L->text[L->n - 1][0])
        L->n--;
}

static uint8_t saved[SCR_H][SCR_W];

void screen_save(void)
{
    memcpy(saved, screen, sizeof saved);
}

void screen_restore(void)
{
    memcpy(screen, saved, sizeof saved);
}

/* f_1d1e_097a: a box of w x h inside a black outline at (x, y) .. (x + w + 2, y + h + 2);
 * grey (7) is the stone texture of SP299 (288, 120). */
void dialog_box_at(int x, int y, int w, int h, int colour)
{
    int i, j, tw, th;

    if (colour == 7 && dialog_texture) {
        for (j = 0; j < h; j += 16)                       /* (f_1dde_03cc) */
            for (i = 0; i < w; i += 32) {
                tw = w - i < 32 ? w - i : 32;
                th = h - j < 16 ? h - j : 16;
                gfx_blit(&sp299, 288, 120, tw, th, x + 1 + i, y + 1 + j, -1);
            }
    } else
        gfx_fill(x + 1, y + 1, w, h, colour);
    gfx_hline(x + 1, x + 1 + w, y + 1, 8);                /* (f_1d1e_0a74) */
    gfx_hline(x + 1, x + 1 + w, y + 1 + h, 15);
    gfx_vline(x + 1 + w, y + 1, y + 1 + h, 8);
    gfx_vline(x + 1, y + 2, y + 1 + h, 15);
    gfx_rect(x, y, w + 2, h + 2, 0);                      /* (f_1d1e_0a13) */
}

/* f_1d1e_003f / f_1d1e_0483: a menu or message at (x, y) (-1: centred over the map).  Lines
 * starting with ' ' or '_' are options; the result is the option chosen (0 = the first), -1
 * for Escape or a click outside, 0 for a plain message.  The highlight recolours the row:
 * grey 7 -> cyan 11 and the texture's 22 -> 3.  An odd y (DLG_DARK here) draws no box and
 * takes the background colour from the screen. */
int dialog(const char *text, int x, int y, int disabled, int flags)
{
    static uint8_t under[SCR_H][SCR_W];
    Lines L;
    int font = dialog_font, lh = font_height(font), w = 0, h, i, sel = dialog_default >= 0 ? dialog_default : 0,
        prev = -1, k, r = -1, first = -1;
    int bg = 7, bg2 = 22, plain, right, line, done, checks;
    /* (an odd y, but 139, means no box: the background is the screen's) */
    int boxless = (flags & DLG_DARK) || (y >= 0 && (y & 1) && y != 139);

    int adv = dialog_advisor, help = dialog_help;

    if (lh == 9)
        lh = 8;
    dialog_advisor = -1;
    dialog_help = 0;
    dialog_default = -1;
    checks = dialog_checks;
    dialog_checks = 0;
    split(text, &L, 280, font);
    for (i = 0; i < L.n; i++) {
        int tw = text_width(font, L.text[i]);
        if (tw > w)
            w = tw;
        if (L.option[i] >= 0 && first < 0)
            first = i;
    }
    if (L.n > (192 - (y < 0 ? 8 : y)) / lh)
        L.n = (192 - (y < 0 ? 8 : y)) / lh;
    h = L.n * lh + 6 + (help ? 2 : 0);
    if (x < 0)
        x = 80 + (240 - (w + 10)) / 2;
    if (y < 0)
        y = 8 + (192 - (h + 2)) / 2;
    if (x + w + 10 > 320)
        x = 320 - (w + 10);
    if (x < 0)
        x = 0;
    if (boxless) {
        bg = gfx_get(x, y);
        bg2 = -1;
    }
    plain = bg == 15 ? 3 : 15;
    right = x + w + 8;
    memcpy(under, screen, sizeof under);
    if (!boxless && adv < 0)
        dialog_box_at(x, y, w + 8, h, bg);
    else if (!boxless) {
        /* (the speaker: its picture left of the box, its title above the text, underlined) */
        char title[40];
        int tw, d = h - 52;
        snprintf(title, sizeof title, "%s%s", speakers[adv], adv <= 2 ? " report:" : "");
        tw = text_width(font, title);
        if (right < x + tw + 8)
            right = x + tw + 8;
        dialog_box_at(x - 42, y - 8, right - x + 42, h + 8 < 61 ? 61 : h + 8, bg);
        if (adv > 2)
            gfx_blit(&sp299, adv * 40 + 40, 140, 40, 60, x - 40, y - 6 + (d > 0 ? d - 1 : 0), -1);
        draw_text(font, x + 5, y - 4, 15, title);
        gfx_hline(x + 5, x + 5 + tw, y + 3, 11);
    }
    if (help)
        draw_text(1, right - 74, y + h - 4, 10, "(HELP AVAILABLE)");   /* (font 2) */
    for (i = 0; i < L.n; i++) {
        int o = L.option[i], ty = y + 5 + i * lh;
        if (o < 0) {
            if (lh > 9)
                draw_text(font, x + 5, ty + 1, 0, L.text[i]);   /* (a shadow under big fonts) */
            draw_text(font, x + 5, ty, plain, L.text[i]);
        } else {
            char line[100];
            snprintf(line, sizeof line, "%s", L.text[i]);
            if (o < 32 && (checks & (1 << o)))
                line[0] = '^';                  /* (the font's check mark) */
            draw_text(font, x + 5, ty, (disabled & (1 << o)) ? 3 : 0, line);
        }
    }
    if (flags & DLG_DRAW) {
        gfx_present();
        return -1;
    }
    if (L.options == 0 || (flags & DLG_NOSEL)) {
        for (;;) {
            k = gfx_wait_key();
            if (k == KEY_MOUSE || k == 13 || k == 27 || k == ' ' || k < 0x100 || k == KEY_QUIT)
                break;
        }
        memcpy(screen, under, sizeof under);
        gfx_present();
        return 0;
    }
    for (done = 0; !done;) {
        if (sel < 0 || sel >= L.options)
            sel = -1;
        if (sel != prev) {
            if (prev >= 0) {
                line = y + (prev + first) * lh + 4;
                gfx_recolour(x + 3, line, w + 5, lh, 11, bg);
                if (bg2 >= 0)
                    gfx_recolour(x + 3, line, w + 5, lh, 3, bg2);
            }
            if (sel >= 0) {
                line = y + (sel + first) * lh + 4;
                gfx_recolour(x + 3, line, w + 5, lh, bg, 11);
                if (bg2 >= 0)
                    gfx_recolour(x + 3, line, w + 5, lh, bg2, 3);
            }
            prev = sel;
        }
        k = gfx_wait_key();
        switch (k) {
        case KEY_QUIT:
            r = (flags & DLG_ESCAPE) ? -1 : sel;
            done = 1;
            continue;
        case 13:
        case ' ':
            if (sel >= 0 && !(disabled & (1 << sel))) {
                r = sel;
                done = 1;
            }
            continue;
        case 27:
            if (flags & DLG_ESCAPE) {
                r = -1;
                done = 1;
            }
            continue;
        case KEY_MOUSE:
            /* (the press picks the row under the pointer, the release takes it) */
            i = (mouse_y - y - 4) / lh - first;
            if (mouse_y - y - 4 < 0 || mouse_x < x || mouse_x > right)
                i = -1;
            if (i >= 0 && i < L.options && (disabled & (1 << i)))
                continue;
            if ((i < 0 || i >= L.options) && !(flags & DLG_ESCAPE))
                continue;
            sel = i >= 0 && i < L.options ? i : -1;
            r = sel;
            done = 1;
            continue;
        case '8':
        case KEY_UP:
            if (sel > 0)
                sel--;
            continue;
        case '2':
        case KEY_DOWN:
            if (sel < L.options - 1)
                sel++;
            continue;
        default:
            /* a letter: the next option (after the highlighted one) with that key letter */
            for (i = 0, line = 0; i < L.n; i++) {
                int o = L.option[i];
                if (o > sel && o < 32 && L.text[i][1] && ((L.text[i][1] ^ k) & 31) == 0) {
                    sel = o;
                    break;
                }
            }
            continue;
        }
    }
    if (r >= 0 && r != prev) {               /* (the row picked by the mouse lights up) */
        if (prev >= 0) {
            line = y + (prev + first) * lh + 4;
            gfx_recolour(x + 3, line, w + 5, lh, 11, bg);
            if (bg2 >= 0)
                gfx_recolour(x + 3, line, w + 5, lh, 3, bg2);
        }
        line = y + (r + first) * lh + 4;
        gfx_recolour(x + 3, line, w + 5, lh, bg, 11);
        if (bg2 >= 0)
            gfx_recolour(x + 3, line, w + 5, lh, bg2, 3);
    }
    if (!(flags & DLG_KEEP))
        memcpy(screen, under, sizeof under);
    gfx_present();
    return r;
}

/* A line of text typed by the player (names). */
int text_input(const char *prompt, char *buf, int max)
{
    static uint8_t under[SCR_H][SCR_W];
    int len = (int)strlen(buf), k, x = 90, y = 80, w = 220, h = 30;

    memcpy(under, screen, sizeof under);
    for (;;) {
        gfx_fill(x, y, w, h, 7);
        frame3d(x, y, w - 1, h - 1, 15, 8);
        draw_text(dialog_font, x + 6, y + 4, 0, prompt);
        gfx_fill(x + 6, y + 15, w - 12, 10, 15);
        draw_text(dialog_font, x + 8, y + 16, 0, buf);
        gfx_vline(x + 8 + text_width(dialog_font, buf), y + 16, y + 23, 0);
        k = gfx_wait_key();
        if (k == 13 || k == KEY_QUIT)
            break;
        if (k == 27) {
            len = -1;
            break;
        }
        if (k == 8 && len > 0)
            buf[--len] = 0;
        else if (k >= ' ' && k < 0x7F && len < max) {
            buf[len++] = (char)k;
            buf[len] = 0;
        }
    }
    memcpy(screen, under, sizeof under);
    gfx_present();
    return len;
}

/* o_23_06ba: the field's characters (the buffer is padded with spaces to len). */
static void field_draw(int x, int y, int len, const char *buf, int cursor, int *cx, int *cw)
{
    char ch[2] = { 0, 0 };
    int i, w;

    x += 2;
    y += 2;
    for (i = 0; i < len; i++) {
        gfx_fill(x, y, 8, 8, 15);
        if ((size_t)i >= strlen(buf))
            continue;
        ch[0] = buf[i];
        draw_text(dialog_font, x, y, 0, ch);
        w = text_width(dialog_font, ch);
        if (i == cursor) {
            *cx = x;
            *cw = w;
        }
        x += w;
    }
}

/* o_23_040d: edit buf (len characters) in a field at (x, y).  The first key typed replaces
 * the text; Enter keeps it, Escape returns 0.  Trailing spaces are dropped. */
int text_field(int x, int y, int len, char *buf)
{
    char pad[64], tmp[64];
    int cursor = 0, first = 1, ok = 1, k, i, cx = x + 2, cw = 8;

    snprintf(pad, sizeof pad, "%s", buf);
    for (i = (int)strlen(pad); i < len && i < 63; i++)
        pad[i] = ' ';
    pad[len] = 0;
    gfx_fill(x, y, len * 7, 11, 15);
    gfx_rect(x, y - 1, len * 8 + 8, 13, 0);
    field_draw(x, y, len, pad, cursor, &cx, &cw);
    for (;;) {
        gfx_recolour(cx, y + 1, cw, 10, 15, 11);         /* the cursor blinks */
        gfx_delay(100);
        gfx_recolour(cx, y + 1, cw, 10, 11, 15);
        k = gfx_poll_wait();
        if (!k) {
            gfx_delay(100);
            continue;
        }
        if (k == 13 || k == KEY_MOUSE || k == KEY_QUIT)
            break;
        if (k == 27) {
            ok = 0;
            break;
        }
        if (k == KEY_LEFT && cursor > 0)
            cursor--;
        if (k == KEY_RIGHT && cursor < len - 1)
            cursor++;
        if (k == 8 && cursor > 0) {
            memmove(pad + cursor - 1, pad + cursor, (size_t)(len - cursor));
            pad[len - 1] = ' ';
            pad[len] = 0;
            cursor--;
        }
        if (k >= 32 && k < 128) {
            if (first)
                memset(pad, ' ', (size_t)len);
            pad[cursor] = (char)k;
            if (cursor < len - 1)
                cursor++;
        }
        first = 0;
        snprintf(tmp, sizeof tmp, "%s", pad);
        field_draw(x, y, len, tmp, cursor, &cx, &cw);
    }
    for (i = len - 1; i > 0 && pad[i] == ' '; i--)
        pad[i] = 0;
    if (ok)
        strcpy(buf, pad);
    return ok;
}

/* o_23_0000: "City Name..." over the map; an empty name asks again. */
int city_name_prompt(char *buf, int len)
{
    static uint8_t under[SCR_H][SCR_W];
    char work[64];
    int ok;

    gfx_cursor(0);
    memcpy(under, screen, sizeof under);
    do {
        gfx_fill(80, 80, 160, 32, 15);
        gfx_rect(80, 80, 160, 32, 11);
        draw_text(dialog_font, 88, 82, 0, "City Name...");
        snprintf(work, sizeof work, "%s", buf);
        ok = text_field(88, 96, len, work);
    } while (!work[0]);
    if (ok)
        snprintf(buf, (size_t)len + 1, "%s", work);
    gfx_cursor(1);
    memcpy(screen, under, sizeof under);
    gfx_present();
    return ok;
}

/* o_23_00d6 / o_23_0173: "Your Name..." and "Name of your Tribe..." */
int name_prompt(const char *prompt, char *buf, int len)
{
    int r;

    gfx_cursor(0);
    gfx_fill(158, 88, 160, 32, 15);
    gfx_rect(158, 88, 160, 32, 11);
    draw_text(dialog_font, 166, 90, 0, prompt);
    r = text_field(166, 104, len, buf);
    gfx_cursor(1);
    return r;
}
