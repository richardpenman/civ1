/* The main screen: map view (seg 1b05), menu bar, status and unit panels (seg 0237/0402).
 * Ported from the VGA paths of the original's drawing code. */
#include <stdio.h>
#include <string.h>
#include "mapview.h"
#include "assets.h"
#include "font.h"
#include "gfx.h"
#include "../game.h"
#include "../turn.h"
#include "../map.h"
#include "../unit.h"
#include "../util.h"
#include "../move.h"
#include "../civ.h"
#include "../city.h"

#define S (&game.s)

int map_font = 0;               /* the font of the map and panels */
int no_units_drawn;             /* g_ec1c */

/* City labels collected while drawing the view (g_6d2c, g_6dbe, g_ee40). */
static int label_n, label_x[32], label_y[32], label_city[32];

static Sprite page2(int x, int y, int w, int h)
{
    return spr(&sp299, x, y, w, h);         /* the sprite cache of page 2 is SP299's right part */
}

/* f_0181_0088: text with a black shadow below. */
void shadow_text(const char *s, int x, int y, int colour)
{
    draw_text(map_font, x, y + 1, 0, s);
    draw_text(map_font, x, y, colour, s);
}

/* f_1d1e_0a74: two-colour frame. */
void frame3d(int x, int y, int w, int h, int light, int dark)
{
    gfx_hline(x, x + w, y, dark);
    gfx_hline(x, x + w, y + h, light);
    gfx_vline(x + w, y, y + h, dark);
    gfx_vline(x, y + 1, y + h, light);
}

/* f_1dde_03cc + f_0237_1bae: a panel of the stone texture with a frame. */
void panel(int x, int y, int w, int h)
{
    int px, py, tw, th, ww, hh;

    for (py = y, hh = h; hh > 0; py += th, hh -= th) {
        th = clamp(hh, 0, 16);
        for (px = x, ww = w; ww > 0; px += tw, ww -= tw) {
            tw = clamp(ww, 0, 32);
            sprite_copy(page2(288, 120, tw, th), px, py);
        }
    }
    frame3d(x, y, w - 1, h - 1, 15, 8);
}

static int square_px(int x, int y, int *px, int *py)
{
    *px = wrap_x(x - view_x) * 16 + 80;
    *py = (y - view_y) * 16 + 8;
    return *px >= 80 && *px < 320 && *py >= 8 && *py <= 192;
}

static int visible_to_human(int x, int y)
{
    if (!map_in_bounds(x, y))
        return 1;
    return (game.s.visibility[wrap_x(x)][y] >> game.s.human_civ) & 1;
}

/* f_1b05_03b1: one square of the view (terrain, improvements, city), no units. */
void draw_square(int x, int y)
{
    int px, py;

    if (square_px(x, y, &px, &py))
        draw_square_at(x, y, px, py, 1);
}

