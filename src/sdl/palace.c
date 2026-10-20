/* The palace picture (o_17_07ec and its helpers o_17_0cfe, o_17_0e2f, o_17_0e95): seven
 * castle sections (levels unk_e83e[1..7], styles unk_c142[]) drawn from CASTLEn.PIC on
 * CBACK.PIC, the middle garden from CBACKSn.PIC, the side gardens from CBRUSHn.PIC. */
#include <stdio.h>
#include <string.h>
#include "gfx.h"
#include "assets.h"
#include "palace.h"
#include "../game.h"

#define LV(i) (game.s.unk_e83e[i])
#define STYLE(i) (game.s.unk_c142[i])

/* DGROUP 0x4AE6: vertical offset of a piece, [level][style][kind] */
static const signed char piece_dy[5][3][5] = {
    { { 1, 1, 1, 1, 1 }, { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
    { { 1, 0, 0, 2, 2 }, { 1, 0, 0, 1, 1 }, { 1, 0, 0, 2, 2 } },
    { { 1, 0, 0, 2, 2 }, { 1, 0, 0, 1, 1 }, { 1, 0, 0, 2, 2 } },
    { { 1, 3, 3, 2, 2 }, { 1, 0, 0, 1, 1 }, { 1, 0, 0, 2, 2 } },
    { { 1, 3, 3, 2, 2 }, { 1, 0, 0, 1, 1 }, { 1, 0, 0, 2, 2 } },
};

static signed char kind[9][2];      /* g_c17e: the piece of each half of a section, -1 none */
static Picture castle[5];
static int loaded;

/* o_17_0cfe(s, 1): which pieces a section is made of, from its neighbours. */
static void section_kinds(int s)
{
    int lv = LV(s), cls;

    kind[s][0] = kind[s][1] = -1;
    if (lv == -1)
        return;
    cls = 2;
    if (s < 4)
        cls = (LV(s - 1) == -1 || s == 1) ? 0 : 1;
    if (s > 4)
        cls = (LV(s + 1) == -1 || s == 7) ? 3 : 1;
    switch (cls) {
    case 0:
        kind[s][0] = 3;
        if (s == 3)
            kind[s][1] = lv ? 1 : 2;
        break;
    case 1:
        kind[s][0] = (LV(s - 1) > lv || s == 5 || LV(s - 2) == -1) ? 1 : 2;
        kind[s][1] = 2;
        break;
    case 2:
        kind[s][0] = 0;
        break;
    case 3:
        if (s == 5)
            kind[s][0] = (LV(s - 1) > lv || s == 5) ? 1 : 2;
        kind[s][1] = 4;
        break;
    }
}

/* o_17_0e95: the piece's rectangle in CASTLEn.PIC. */
static Sprite piece(int s, int part)
{
    int k = kind[s][part], st = STYLE(s) % 3, ox = (st & 1) * 160, oy = (st & 2) * 50, lv = LV(s);
    const Picture *p = &castle[lv < 0 ? 0 : lv > 4 ? 4 : lv];

    switch (k) {
    case 0: return spr(p, ox, oy + 1, 52, 99);
    case 1: return spr(p, ox + 53, oy + 1, 24, 99);
    case 2: return spr(p, ox + 78, oy + 1, 24, 99);
    case 3: return st == 1 ? spr(p, 160, 101, 35, 99) : spr(p, ox + 103, oy + 1, 28, 99);
    case 4: return st == 1 ? spr(p, 196, 101, 35, 99) : spr(p, ox + 132, oy + 1, 27, 99);
    }
    return spr(p, 0, 0, 0, 0);
}

/* o_17_0e2f */
static void draw_piece(int level, int s, int x, int y, int part)
{
    int k = kind[s][part], st = STYLE(s) % 3;

    if (k < 0 || level < 0 || level > 4)
        return;
    sprite_draw(piece(s, part), x, piece_dy[level][st][k] + y + level);
}

void palace_draw(int sad)
{
    Picture back;
    char name[16];
    int s, x = 16, y = 36, last = 7, prev = 1, lv, cls;

    if (!loaded) {
        for (s = 0; s < 5; s++) {
            snprintf(name, sizeof name, "CASTLE%d.PIC", s);
            load_picture(name, &castle[s]);
        }
        loaded = 1;
    }
    for (s = 1; s < 8; s++)
        section_kinds(s);
    gfx_fill(0, 0, 320, 200, 0);
    if (load_picture(sad ? "SAD.PIC" : "CBACK.PIC", &back) == 0) {
        if (back.has_palette)
            gfx_set_palette((const uint8_t (*)[3])back.palette);
        gfx_blit(&back, 0, 0, 320, 200, 0, 0, -1);
        pic_free(&back);
    }
    if (LV(10) > 0) {
        snprintf(name, sizeof name, "CBACKS%d.PIC", LV(10));
        if (load_picture(name, &back) == 0) {
            gfx_blit(&back, 0, 0, back.width, back.height, 0, 135, -1);
            pic_free(&back);
        }
    }
    if (LV(2) == -1)
        x -= 24;
    if (STYLE(4) == 1)
        x++;
    while (last > 1 && LV(last) == -1)
        last--;
    for (s = 1; s < 8; s++) {
        lv = LV(s);
        if (lv == -1) {
            x += 48;
            continue;
        }
        cls = 2;
        if (s < 4)
            cls = (LV(s - 1) == -1 || s == 1) ? 0 : 1;
        if (s > 4)
            cls = (LV(s + 1) == -1 || s == 7) ? 3 : 1;
        switch (cls) {
        case 0:
            if (s == 3)
                draw_piece(lv, s, x + 25, y, 1);
            if (STYLE(s) != 1 || s == 3)
                draw_piece(lv, s, x, y, 0);
            x += 25;
            if (s == 3)
                x += 24;
            prev = s;
            break;
        case 1:
            if (prev + 1 == s && STYLE(s) == STYLE(prev))
                draw_piece(lv, prev, x + 24 - 17, y - 2, 0);
            if (last - 1 == s && STYLE(s) == STYLE(last))
                draw_piece(lv, last, x + 24 - 17, y - 2, 1);
            draw_piece(lv, s, x, y, 0);
            x += 24;
            draw_piece(lv, s, x, y, 1);
            x += 24;
            break;
        case 2:
            if (STYLE(4) == 1)
                x--;
            draw_piece(lv, s, x - 2, y, 0);
            x += 48;
            if (STYLE(4) == 1)
                x--;
            break;
        case 3:
            if (s == 5) {
                draw_piece(lv, s, x, y, 0);
                x += 24;
            }
            draw_piece(lv, s, x - (STYLE(s) == 1 ? 3 : 2), y, 1);
            x += 25;
            break;
        }
    }
    if (STYLE(prev) == 1 && prev != 3)
        draw_piece(LV(prev), prev, prev * 48 - 36, LV(prev + 1) - LV(prev) + y, 0);
    if (LV(9) > 0) {
        snprintf(name, sizeof name, "CBRUSH%d.PIC", LV(9) * 2 - 2);
        if (load_picture(name, &back) == 0) {
            gfx_blit(&back, 0, 0, back.width, back.height, 0, LV(9) > 1 ? 94 : 105, 0);
            pic_free(&back);
        }
    }
    if (LV(11) > 0) {
        snprintf(name, sizeof name, "CBRUSH%d.PIC", LV(11) * 2 - 1);
        if (load_picture(name, &back) == 0) {
            gfx_blit(&back, 0, 0, back.width, back.height, 184, LV(11) > 1 ? 94 : 105, 0);
            pic_free(&back);
        }
    }
}
