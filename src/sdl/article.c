/* A Civilopedia article (o_08_062a): a picture from the icon pages (ICONPGn / ICONPGA.. /
 * ICONPGT1-2) or CITYPIX2, the name, the kind, and the BLURB text. */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "gfx.h"
#include "font.h"
#include "assets.h"
#include "dialog.h"
#include "article.h"
#include "../game.h"
#include "../civ.h"
#include "../util.h"

void building_icon(int b, int x, int y);
void frame_border(int style);

/* far 2732: the order of the pictures on the icon pages */
static const signed char adv_pic[72] = {
    24, 32, 11, 65, 26, 36, 13, 59, 53, 44, 39, 5, 1, 51, 4, 35, 16, 50, 52, 38, 29, 49, 22, 0,
    21, 10, 33, 60, 19, 37, 45, 12, 15, 43, 30, 40, 2, 41, 17, 27, 18, 3, 61, 56, 57, 6, 34, 55,
    14, 62, 20, 58, 23, 8, 64, 66, 63, 31, 9, 48, 42, 7, 47, 54, 28, 25, 46, -1, 4, 21, 19, 14,
};
static const signed char unit_pic[28] = {
    4, 21, 19, 14, 24, 15, 13, 18, 16, 17, 8, 9, 12, 26, 20, 6, 1, 22, 25, 23, 11, 0, 10, 5, 2, 3, 27, 7,
};
static const signed char terrain_pic[12] = { 6, 7, 8, 9, 10, 11, 0, 2, 3, 1, 4, 5 };

static int index_of(const signed char *t, int n, int v)
{
    int i;

    for (i = 0; i < n; i++)
        if (t[i] == v)
            return i;
    return -1;
}

/* o_08_16c4: an advance's picture on the icon pages (ICONPG1 + k / 9, k % 9 on the page) */
int advance_icon(int adv)
{
    return index_of(adv_pic, 72, adv);
}

/* o_08_16f7: a w x h piece of the page (clipped at its right and bottom edges, and to what
 * fits left of cx), centred on (cx, cy). */
void icon_centre(const Picture *p, int sx, int sy, int w, int h, int cx, int cy)
{
    if (sx + w > 319)
        w = 319 - sx;
    if (sy + h > 199)
        h = 199 - sy;
    if (cx < w / 2) {
        sx += w / 2 - cx;
        w = cx * 2;
    }
    gfx_blit(p, sx, sy, w, h, cx - w / 2, cy - h / 2, -1);
}


static void upper_key(char *key, size_t n, const char *name)
{
    size_t i;

    snprintf(key, n, "*%s", name);
    for (i = 1; key[i]; i++)
        key[i] = (char)toupper((unsigned char)key[i]);
}

/* f_1dde_047b: a picture's palette, but for the 16 standard colours; colour 45 is brown. */
static void palette_from(const char *name)
{
    uint8_t pal[256][3];
    Picture p;

    if (load_picture(name, &p) != 0)
        return;
    if (p.has_palette) {
        memcpy(pal, p.palette, sizeof pal);
        memcpy(pal, game_palette, 16 * 3);
        pal[45][0] = 51 * 255 / 63;
        pal[45][1] = 39 * 255 / 63;
        pal[45][2] = 25 * 255 / 63;
        gfx_set_palette((const uint8_t (*)[3])pal);
    }
    pic_free(&p);
}

static void line_at(int x, int y, int colour, const char *s)     /* f_0181_005e (font 7) */
{
    draw_text(6, x, y, colour, s);
}

/* o_08_062a: an article.  On a light grey page inside the screen border (`style`, the random
 * f_0860_25e1), the picture top left, the name in capitals (font 6) and its kind centred on
 * x 204 (224 for units, 160 for concepts and wonders); with Civilopedia Text on the BLURBn
 * text first (a page of its own), then the facts: what an advance requires and allows (and
 * where it came from), what a building costs, a unit's strengths, a terrain's yields. */