void draw_square_at(int x, int y, int px, int py, int labels)
{
    int t, imp, k, bits, m, q, n, nimp, dot, c, size;
    char buf[16];

    t = map_terrain(x, y);
    imp = show_all ? map_improvements(x, y) : map_known_improvements(x, y);

    if (t == TERRAIN_OCEAN) {
        bits = 0;
        for (k = 1; k < 9; k++) {
            bits >>= 1;
            if (map_terrain(wrap_x(x + around_dx[k]), y + around_dy[k]) != TERRAIN_OCEAN &&
                map_in_bounds(0, y + around_dy[k]))
                bits |= 128;
        }
        m = ((bits >> 6) & 3) + bits * 4;
        for (q = 0; q < 4; q++) {
            n = (m >> (2 * q)) & 7;
            if (q < 2)
                sprite_draw(spr(&ter257, 16 * n + (q & 1) * 8, 176, 8, 8), px + (q & 1) * 8, py);
            else
                sprite_draw(spr(&ter257, 16 * n + 8 - (q & 1) * 8, 184, 8, 8), px + 8 - (q & 1) * 8, py + 8);
        }
        switch (bits) {
        case 28:  sprite_copy(page2(224, 100, 16, 16), px, py); break;
        case 193: sprite_copy(page2(240, 100, 16, 16), px, py); break;
        case 7:   sprite_copy(page2(256, 100, 16, 16), px, py); break;
        case 112: sprite_copy(page2(272, 100, 16, 16), px, py); break;
        case 143: sprite_copy(page2(288, 100, 16, 16), px, py); break;
        case 248: sprite_copy(page2(304, 100, 16, 16), px, py); break;
        }
        for (k = 1; k < 9; k += 2)
            if (map_terrain(wrap_x(x + around_dx[k]), y + around_dy[k]) == TERRAIN_RIVER)
                sprite_draw(spr(&ter257, 128 + 16 * (k / 2), 176, 16, 16), px, py);
    } else {
        sprite_copy(page2(256, 120, 16, 16), px, py);
    }
    if ((imp & IMP_IRRIGATION) && t != TERRAIN_OCEAN && !no_units_drawn && !(imp & IMP_CITY))
        sprite_draw(icon_sprite(4), px, py);
    if (t == TERRAIN_RIVER) {
        bits = 0;
        for (k = 1; k < 9; k += 2) {
            int nt = map_terrain(x + around_dx[k], y + around_dy[k]);
            bits >>= 1;
            if (nt == TERRAIN_RIVER || nt == TERRAIN_OCEAN)
                bits |= 8;
        }
        /* (g_6d7e: SP257 row 64 for 0-15, row 80 for 16-31; bit 8 of the layer adds 16) */
        sprite_draw(spr(&sp257, 16 * bits, (map_pixel(x, y + 150) & 8) ? 80 : 64, 16, 16), px, py);
    }
    if (t != TERRAIN_OCEAN && t != TERRAIN_RIVER) {
        bits = 0;
        for (k = 1; k < 9; k += 2) {
            bits >>= 1;
            if (map_terrain(wrap_x(x + around_dx[k]), y + around_dy[k]) == t && map_in_bounds(0, y + around_dy[k]))
                bits |= 8;
        }
        sprite_draw(terrain_sprite(t, bits), px, py);
        if (t == TERRAIN_GRASSLAND && !(((x * 7 + y * 11) & 0xFF) & 2))
            sprite_draw(spr(&sp257, 153, 41, 7, 7), px + 4, py + 4);
    }
    if (imp & IMP_POLLUTION)
        sprite_draw(icon_sprite(6), px, py);
    if (imp & IMP_ROAD) {
        dot = (imp & IMP_RAILROAD) ? 0 : 6;
        for (k = 1; k <= 8; k++) {
            nimp = map_known_improvements(x + around_dx[k], y + around_dy[k]);
            if (!(nimp & IMP_ROAD))
                continue;
            dot = -1;
            if ((imp & (IMP_CITY | IMP_RAILROAD)) && (nimp & (IMP_CITY | IMP_RAILROAD)))
                sprite_draw(spr(&sp257, 16 * (k - 1) + 128, 96, 16, 16), px, py);
            else
                sprite_draw(spr(&sp257, 16 * (k - 1), 48, 16, 16), px, py);
        }
        if (dot != -1)
            gfx_fill(px + 7, py + 7, 2, 2, dot);
    }
    if ((imp & IMP_MINE) && !no_units_drawn)
        sprite_draw(icon_sprite(5), px, py);
    if (has_special_resource(x, y))
        sprite_draw(icon_sprite(t + 16), px, py);
    if (has_hut(t, x, y))
        sprite_draw(icon_sprite(31), px, py);
    if (imp & IMP_FORTRESS)
        sprite_draw(icon_sprite(30), px, py);
    if (!show_all)
        for (k = 1; k <= 8; k += 2)
            if (!visible_to_human(x + around_dx[k], y + around_dy[k]))
                sprite_draw(spr(&sp257, 80 + 16 * (k / 2), 128, 16, 16), px, py);
    if (!(imp & IMP_CITY) || no_units_drawn)
        return;
    c = city_at(x, y);
    if (c < 0)
        return;
    {
        City *ct = &S->cities[c];
        int owner = (int8_t)ct->owner;
        if (owner != S->human_civ && ct->visible_size == 0 && !show_all)
            return;
        gfx_rect(px + 1, py + 1, 13, 13, 15);
        gfx_rect(px + 2, py + 1, 12, 12, civ_colour2[owner & 7]);
        gfx_fill(px + 2, py + 2, 12, 12, civ_colour[owner & 7]);
        sprite_draw(icon_sprite(28), px + 1, py + 1);
        gfx_recolour(px + 2, py + 2, 12, 12, 5, civ_colour2[owner & 7]);
        size = (owner == S->human_civ || show_all) ? ct->size : ct->visible_size;
        snprintf(buf, sizeof buf, "%d", (int8_t)size);
        if (ct->status & CITY_DISORDER)
            sprite_draw(spr(&sp257, 32, 128, 8, 16), px + 5, py + 2);
        else
            draw_text(map_font, px + ((int8_t)size >= 10 ? 3 : 6), py + 5, 0, buf);
        if (unit_at(x, y) != -1 || ct->fortified[0] != NONE8)
            gfx_rect(px, py, 15, 15, 0);
        if (ct->buildings & BIT(B_CITY_WALLS))
            sprite_draw(icon_sprite(29), px + 1, py + 1);
        if (labels && label_n < 32) {
            label_x[label_n] = px;
            label_y[label_n] = py;
            label_city[label_n++] = c;
        }
    }
}

