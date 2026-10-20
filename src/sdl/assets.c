/* Sprite tables of o_05_143e (the VGA path). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "assets.h"
#include "gfx.h"
#include "../game.h"
#include "../civ.h"

Picture sp257, ter257, sp299;
uint8_t game_palette[256][3];
Picture unit_sheet[8];

const short civ_colour[16] = { 12, 15, 10, 9, 14, 11, 13, 7, 4, 7, 2, 1, 10, 3, 4, 8 };
const short civ_colour2[16] = { 4, 7, 2, 1, 10, 3, 4, 8, 172, 180, 190, 199, 209, 218, 229, 234 };

int load_picture(const char *name, Picture *p)
{
    char path[512];

    snprintf(path, sizeof path, "%s/%s", data_dir, name);
    if (pic_load(path, p) != 0) {
        fprintf(stderr, "cannot load %s\n", path);
        return -1;
    }
    return 0;
}

Sprite spr(const Picture *p, int x, int y, int w, int h)
{
    Sprite s;

    s.pic = p;
    s.x = (short)x;
    s.y = (short)y;
    s.w = (short)w;
    s.h = (short)h;
    return s;
}

void sprite_draw(Sprite s, int x, int y)
{
    gfx_blit(s.pic, s.x, s.y, s.w, s.h, x, y, 0);
}

void sprite_copy(Sprite s, int x, int y)
{
    gfx_blit(s.pic, s.x, s.y, s.w, s.h, x, y, -1);
}

static void recolour(Picture *p, int from, int to)
{
    int i;

    for (i = 0; i < p->width * p->height; i++)
        if (p->pixels[i] == from)
            p->pixels[i] = (uint8_t)to;
}

int assets_load(void)
{
    Picture pal;
    int c;

    if (load_picture("SP257.PIC", &sp257) || load_picture("TER257.PIC", &ter257) ||
        load_picture("SP299.PIC", &sp299))
        return -1;
    if (load_picture("SP256.PAL", &pal) == 0 && pal.has_palette)
        memcpy(game_palette, pal.palette, sizeof game_palette);
    else
        memcpy(game_palette, sp257.palette, sizeof game_palette);
    pic_free(&pal);
    /* Units: rows 160..191 recoloured per civ (o_05_143e copies them to row 128 first). */
    for (c = 0; c < 8; c++) {
        Picture *u = &unit_sheet[c];
        memset(u, 0, sizeof *u);
        u->width = 320;
        u->height = 32;
        u->pixels = malloc(320 * 32);
        memcpy(u->pixels, sp257.pixels + 160 * 320, 320 * 32);
        if (civ_colour[c] == 15)
            recolour(u, 15, 11);
        if (civ_colour[c] == 7)
            recolour(u, 7, 3);
        recolour(u, 10, civ_colour[c]);
        recolour(u, 2, civ_colour2[c]);
    }
    return 0;
}

Sprite unit_sprite(int civ, int type)
{
    return spr(&unit_sheet[civ & 7], (type % 20) * 16 + 1, (type / 20) * 16 + 1, 15, 15);
}

/* g_c7a6[terrain][mask]: TER257 row `terrain`; terrains past hills have one picture. */
Sprite terrain_sprite(int terrain, int mask)
{
    if (terrain >= 6)
        mask = 0;
    return spr(&ter257, mask * 16, terrain * 16, 16, 16);
}

/* g_e3ee[n]: 0..6 SP257 (16n, 32); 7 the torch; 8..15 7x7 pieces; 16..31 (16k+1, 113)
 * (terrain specials, city, fortified, fortress, hut); 32..39 the explosion (16k+1, 97). */
Sprite icon_sprite(int n)
{
    if (n < 7)
        return spr(&sp257, n * 16, 32, 16, 16);
    if (n >= 8 && n < 16) {
        int k = n - 8;
        return spr(&sp257, (k & 3) * 8 + 129, (k & 4) * 2 + 33, 7, 7);
    }
    if (n >= 16 && n < 32)
        return spr(&sp257, (n - 16) * 16 + 1, 113, 15, 15);
    if (n >= 32 && n < 40)                       /* the explosion */
        return spr(&sp257, (n - 32) * 16 + 1, 97, 15, 15);
    return spr(&sp257, 0, 0, 0, 0);
}

/* f_0237_1be7: the map's palette is SP256.PAL below 128 and the era's BACKna/m.PAL above
 * (n the government / 2, 3 for communism; m once Invention is known). */
void era_palette(void)
{
    static uint8_t base[256][3];
    static int have_base;
    int h = game.s.human_civ, g = game.s.government[h];
    char name[16];
    Picture p;

    if (!have_base) {
        memcpy(base, game_palette, sizeof base);
        have_base = 1;
    }
    memcpy(game_palette, base, sizeof base);
    snprintf(name, sizeof name, "BACK%c%c.PAL", g == GOV_COMMUNISM ? '3' : '0' + g / 2,
             g != GOV_COMMUNISM && civ_has_advance(h, 20) ? 'M' : 'A');
    if (load_picture(name, &p) == 0 && p.has_palette)
        memcpy(game_palette[128], p.palette[128], 128 * 3);
    pic_free(&p);
    gfx_set_palette(game_palette);
}
