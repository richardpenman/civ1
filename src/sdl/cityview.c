/* The city view (overlay 19's drawing): the city laid out by city_view_layout() on the hill
 * picture, houses and roads from CITYPIX1, buildings from CITYPIX2/3, wonders from WONDERS
 * and WONDERS2, the citizens from POP.PIC. */
#include <stdio.h>
#include <string.h>
#include "gfx.h"
#include "font.h"
#include "assets.h"
#include "mapview.h"
#include "dialog.h"
#include "../game.h"
#include "../civ.h"
#include "../util.h"
#include "../city.h"
#include "../ui.h"

#define S (&game.s)

extern const char *shot_path;

/* far 277b: where each wonder is in WONDERS(2).PIC: x1, y1, x2, y2 */
static const short wonder_rect[22][4] = {
    { 0, 0, 0, 0 }, { 131, 54, 318, 82 }, { 159, 149, 227, 198 }, { 88, 97, 211, 135 },
    { 229, 116, 268, 198 }, { 61, 117, 101, 172 }, { 164, 97, 227, 147 }, { 1, 38, 66, 118 },
    { 268, 53, 318, 114 }, { 9, 117, 59, 172 }, { 126, 1, 182, 53 }, { 276, 1, 318, 51 },
    { 103, 139, 157, 198 }, { 184, 1, 224, 69 }, { 40, 69, 101, 115 }, { 1, 14, 147, 33 },
    { 253, 1, 274, 47 }, { 226, 1, 251, 47 }, { 103, 79, 162, 137 }, { 270, 116, 318, 198 },
    { 63, 1, 124, 54 }, { 240, 60, 266, 114 },
};
#define SPECIAL_WONDERS 0x808A   /* Pyramids, Colossus, Great Wall, Hoover Dam */

static Picture hill, pix1, pix2, wonders, wonders2, pop;
static int loaded, pix2_kind = -1;

static void load_all(int owner)
{
    int kind = civ_has_advance(owner, 37) ? 3 : 2;

    if (!loaded) {
        load_picture("HILL.PIC", &hill);
        load_picture("CITYPIX1.PIC", &pix1);
        load_picture("WONDERS.PIC", &wonders);
        load_picture("WONDERS2.PIC", &wonders2);
        load_picture("POP.PIC", &pop);
        loaded = 1;
    }
    if (pix2_kind != kind) {
        pic_free(&pix2);
        load_picture(kind == 3 ? "CITYPIX3.PIC" : "CITYPIX2.PIC", &pix2);
        pix2_kind = kind;
    }
}

static int cell(const signed char *g, int x, int y)
{
    if (x < 0 || x >= 19 || y < 0 || y >= 12)
        return -1;
    return g[x * 12 + y];
}

static int px_of(int x, int y)
{
    return ((y & 1) << 3) + x * 16;
}

static void road(int x, int y, int mask, int owner)
{
    int top = civ_has_advance(owner, 58) ? 81 : 65;

    sprite_draw(spr(&pix1, (mask & 7) * 24, top + (mask >> 3) * 8, 24, 8), px_of(x, y) - 5, y * 8 + 50);
}

static Sprite wonder_sprite(int w)
{
    const short *r = wonder_rect[w];
    const Picture *p = ((1 << w) & SPECIAL_WONDERS) && w < 16 ? &wonders2 : &wonders;

    return spr(p, r[0], r[1], r[2] - r[0] + 1, r[3] - r[1] + 1);
}

static int road_neighbour(const signed char *g, int x, int y, int k)
{
    int off = (y & 1) ? (around_dy[k] >= 0 ? 0 : 1) : (around_dy[k] > 0 ? -1 : 0);

    return cell(g, off + around_dx[k] + x, around_dy[k] + y);
}