/* f_1b05_0fa5: a unit's picture with its order letters at (px, py). */
void draw_unit_at(int civ, int u, int px, int py, const Unit *p)
{
    int letter, shade = civ == 1 ? 9 : 15;
    char buf[2] = { 0, 0 };

    sprite_draw(unit_sprite(civ, p->type), px, py);
    if (p->status & UNIT_FORTIFIED)
        sprite_draw(icon_sprite(29), px, py);
    else if (p->status & UNIT_FORTIFYING)
        shadow_text("F", px + 4, py + 7, shade);
    if (civ == S->human_civ && p->goto_x != NONE8)
        shadow_text("G", px + 4, py + 7, shade);
    if ((p->status & (UNIT_ROAD | UNIT_IRRIGATE | UNIT_MINE)) && S->unit_types[p->type].domain != DOMAIN_AIR) {
        letter = 'R';
        if (p->status & UNIT_IRRIGATE)
            letter = p->type == UNIT_SETTLERS ? 'I' : '?';
        if (p->status & UNIT_MINE) {
            letter = 'M';
            if (p->status & UNIT_IRRIGATE)
                letter = 'F';
            if (p->status & UNIT_ROAD)
                letter = 'P';
        }
        buf[0] = (char)letter;
        shadow_text(buf, px + 4, py + 7, shade);
        gfx_rect(px - 1, py - 1, 15, 15, 7);
    }
    if (p->status & UNIT_SENTRY) {
        gfx_recolour(px, py, 16, 16, 5, 7);
        gfx_recolour(px, py, 16, 16, 8, 7);
    }
    (void)u;
}

/* f_1b05_0e1b: a unit where it stands, if that is in the view. */
int draw_unit(int civ, int u)
{
    Unit *p = &S->units[civ][u];
    int px, py;

    if (p->type == NONE8 || !square_px(p->x, p->y, &px, &py))
        return 0;
    if (map_terrain(p->x, p->y) == TERRAIN_OCEAN && (p->status & UNIT_SENTRY) &&
        S->unit_types[p->type].domain != DOMAIN_SEA)
        return 0;                                   /* cargo */
    if (p->next != NONE8)
        sprite_draw(unit_sprite(civ, p->type), px + 1, py + 1);
    draw_unit_at(civ, u, px, py, p);
    return 1;
}