void article_show(int category, int item, const char *concept, int style)
{
    static uint8_t under[SCR_H][SCR_W];
    uint8_t pal[256][3];
    Picture p;
    char name[64], key[80], file[16], text[4096], line[160], num[16];
    const char *kind = "";
    int k, i, y, bg = 253, cx = 204, first, src;

    memcpy(under, screen, sizeof under);
    memcpy(pal, game_palette, sizeof pal);
    pal[253][0] = pal[253][1] = pal[253][2] = 61 * 255 / 63;     /* (f_1dde_0688(253, 61, 61, 61)) */
    gfx_set_palette((const uint8_t (*)[3])pal);
    gfx_fill(0, 0, 320, 200, bg);
    frame_border(style);
    if (category == 2)
        cx = 224;
    if (category == 4 || (category == 1 && item > 24))
        cx = 160;
    memset(&p, 0, sizeof p);
    switch (category) {
    case 0:
        k = index_of(adv_pic, 72, item);
        snprintf(file, sizeof file, "ICONPG%d.PIC", 1 + k / 9);
        if (k >= 0 && load_picture(file, &p) == 0) {
            palette_from("ICONPG1.PAL");
            icon_centre(&p, (k % 3) * 111 + 1, ((k % 9) / 3) * 69 + 1, 110, 68, 62, 42);
        }
        snprintf(name, sizeof name, "%s", advance_types[item].name);
        kind = "Civilization Advance";
        break;
    case 1:
        if (item > 1 && item <= 21 && load_picture("CITYPIX2.PIC", &p) == 0) {
            palette_from("HILL.PAL");
            gfx_blit(&p, ((item - 2) / 4) * 50 + 1, ((item - 2) & 3) * 50 + 1, 49, 49, 37, 17, 0);
        }
        snprintf(name, sizeof name, "%s", building_types[item].name);
        kind = item > 24 ? "Wonder of the World" : "City Improvement";
        break;
    case 2:
        k = index_of(unit_pic, 28, item);
        snprintf(file, sizeof file, "ICONPG%c.PIC", 'A' + k / 6);
        if (k >= 0 && load_picture(file, &p) == 0) {
            palette_from("ICONPGA.PAL");
            icon_centre(&p, (k & 1) * 160 + 1, ((k % 6) / 2) * 61, 158, 61, 88, 42);
        }
        snprintf(name, sizeof name, "%s", game.s.unit_type_names[item]);
        kind = "Military Unit";
        break;
    case 3:
        k = index_of(terrain_pic, 12, item);
        snprintf(file, sizeof file, "ICONPGT%d.PIC", 1 + k / 6);
        if (k >= 0 && load_picture(file, &p) == 0) {
            Picture sp;
            if (load_picture("SP256.PAL", &sp) == 0 && sp.has_palette) {
                memcpy(pal, sp.palette, sizeof pal);
                pal[253][0] = pal[253][1] = pal[253][2] = 61 * 255 / 63;
                gfx_set_palette((const uint8_t (*)[3])pal);
            }
            pic_free(&sp);
            icon_centre(&p, (k % 3) * 107 + 1, ((k % 6) / 3) * 87 + 5, 106, 77, 76, 46);
        }
        snprintf(name, sizeof name, "%s", terrain_types[item].name);
        kind = "Terrain Type";
        break;
    default:
        snprintf(name, sizeof name, "%s", concept ? concept : "");
        kind = "Game Concept";
        break;
    }
    pic_free(&p);
    draw_text_centered(6, cx, 36, 7, kind);
    for (i = 0; name[i]; i++)
        name[i] = (char)toupper((unsigned char)name[i]);
    draw_text_centered(5, cx, 20, 0, name);                       /* (font 6) */

    snprintf(file, sizeof file, "BLURB%d.TXT", category);
    snprintf(key, sizeof key, "*%s", name);
    y = 76;
    if ((game.s.unk_19c0 & 64) && text_section_lines(file, key, text, sizeof text) >= 0) {
        text_wrapped(text, 6, 37, 12, 76, 1);
        gfx_present();
        gfx_wait_key();
        gfx_fill(8, 76, 304, 116, bg);
    }
    strcat(key, "2");
    if (category != 0) {
        text_section_lines(file, key, text, sizeof text);
        y = text_wrapped(text, 6, 36, 12, y, 1) + 8;
    }

    switch (category) {
    case 0: {
        const AdvanceType *a = &advance_types[item];
        y += 8;
        line[0] = 0;
        if (a->prereq[0] != -1)
            snprintf(line, sizeof line, "Requires %s", advance_types[(int)a->prereq[0]].name);
        if (a->prereq[1] != -1)
            snprintf(line + strlen(line), sizeof line - strlen(line), " and %s", advance_types[(int)a->prereq[1]].name);
        line_at(32, y, 1, line);
        y += 16;
        line_at(32, y, 1, "Allows: ");
        y += 8;
        first = y;
        for (i = 0; i < NUM_ADVANCES; i++)
            for (k = 0; k < 2; k++) {
                int other = advance_types[i].prereq[1 - k];
                if (advance_types[i].prereq[k] != item)
                    continue;
                snprintf(line, sizeof line, "%s", advance_types[i].name);
                if (other != -1)
                    snprintf(line + strlen(line), sizeof line - strlen(line), " (with %s)", advance_types[other].name);
                line_at(40, y, 9, line);
                y += 8;
            }
        y += 4;
        for (i = 0; i < NUM_UNIT_TYPES; i++)
            if (game.s.unit_types[i].prereq == item) {
                sprite_draw(unit_sprite(game.s.human_civ, i), 40, y - 4);
                snprintf(line, sizeof line, "%s unit", game.s.unit_type_names[i]);
                line_at(60, y, 12, line);
                y += 12;
            }
        for (i = 1; i <= 45; i++)
            if (building_types[i].prereq == item) {
                building_icon(i, 40, y - 2);
                snprintf(line, sizeof line, "%s%s", building_types[i].name, i > 24 ? " Wonder" : " improvement");
                line_at(60, y, 2, line);
                y += 12;
            }
        if (first + 4 == y)
            line_at(32, y - 12, bg, "Allows: ");
        src = game.s.advance_source[game.s.human_civ][item];
        line[0] = 0;
        if (src == 0xFE)
            snprintf(line, sizeof line, "(from Great Library)");
        else if (src == 0)
            snprintf(line, sizeof line, "(Discovered)");
        else if (src != 0xFF)
            snprintf(line, sizeof line, "(Taken from %s)", game.s.civ_names[src & 7]);
        draw_text_centered(6, cx, 48, 7, line);
        break;
    }
    case 1:
        snprintf(line, sizeof line, "Requires %s",
                 building_types[item].prereq >= 0 ? advance_types[building_types[item].prereq].name : "");
        line_at(12, y, 9, line);
        y += 8;
        snprintf(line, sizeof line, "Cost: %d shields.", building_types[item].cost * 10);
        line_at(12, y, 9, line);
        y += 8;
        snprintf(line, sizeof line, "Maintenance: $%d", building_types[item].upkeep);
        line_at(12, y, 12, line);
        break;
    case 2: {
        const UnitType *u = &game.s.unit_types[item];
        snprintf(line, sizeof line, "Requires %s%s", u->prereq >= 0 ? advance_types[u->prereq].name : "",
                 item == 25 ? " and Nuclear Fission" : "");
        line_at(100, y, 9, line);
        y += 8;
        snprintf(line, sizeof line, "Cost: %d resources.", u->cost * 10);
        line_at(100, y, 9, line);
        y += 8;
        snprintf(line, sizeof line, "Attack Strength: %d", u->attack);
        line_at(100, y, 12, line);
        y += 8;
        snprintf(line, sizeof line, "Defense Strength: %d", u->defense);
        line_at(100, y, 12, line);
        y += 8;
        snprintf(line, sizeof line, "Movement Rate: %d", u->moves);
        line_at(100, y, 0, line);
        break;
    }
    case 3: {
        const TerrainImprove *ti = &terrain_improve[item];
        int t, v;
        for (t = item; t <= item + 12; t += 12) {
            const TerrainType *tt = &terrain_types[t];
            if (t != item && terrain_types[item].food == 2)
                continue;
            line_at(12, y, 1, tt->name);
            y += 8;
            if (tt->food != 0 || ti->irrigate < -1) {
                snprintf(line, sizeof line, "Food: %d%s", tt->food, tt->food >= 3 ? "*" : "");
                if (ti->irrigate < -1) {
                    snprintf(num, sizeof num, "%d", tt->food - ti->irrigate - 1);
                    snprintf(line + strlen(line), sizeof line - strlen(line), " (%s%s with Irrigation)", num,
                             tt->food >= 2 ? "*" : "");
                }
                strcat(line, " units.");
                line_at(16, y, 9, line);
                y += 8;
            }
            if (tt->shields != 0 || ti->mine < -1) {
                snprintf(line, sizeof line, "Production: %s%d", item == 2 || item == 11 ? "0/" : "", tt->shields);
                if (ti->mine < -1) {
                    v = tt->shields - ti->mine - 1;
                    snprintf(line + strlen(line), sizeof line - strlen(line), " (%d%s with Mining)", v, v >= 3 ? "*" : "");
                }
                strcat(line, " units.");
                line_at(16, y, 9, line);
                y += 8;
            }
            if (tt->trade != 0 || item <= 2) {
                snprintf(line, sizeof line, "Trade: %d%s%s", tt->trade, tt->trade ? "%" : "", tt->trade >= 3 ? "*" : "");
                if (item <= 2)
                    snprintf(line + strlen(line), sizeof line - strlen(line), " (%d%% with Roads)", tt->trade + 1);
                line_at(16, y, 9, line);
                y += 8;
            }
            if (t == 7) {
                line_at(16, y, 9, "nothing");
                y += 8;
            }
            y += 4;
        }
        line_at(16, y, 9, "*  -1 if government is Despotism/Anarchy.");
        y += 8;
        line_at(16, y, 9, "%  +1 if government is Republic/Democracy.");
        y += 12;
        snprintf(line, sizeof line, "Movement cost: %d MP", terrain_types[item].move_cost);
        line_at(12, y, 12, line);
        y += 8;
        snprintf(line, sizeof line, "Defense bonus: +%d%%", terrain_types[item].defense * 50 - 100);
        line_at(12, y, 12, line);
        break;
    }
    }
    gfx_present();
    gfx_wait_key();
    gfx_set_palette(game_palette);
    memcpy(screen, under, sizeof under);
    gfx_present();
}

/* An article from its BLURB key ("*PYRAMIDS"): which kind of thing it is. */
void article_by_key(const char *key)
{
    char k[80];
    int i;

    for (i = 0; i < NUM_ADVANCES; i++) {
        upper_key(k, sizeof k, advance_types[i].name);
        if (advance_types[i].name[0] && !strcmp(k, key)) {
            article_show(0, i, NULL, rnd(2));
            return;
        }
    }
    for (i = 1; i < NUM_BUILDINGS; i++) {
        upper_key(k, sizeof k, building_types[i].name);
        if (!strcmp(k, key)) {
            article_show(1, i, NULL, rnd(2));
            return;
        }
    }
    for (i = 0; i < NUM_UNIT_TYPES; i++) {
        upper_key(k, sizeof k, game.s.unit_type_names[i]);
        if (!strcmp(k, key)) {
            article_show(2, i, NULL, rnd(2));
            return;
        }
    }
    for (i = 0; i < 12; i++) {
        upper_key(k, sizeof k, terrain_types[i].name);
        if (!strcmp(k, key)) {
            article_show(3, i, NULL, rnd(2));
            return;
        }
    }
    article_show(4, 0, key + 1, rnd(2));
}