static void draw_layout(int city, int mode, const signed char *layout)
{
    City *c = &S->cities[city];
    int owner = (int8_t)c->owner, x, y, v, k, mask, kind, variant, w, h, i;
    signed char g[19 * 12];
    char buf[64];

    /* (L0a35) a road with nothing next to it is not drawn */
    memcpy(g, layout, sizeof g);
    for (y = 0; y < 12; y++)
        for (x = 0; x < 19; x++) {
            if (g[x * 12 + y] != -2)
                continue;
            for (k = 1; k <= 8; k++)
                if (road_neighbour(g, x, y, k) >= 0)
                    break;
            if (k > 8)
                g[x * 12 + y] = -1;
        }

    gfx_blit(&hill, 0, 0, 320, 200, 0, 0, -1);
    /* the four wonders drawn in fixed places */
    for (k = 1; k <= 7; k++) {
        w = k == 2 ? 15 : k;
        if (S->wonder_city[w] != city || w + 24 == mode || !((1 << w) & SPECIAL_WONDERS))
            continue;
        x = wonder_rect[w][0];
        sprite_draw(wonder_sprite(w), x + (x < 10 ? -1 : 2) + (w == 3 ? 80 : 0),
                    w > 7 ? wonder_rect[w][1] - 6 : 0);
    }
    for (y = 0; y < 12; y++)
        for (x = 0; x < 19; x++) {
            v = cell(g, x, y);
            if (v == -1) {
                if (x > 2 && cell(g, x - 1, y) != -1 && ((x + y) & 1))
                    road(x, y, 0, owner);
                continue;
            }
            if (v == 15)
                continue;
            if (v == -2) {
                mask = 0;
                for (k = 1; k <= 8; k += 2) {
                    mask >>= 1;
                    if (road_neighbour(g, x, y, k) == -2)
                        mask |= 8;
                }
                road(x, y, mask, owner);
                continue;
            }
            if (v < 16) {
                kind = v / 2;
                variant = v < 2 ? (v & 1) * 2 + (city & 1) : ((x + y) & 1) + (v & 1) * 2;
                sprite_draw(spr(&pix1, kind * 64 + 1 + (variant & 1) * 32, 1 + (variant >> 1) * 32, 31, 31),
                            px_of(x, y), y * 8 + 26);
                continue;
            }
            if (mode + 14 == v || mode + 40 == v)
                continue;                       /* (just built: it appears in the animation) */
            k = v - 16;
            if (k < 48) {                       /* a building (bit k + 1) */
                sprite_draw(spr(&pix2, (k / 4) * 50 + 1, (k & 3) * 50 + 1, 49, 49),
                            clamp(px_of(x, y) - 9, 0, 999), y * 8 + 16);
            } else {
                w = k - 48;
                h = wonder_rect[w][3] - wonder_rect[w][1] + 1;
                sprite_draw(wonder_sprite(w), clamp(px_of(x, y) - 9, 0, 999), y * 8 - h + 76 - 10);
            }
        }
    /* city walls, and the coastal defences */
    if ((c->buildings & BIT(B_CITY_WALLS)) && mode != 8) {
        for (i = 0; i < 320; i += 43) {
            if (i == 172)
                i += 19;
            sprite_draw(spr(&pix2, (22 / 4) * 50 + 1, (22 & 3) * 50 + 1, 49, 49), i, 108);
        }
        sprite_draw(spr(&pix2, (6 / 4) * 50 + 1, (6 & 3) * 50 + 1, 49, 49), 142, 108);
    }
    if ((c->buildings & (1UL << 12)) && mode != 13) {
        for (i = 0; i < 310; i += 46)
            sprite_draw(spr(&pix2, (21 / 4) * 50 + 1, (21 & 3) * 50 + 1, 49, 49), i, 115);
        sprite_draw(spr(&pix2, (11 / 4) * 50 + 1, (11 & 3) * 50 + 1, 49, 49), 0, 115);
    }
    snprintf(buf, sizeof buf, "%s", S->city_names[c->name]);
    draw_text_centered(5, 161, 3, 0, buf);
    draw_text_centered(5, 160, 2, 15, buf);
    snprintf(buf, sizeof buf, "%d %s", S->year < 0 ? -S->year : S->year, S->year < 0 ? "BC" : "AD");
    draw_text_centered(5, 161, 16, 0, buf);
    draw_text_centered(5, 160, 15, 15, buf);
}

/* o_19_1110: the citizens in a row (happy, content, unhappy, specialists). */
static void draw_people(int city, int x, int y, int width)
{
    City *c = &S->cities[city];
    int size = (int8_t)c->size, step, i, happy, unhappy, spec = (int)(c->worked >> 26), content, top;

    top = civ_has_advance(S->human_civ, 35) ? 1 : 52;
    step = size * 12 > width ? width / (size ? size : 1) : 12;
    happy = clamp(calc.happy, 0, size);
    unhappy = clamp(calc.unhappy, 0, size);
    while (happy + unhappy > clamp(size - spec, 0, 99)) {
        if (happy > 0)
            happy--;
        if (unhappy > 0)
            unhappy--;
    }
    content = size - happy - unhappy - spec;
    for (i = 0; i < happy; i++, x += step)
        sprite_draw(spr(&pop, (i & 1) * 35 + 1, top, 34, 50), x, y);
    if (happy)
        x += 8;
    for (i = 0; i < content; i++, x += step)
        sprite_draw(spr(&pop, (2 + (i & 1)) * 35 + 1, top, 34, 50), x, y);
    if (content > 0)
        x += 8;
    for (i = 0; i < unhappy; i++, x += step)
        sprite_draw(spr(&pop, (4 + (i & 1)) * 35 + 1, top, 34, 50), x, y);
    x += 12;
    for (i = 0; i < spec && i < 8; i++, x += step) {
        int t = (c->specialists >> (2 * i)) & 3;
        sprite_draw(spr(&pop, (5 + t) * 35 + 1, top, 34, 50), x, y);
    }
}

void ui_city_view(int city, int mode, const signed char *grid)
{
    static uint8_t under[SCR_H][SCR_W];

    load_all((int8_t)S->cities[city].owner);
    memcpy(under, screen, sizeof under);
    gfx_set_palette(hill.has_palette ? (const uint8_t (*)[3])hill.palette : (const uint8_t (*)[3])game_palette);
    draw_layout(city, mode, grid);
    if (mode == -1)
        draw_people(city, 24, 140, 256);
    gfx_present();
    if (shot_path) {
        gfx_save_bmp(shot_path);
        return;
    }
    gfx_wait_key();
    gfx_set_palette(game_palette);
    memcpy(screen, under, sizeof under);
    gfx_present();
}