/* f_1b05_124d: the unit shown for a stack (a ship at sea, else the defender). */
static void draw_stack(int civ, int u)
{
    Unit *p = &S->units[civ][u];
    int k;

    if (map_terrain(p->x, p->y) == TERRAIN_OCEAN && p->next != NONE8 &&
        S->unit_types[p->type].domain != DOMAIN_SEA) {
        for (k = (int8_t)p->next; k != -1 && k != u; k = (int8_t)S->units[civ][k].next)
            if (S->unit_types[S->units[civ][k].type].domain == DOMAIN_SEA) {
                draw_unit(civ, k);
                return;
            }
        draw_unit(civ, u);
        return;
    }
    draw_unit(civ, stack_best_defender(civ, u));
}

/* f_1b05_11c6: a square with the units on it. */
void draw_square_units(int x, int y)
{
    int u;

    draw_square(x, y);
    if (no_units_drawn)
        return;
    u = unit_at(x, y);
    if (u == -1)
        return;
    if (!show_all && stack_owner != S->human_civ &&
        !((S->units[stack_owner][u].visible >> S->human_civ) & 1))
        return;
    if (map_improvements(x, y) & IMP_CITY)
        return;
    draw_stack(stack_owner, u);
}

void draw_city_labels(void)
{
    int i, x;
    char name[32];

    for (i = 0; i < label_n; i++) {
        if (label_y[i] >= 184)
            continue;
        snprintf(name, sizeof name, "%s", S->city_names[S->cities[label_city[i]].name]);
        while (name[0] && text_width(map_font, name) > 327 - label_x[i])
            name[strlen(name) - 1] = 0;
        x = clamp(label_x[i] - 8, 80, 999);
        shadow_text(name, x, label_y[i] + 16, 11);
    }
}

/* The minimap of f_1b05_0004: page 2's human map (x + 240), cities, the view's box. */
void draw_minimap(int civ, int x, int y)
{
    int left = x - 32, top = y - 19, sx, sy, srcx, srcy, c, cx, cy;
    int off_y, from_y;

    if (civ_has_advance(1 << civ, 8))       /* (the civ as a mask, sic) */
        top = 0;
    from_y = top < 0 ? 0 : top;
    off_y = from_y - top;
    gfx_fill(0, 8, 80, 50, 0);
    if (show_all) {
        left = clamp(left, 0, 16);
        top = clamp(top, 0, -6);
    }
    for (sy = 0; sy < 50; sy++)
        for (sx = 0; sx < 80; sx++) {
            srcx = wrap_x(left + sx);
            srcy = sy - off_y + from_y;
            if (srcy < 0 || srcy >= 50 || sy < off_y)
                continue;
            if (show_all)
                gfx_pixel(sx, sy + 8, map_pixel(srcx, srcy) == 1 ? 1 : 2);
            else
                gfx_pixel(sx, sy + 8, map_pixel(srcx + 240, srcy));
        }
    for (c = 0; c < MAX_CITIES; c++) {
        City *ct = &S->cities[c];
        if (ct->status == NONE8)
            continue;
        if (ct->visible_size == 0 && (int8_t)ct->owner != S->human_civ && !show_all)
            continue;
        cx = wrap_x(ct->x - left);
        cy = ct->y - from_y + off_y;
        if (cy < 0 || cy >= 50)
            continue;
        gfx_pixel(cx, cy + 8, civ_colour[(int8_t)ct->owner & 7]);
    }
    frame3d(0, 8, 79, 49, 15, 8);
    gfx_rect(wrap_x(x - left) - 1, y - top + 7, 17, 10, 15);
    minimap_left = left;
    minimap_top = top;
}

int minimap_left, minimap_top;

/* f_1b05_0004's drawing: the 15 x 12 squares in the original's dissolve order. */
void draw_view(int civ, int x, int y, int start)
{
    int k, sx, sy, n = start & 255, mask = 1 << civ;

    label_n = 0;
    for (k = 0; k < 256; k++, n = (n * 5 + 1) & 255) {
        sx = n & 15;
        sy = n >> 4;
        if (sy >= 12 || sx >= 15)
            continue;
        if (!show_all && !(S->visibility[wrap_x(sx + x)][sy + y] & mask)) {
            gfx_fill(sx * 16 + 80, sy * 16 + 8, 16, 16, 0);
            continue;
        }
        draw_square_units(wrap_x(sx + x), sy + y);
    }
    draw_city_labels();
    draw_minimap(civ, x, y);
}

/* f_0237_0fe6 */
void draw_menu_bar(void)
{
    static const char *const title[5] = { "GAME", "ORDERS", "ADVISORS", "WORLD", "CIVILOPEDIA" };
    static const int at[5] = { 8, 64, 128, 192, 240 };
    char first[2] = { 0, 0 };
    int i, x;

    gfx_fill(0, 0, 320, 8, 0);
    for (i = 0; i < 5; i++) {               /* (f_0181_005e: the key letter white, the rest grey) */
        first[0] = title[i][0];
        x = draw_text(map_font, at[i], 1, 15, first);
        draw_text(map_font, x, 1, 7, title[i] + 1);
    }
}

/* f_0237_149f: the little palace in the status panel. */
static void draw_mini_palace(int x, int y)
{
    int lv[9], k, kind;
    Sprite s;

    gfx_hline(x - 5, x + 68, y + 13, 2);
    if (S->city_count[S->human_civ] == 0)
        return;
    for (k = 0; k < 9; k++)
        lv[k] = S->unk_e83e[k];
    for (k = 0; k < 9; k++) {
        if (lv[k] <= 0)
            continue;
        kind = 2;
        if (k < 4) {
            kind = 1;
            if (k == 1 || (k > 0 && lv[k - 1] == -1))
                kind = 0;
        }
        if (k > 4) {
            kind = 3;
            if (k == 7 || (k < 8 && lv[k + 1] == -1))
                kind = 4;
        }
        s = spr(&sp257, 160 + 7 * (clamp(lv[k] - 1, 0, 3) * 5 + kind), 16 * (S->unk_c142[k] % 3), 7, 15);
        sprite_draw(s, k * 7 + x, y - 1);
    }
}

static void commas(char *out, size_t n, long v)
{
    char tmp[32];
    int len, i, o = 0;

    snprintf(tmp, sizeof tmp, "%ld", v);
    len = (int)strlen(tmp);
    for (i = 0; i < len && o + 2 < (int)n; i++) {
        out[o++] = tmp[i];
        if ((len - i - 1) % 3 == 0 && i != len - 1)
            out[o++] = ',';
    }
    out[o] = 0;
}

/* f_0237_107a's drawing */
void draw_status_panel(void)
{
    int h = S->human_civ, pop, k, cx;
    char buf[64], num[32];

    panel(0, 58, 80, 39);
    gfx_fill(3, 60, 74, 11, 11);
    if (status_peace > 1)                      /* (the years of peace) */
        gfx_fill(3, 60, clamp(status_peace * 2, 0, 60), 2, 15);
    draw_mini_palace(8, 58);
    pop = civ_population(h);
    if (pop) {
        commas(num, sizeof num, (long)pop * 10000);
        snprintf(buf, sizeof buf, "%s#", num);
        draw_text(map_font, 2, 73, 0, buf);
    }
    snprintf(buf, sizeof buf, "%d %s", S->year < 0 ? -S->year : S->year, S->year < 0 ? "BC" : "AD");
    draw_text(map_font, 2, 81, 0, buf);
    cx = S->year >= 0 ? 2 : 1;
    k = (S->research_progress[h] * 4) /
        (((S->difficulty * 2 + research_bonus + 6) * S->advances_known[h]) * cx + 1);
    sprite_copy(page2(160 + clamp(k, 0, 3) * 8, 120, 8, 8), 48, 80);
    if (S->unk_c10a)
        sprite_copy(page2(160 + clamp(S->unk_c10a / 4, 0, 3) * 8, 128, 8, 8), 56, 80);
    snprintf(buf, sizeof buf, "%d$ %d.%d.%d", S->treasury[h], 10 - S->tax_rate[h] - S->science_rate[h],
             S->tax_rate[h], S->science_rate[h]);
    draw_text(map_font, 2, 89, 0, buf);
}

/* f_0402_4010: the active unit (u = 128: "End of Turn"). */
void draw_unit_panel(int civ, int u)
{
    int y = 99, mv, frac, fuel, imp, k, px, py;
    char buf[64];
    Unit *p;

    panel(0, 97, 80, 103);
    if (u >= MAX_UNITS) {
        draw_text(map_font, 4, 124, 0, "End of Turn");
        draw_text(map_font, 4, 136, 0, "Press Enter");
        draw_text(map_font, 4, 144, 0, "to continue");
        return;
    }
    p = &S->units[civ][u];
    draw_text(map_font, 4, y, 0, S->civ_adjectives[civ]);
    y += 8;
    draw_text(map_font, 4, y, 0, S->unit_type_names[p->type]);
    y += 8;
    if (p->status & UNIT_VETERAN) {
        draw_text(map_font, 8, y, 0, "Veteran");
        y += 8;
    }
    mv = clamp((int8_t)p->moves / 3, 0, 99);
    frac = (int8_t)p->moves % 3;
    snprintf(buf, sizeof buf, "Moves: %d", mv);
    if (frac)
        snprintf(buf + strlen(buf), sizeof buf - strlen(buf), ".%d", frac);
    if (S->unit_types[p->type].fuel) {
        fuel = mv + S->unit_types[p->type].moves * (int8_t)p->special;
        snprintf(buf + strlen(buf), sizeof buf - strlen(buf), "(%d)", fuel);
    }
    draw_text(map_font, 4, y, 0, buf);
    y += 8;
    draw_text(map_font, 4, y, 0, p->home_city == NONE8 ? "NONE" : S->city_names[S->cities[p->home_city].name]);
    y += 8;
    snprintf(buf, sizeof buf, "(%s)", terrain_types[map_terrain(p->x, p->y)].name);
    draw_text(map_font, 4, y, 0, buf);
    y += 8;
    imp = map_improvements(p->x, p->y);
    if (imp & (IMP_RAILROAD | IMP_ROAD)) {
        draw_text(map_font, 4, y, 0, (imp & IMP_RAILROAD) ? "(RailRoad)" : "(Road)");
        y += 8;
    }
    if (imp & (IMP_IRRIGATION | IMP_MINE)) {
        draw_text(map_font, 4, y, 0, (imp & IMP_IRRIGATION) ? "(Irrigation)" : "(Mining)");
        y += 8;
    }
    if (imp & IMP_POLLUTION) {
        draw_text(map_font, 4, y, 0, "(Pollution)");
        y += 8;
    }
    y += 4;
    px = 8;
    py = y;
    if (imp & IMP_CITY) {
        City *ct = &S->cities[city_at(p->x, p->y)];
        int i;
        for (i = 0; i < 2; i++) {
            Unit fake;
            if (ct->fortified[i] == NONE8)
                continue;
            memset(&fake, 0, sizeof fake);
            fake.type = ct->fortified[i] & 63;
            fake.status = UNIT_FORTIFIED;
            fake.goto_x = NONE8;
            draw_unit_at(civ, 127, px, py, &fake);
            px += 16;
        }
    }
    k = (int8_t)p->next;
    while (k != -1 && k != u && py < 184) {
        draw_unit_at(civ, k, px, py, &S->units[civ][k]);
        px += 16;
        if (px > 64) {
            px = 8;
            py += 16;
        }
        k = (int8_t)S->units[civ][k].next;
    }
    if (k != -1 && k != u)
        draw_text(map_font, 4, py, 0, "+");
}
