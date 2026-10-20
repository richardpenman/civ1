/* The city screen.  The original draws it inside city_process() (f_0d07_0043) in mode 1 as
 * the numbers are worked out, and reads the player's input at its end (L4863); here the
 * engine calls the hooks below at the same points (city.c), and ui_city_screen_again() is
 * the input part.  Coordinates, colours and the order of drawing follow the original. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "gfx.h"
#include "font.h"
#include "assets.h"
#include "mapview.h"
#include "dialog.h"
#include "../game.h"
#include "../map.h"
#include "../unit.h"
#include "../util.h"
#include "../city.h"
#include "../civ.h"
#include "../view.h"
#include "../move.h"
#include "../ui.h"

#define S (&game.s)
#define CFONT 1                  /* the original's font 2: small capitals */

static int tab;                  /* g_2496: 0 INFO, 1 HAPPY, 2 MAP (3 VIEW opens the city view) */
static int info_units[18], info_n;    /* l76: the units in the city, on the INFO page */
static int sup_x, sup_y;         /* l242, l248: the next unit in the "supported" box */
static int happy_y, happy_x;     /* l64, l4: the HAPPY page's next row, its icons */
static int imp_first;            /* l216: the first improvement listed */
static int imp_flags;            /* l200: 2 = MORE shown, 1 = showing the second page */
static int imp_next;             /* l250: where the next page starts */
static int n_wonders, n_buildings;    /* l62, l178 */
static int citizen_step;         /* l240 */
static int map_cursor;           /* l182: the keyboard picks squares ('p') */

/* ---- the original's drawing helpers -------------------------------------------------- */

static void icon(int n, int x, int y)                /* f_1dde_0098 */
{
    if (n >= 64)
        sprite_draw(unit_sprite((n - 64) / 32, (n - 64) % 32), x, y);
    else
        sprite_draw(icon_sprite(n), x, y);
}

static void texture(int x, int y, int w, int h)      /* f_0d07_6f89: SP299 (208, 100) */
{
    int i, j, tw, th;

    for (j = 0; j < h; j += 16)
        for (i = 0; i < w; i += 16) {
            tw = w - i < 16 ? w - i : 16;
            th = h - j < 16 ? h - j : 16;
            gfx_blit(&sp299, 208, 100, tw, th, x + i, y + j, -1);
        }
}

static void cs_panel(int x1, int y1, int x2, int y2)    /* f_0d07_6f47 */
{
    texture(x1, y1, x2 - x1, y2 - y1);
    gfx_rect(x1, y1, x2 - x1, y2 - y1, 1);
}

static void centred(const char *s, int x, int y, int colour)    /* f_0181_00b5 */
{
    draw_text(CFONT, x - text_width(CFONT, s) / 2, y, colour, s);
}

void button(int x1, int y1, int x2, int y2, const char *text, int c)           /* f_0d07_703b */
{
    gfx_fill(x1, y1, x2 - x1, y2 - y1, c);
    gfx_hline(x1, x2, y1, c < 8 ? c + 8 : 7);
    gfx_vline(x1, y1, y2, c < 8 ? c + 8 : 7);
    gfx_hline(x1 + 1, x2, y2, c < 8 ? 8 : c - 8);
    gfx_vline(x2, y1, y2, c < 8 ? 8 : c - 8);
    centred(text, (x1 + x2) / 2 + 1, (y1 + y2) / 2 - 2, c ^ 8);
}

void building_icon(int b, int x, int y)              /* f_0d07_6ec1: SP299 18 x 10 pictures */
{
    int n = b;

    if (n >= 22 && n <= 24)
        return;                      /* (the spaceship parts have none) */
    n--;
    if (n >= 24)
        n -= 3;
    if (n < 40)
        gfx_blit(&sp299, (n / 5) * 19 + 161, (n % 5) * 10 + 50, 18, 10, x, y, -1);
    else
        gfx_blit(&sp299, (n & 1) * 19 + 161, 100, 18, 10, x, y, -1);
}

/* f_0d07_72d1: in the current font */
void shorten_in(int font, char *s, int maxw)
{
    size_t n;

    while (text_width(font, s) > maxw && (n = strlen(s)) >= 3) {
        if (s[n - 3] != ' ')
            s[n - 2] = '.';
        s[n - 1] = 0;
    }
}

static void shorten(char *s, int maxw)
{
    shorten_in(CFONT, s, maxw);
}

static void upper(char *s)
{
    for (; *s; s++)
        *s = (char)toupper((unsigned char)*s);
}

static const char *home_name(int city)               /* f_1439_08be */
{
    return city < 0 || city == NONE8 ? "NONE" : S->city_names[S->cities[city].name];
}

/* f_0d07_6d7a: the citizens from (x, y), at most maxw wide; returns the step. */
int citizens(int city, int x, int y, int specialists, int maxw)
{
    int size = (int8_t)S->cities[city].size, step, i, n;

    step = size * 7 > maxw ? maxw / size : 7;
    for (i = 0; i < calc.happy; i++, x += step)
        sprite_draw(spr(&sp257, (i & 1) * 8, 128, 8, 16), x, y);
    x += 2;                          /* (the original tests an uninitialised local here) */
    n = size - specialists - calc.happy - calc.unhappy;
    for (i = 0; i < n; i++, x += step)
        sprite_draw(spr(&sp257, (2 + (i & 1)) * 8, 128, 8, 16), x, y);
    if (i)
        x += 2;
    for (i = 0; i < calc.unhappy; i++, x += step)
        sprite_draw(spr(&sp257, (4 + (i & 1)) * 8, 128, 8, 16), x, y);
    x += 4;
    for (i = 0; i < specialists; i++, x += step)
        sprite_draw(spr(&sp257, (5 + city_specialist(i)) * 8, 128, 8, 16), x, y);
    return step;
}

static void minimap_dot(int x, int y, int colour)    /* f_0d07_726d */
{
    if (tab != 2)
        return;
    x = wrap_x(x - S->capital_x[S->human_civ] + 40);
    gfx_fill(x * 3 / 2 + 101, y * 3 / 2 + 118, 2, 2, colour);
}

static void minimap(void)                            /* f_0d07_7133 */
{
    static const signed char ox[4] = { 0, 2, 0, 1 }, oy[4] = { -1, 0, 1, 2 };
    int row, col, x, k;

    gfx_fill(101, 117, 120, 75, 0);
    for (row = 0; row < 50; row++)
        for (col = 0; col < 80; col++) {
            x = wrap_x(S->capital_x[S->human_civ] + col - 40);
            if (!(S->visibility[x][row] & (1 << S->human_civ)) && !show_all)
                continue;
            k = (row & 1) * 2 + (col & 1);
            gfx_fill((col / 2) * 3 + ox[k] + 101, (row / 2) * 3 + oy[k] + 118, 2, 2,
                     map_terrain(x, row) == TERRAIN_OCEAN ? 1 : 2);
        }
    gfx_rect(100, 117, 121, 75, 9);
}

/* ---- the hooks, in the order city_process() calls them ------------------------------ */

void ui_city_screen(int civ, int city)
{
    (void)civ;
    (void)city;
    tab = 0;
    imp_first = imp_flags = 0;
    map_cursor = 0;
}

/* L0495: the frame, the name, the city's squares. */
void ui_city_screen_begin(int city)
{
    const City *c = &S->cities[city];
    int civ = (int8_t)c->owner, pop = 0, i, sq, x, y, o;
    char buf[64];

    gfx_fill(0, 0, 320, 200, 0);
    cs_panel(2, 1, 208, 21);
    for (i = 1; i <= (int8_t)c->size; i++)
        pop += i;
    snprintf(buf, sizeof buf, "%s", S->city_names[c->name]);
    upper(buf);
    strcat(buf, " (Pop:");
    if (pop >= 100)
        snprintf(buf + strlen(buf), sizeof buf - strlen(buf), "%d,%s", pop / 100, pop % 100 < 10 ? "0" : "");
    snprintf(buf + strlen(buf), sizeof buf - strlen(buf), "%d0,000)", pop % 100);
    centred(buf, 104, 2, 15);
    cs_panel(127, 23, 208, 104);
    view_x = c->x - 5;
    view_y = c->y - 3;
    draw_square(c->x, c->y);
    for (sq = 0; sq < 21; sq++) {
        x = c->x + city_square_dx[sq];
        y = c->y + city_square_dy[sq];
        if (civ == S->human_civ && !(S->visibility[wrap_x(x)][y] & (1 << civ)) && !show_all)
            continue;
        o = map_unit_owner(x, y);
        if (o != -1 && o != civ) {
            draw_square_units(wrap_x(x), y);
            continue;
        }
        draw_square(wrap_x(x), y);
        if (city_taken[city_square_dx[sq] + 2][city_square_dy[sq] + 2])
            gfx_rect((x - view_x) * 16 + 80, (y - view_y) * 16 + 8, 15, 15, 12);
    }
    info_n = 0;
    sup_x = 8;
    sup_y = 69;
    happy_y = 116;
    happy_x = 208;
}

/* f_0d07_6761 in mode 1: a worked square and what it yields. */
void ui_city_work_square(int city, int sq)
{
    const City *c = &S->cities[city];
    int x = c->x + city_square_dx[sq], y = c->y + city_square_dy[sq], total = 0, step, k, n, ix = 0, iy = 0;

    draw_square(wrap_x(x), y);
    for (k = 0; k < 3; k++)
        total += tile_yield(city, x, y, k);
    step = total <= 4 ? 8 : total <= 6 ? 5 : 3;
    for (k = 0; k < 3; k++)
        for (n = tile_yield(city, x, y, k); n > 0; n--) {
            icon(k + 8, city_square_dx[sq] * 16 + ix + 161, city_square_dy[sq] * 16 + iy + 57);
            if (ix < 8)
                ix += step;
            else {
                ix = 0;
                iy += 8;
            }
        }
    if (total == 0)
        icon(13, city_square_dx[sq] * 16 + 165, city_square_dy[sq] * 16 + 61);
}

/* L2659: the two lower panels and the page buttons. */
void ui_city_screen_resources(int city)
{
    (void)city;
    cs_panel(2, 67, 124, 104);
    cs_panel(95, 106, 227, 197);
    button(95, 106, 128, 114, "INFO", 9);
    button(129, 106, 160, 114, "HAPPY", 9);
    button(161, 106, 193, 114, "MAP", 9);
    button(194, 106, 226, 114, "VIEW", 9);
    gfx_recolour(tab * 33 + 96, 107, 32, 7, 9, 15);
    if (tab == 2)
        minimap();
}

static void happy_row(int city)
{
    citizens(city, 100, happy_y, calc.free, 92);
    happy_y += 16;
    gfx_hline(100, 222, happy_y - 2, 1);
}

void ui_city_happy(int city, int step, int arg)
{
    if (tab != 1)
        return;
    switch (step) {
    case HAPPY_ROW:
        happy_row(city);
        happy_x = 208;
        break;
    case HAPPY_LUX_ROW:
        citizens(city, 100, happy_y, calc.free, 92);
        icon(14, 208, happy_y + 4);
        happy_y += 16;
        gfx_hline(100, 222, happy_y - 2, 1);
        happy_x = 208;
        break;
    case HAPPY_BUILDING:
        building_icon(arg, happy_x, happy_y);
        happy_x -= 16;
        break;
    case HAPPY_NEXT:
        happy_x = 208;
        break;
    case HAPPY_UNIT:
        icon(arg, happy_x, happy_y - 1);
        happy_x -= 2;
        break;
    case HAPPY_AWAY:
        for (; arg > 0; arg--) {
            icon(13, happy_x, happy_y + 4);
            happy_x -= 2;
        }
        break;
    case HAPPY_WONDERS_ROW:
        citizens(city, 100, happy_y, calc.free, 92);
        draw_text(CFONT, 190, happy_y + 5, 15, "WONDERS");
        happy_y += 16;
        gfx_hline(100, 222, happy_y - 2, 1);
        break;
    }
}

/* city_support() meets each unit (L3580, L37aa): the units the city supports go in the box
 * on the left with what they cost; the units in the city are listed on the INFO page. */
void ui_city_unit(int city, int civ, int u, int home)
{
    const City *c = &S->cities[city];
    const Unit *p = &S->units[civ][u];
    int n;
    char buf[16];

    if (home) {
        if (p->type < UNIT_DIPLOMAT) {
            if (calc.shield_upkeep != 0)
                icon(9, sup_x + 8, sup_y + 12);
            if (p->type == UNIT_SETTLERS) {
                icon(8, sup_x, sup_y + 12);
                if (S->government[civ] >= 2)
                    icon(8, sup_x + 2, sup_y + 12);
            } else {
                n = !civ_has_wonder(civ, WONDER_WOMENS_SUFFRAGE);
                if (S->government[civ] == GOV_DEMOCRACY)
                    n++;
                if (S->government[civ] >= GOV_REPUBLIC && n &&
                    (S->unit_types[p->type].domain == DOMAIN_AIR || p->x != c->x || p->y != c->y)) {
                    icon(13, sup_x, sup_y + 12);
                    if (n > 1)
                        icon(13, sup_x + 2, sup_y + 12);
                }
            }
        }
        icon(p->type + civ * 32 + 64, sup_x, sup_y);
        minimap_dot(p->x, p->y, 7);
        sup_x += 16;
        if (sup_x >= 112) {
            sup_x = 8;
            sup_y += 16;
            if (sup_y > 85)
                sup_y -= 24;
        }
    }
    if (info_n >= 18 || tab != 0 || p->x != c->x || p->y != c->y)
        return;
    draw_unit_at(civ, u, (info_n % 6) * 18 + 100, (info_n / 6) * 16 + 116, p);
    snprintf(buf, sizeof buf, "%.3s.", home_name((int8_t)p->home_city));
    draw_text(CFONT, (info_n % 6) * 18 + 100, (info_n / 6) * 16 + 131, 0, buf);
    info_units[info_n++] = u;
}

/* L395c: improvements, production, food, resources, citizens, trade, pollution. */
void ui_city_screen_end(int city)
{
    City *c = &S->cities[city];
    int civ = (int8_t)c->owner, box = calc.box, y = 2, row = 2, i, w, b, cost, rows, cw, pw, ph, step, x, need,
        lh;
    char buf[64];

    cs_panel(211, 1, 317, 97);
    n_wonders = n_buildings = 0;
    if (imp_first == 0)
        for (w = 1; w <= NUM_WONDERS; w++) {
            if (S->wonder_city[w] != city)
                continue;
            b = FIRST_WONDER + w - 1;
            snprintf(buf, sizeof buf, "%s", building_types[b].name);
            upper(buf);
            shorten(buf, 63);
            draw_text(CFONT, 253, y + 2, 15, buf);
            building_icon(b, (row & 1) ? 213 : 233, y - 2);
            y += 6;
            row++;
            n_wonders++;
        }
    imp_next = 0;
    for (b = imp_first; b < 24; b++) {
        if (!(c->buildings & (1UL << b)))
            continue;
        if (row >= 16) {
            imp_flags = (imp_flags | 2) & ~1;
            imp_next = b;
            break;
        }
        n_buildings++;
        icon(11, 309, y + 1);
        snprintf(buf, sizeof buf, "%s", building_types[b + 1].name);
        shorten(buf, 56);
        upper(buf);
        draw_text(CFONT, 253, y + 2, 15, buf);
        building_icon(b + 1, (row & 1) ? 213 : 233, y - 2);
        y += 6;
        row++;
    }
    gfx_hline(231, 250, 0, 0);
    gfx_hline(231, 250, 1, 1);
    if (imp_flags & 2)
        button(287, 88, 315, 96, "MORE", 9);
    gfx_recolour(309, 2, 8, 96, 14, 12);

    /* production */
    cost = c->producing >= 0 ? S->unit_types[c->producing].cost : building_types[-c->producing].cost;
    rows = clamp((cost - 1) / 10 + 1, (c->shields - 1) / 100 + 1, 99);
    cw = 80 / box;
    pw = ((rows * 8 - 8) / rows) + box * cw;
    ph = ((cost - 1) / rows) * 8 + 8;
    gfx_fill(230, 99, pw + 3, ph + 19, 1);
    if (civ == S->human_civ) {
        button(231, 106, 263, 114, (c->status & CITY_AUTOBUILD) ? "AUTO." : "CHANGE", 9);
        button(294, 106, 311, 114, "BUY", 9);
    }
    if (c->producing >= 0)
        icon(c->producing + civ * 32 + 64, 264, 100);
    else {
        snprintf(buf, sizeof buf, "%s", building_types[-c->producing].name);
        shorten(buf, 86);
        centred(buf, 274, 100, 15);
    }
    texture(231, 116, pw + 1, ph + 1);
    for (i = 0; i < c->shields; i++)
        icon(9, ((i % (box * rows)) * cw) / rows + 232, (i / (box * rows)) * 8 + 117);

    /* food storage */
    step = clamp(80 / (int8_t)c->size, 1, 8);
    lh = 80 / box;
    gfx_fill(2, 106, 91, box * lh + 12, 1);
    draw_text(CFONT, 8, 108, 15, "Food Storage");
    texture(3, 115, (int8_t)c->size * step + 9, box * lh + 2);
    if (c->buildings & BIT(B_GRANARY))
        gfx_hline(5, (int8_t)c->size * step + 9, 155, 1);
    for (i = 0; i < clamp(c->food, 0, ((int8_t)c->size + 1) * box); i++)
        icon(8, (i % ((int8_t)c->size + 1)) * step + 4, (i / ((int8_t)c->size + 1)) * lh + 116);

    /* city resources */
    cs_panel(2, 23, 124, 65);
    gfx_fill(2, 23, 122, 9, 1);
    draw_text(CFONT, 8, 25, 15, "City Resources");
    need = (int8_t)c->size * 2 + calc.settler_food * calc.settlers;
    for (b = 0; b < 3; b++) {
        x = 4;
        step = clamp(116 / ((b == 0 ? clamp(calc.yield[0], need, 999) : calc.yield[b]) + 1), 1, 8);
        for (i = 0; i < calc.yield[b]; i++) {
            if (b == 0 && i == need)
                x += 4;
            if (b == 1 && calc.shield_upkeep != 0 && i == calc.shield_upkeep)
                x += 4;
            if (b == 2 && i == calc.yield[2] - calc.corruption)
                x += 2;
            icon(b + 8, x, b * 8 + 33);
            x += step;
        }
    }
    x = 8;
    step = clamp(224 / (calc.lux + calc.science + calc.tax + calc.corruption + 2), 1, 16);
    for (i = 0; i < calc.lux; i++, x += step)
        icon(14, x / 2, 57);
    if (calc.lux)
        x += 8;
    for (i = 0; i < calc.tax; i++, x += step)
        icon(11, x / 2, 57);
    if (calc.tax)
        x += 8;
    for (i = 0; i < calc.science; i++, x += step)
        icon(12, x / 2, 57);
    if (need > calc.yield[0]) {
        step = clamp(116 / (need + 1), 1, 8);
        for (i = calc.yield[0]; i < need; i++)
            icon(8, i * step + 8, 33);
        gfx_recolour(step * calc.yield[0] + 8, 33, (need - calc.yield[0]) * step + 4, 8, 15, 0);
    }
    if (calc.yield[1] < calc.shield_upkeep) {
        step = clamp(116 / (calc.yield[1] + 1), 1, 8);
        for (i = calc.yield[1]; i < calc.shield_upkeep; i++)
            icon(9, i * step + 8, 41);
        gfx_recolour(step * calc.yield[1] + 8, 41, (calc.shield_upkeep - calc.yield[1]) * step, 8, 15, 0);
    }
    if (calc.corruption) {
        step = clamp(116 / (calc.yield[2] + 1), 1, 8);
        gfx_recolour((calc.yield[2] - calc.corruption) * step + 6, 49, step * calc.corruption + 2, 8, 15, 0);
    }

    /* the citizens */
    texture(8, 8, 200, 13);
    citizen_step = citizens(city, 8, 8, calc.free, 192);

    /* trade routes */
    for (i = 0; i < 3; i++) {
        int o = c->trade_routes[i], v;
        if (o == NONE8)
            continue;
        v = (int8_t)S->cities[o].owner != civ ? ((int8_t)c->trade + (int8_t)S->cities[o].trade + 4) / 8
                                              : ((int8_t)c->trade + (int8_t)S->cities[o].trade + 4) / 16;
        snprintf(buf, sizeof buf, "%s:+%d} ", home_name(o), v);
        minimap_dot(S->cities[o].x, S->cities[o].y, 10);
        if (tab == 0)
            draw_text(CFONT, 98, i * 6 + 179, 10, buf);
    }
    minimap_dot(c->x, c->y, 15);

    /* pollution */
    if (tab == 0) {
        int p = calc.yield[1] / calc.pollution_divisor - 20 + (int8_t)c->size * calc.pollution_factor / 4;
        step = clamp(100 / clamp(p, 1, 99), 1, 8);
        for (i = 0; i < p; i++)
            sprite_draw(spr(&sp257, 48, 32, 16, 16), i * step + 98, (i & 1) + 161);
    }
    if (civ == S->human_civ || show_all)
        button(284, 190, 316, 198, "EXIT", 12);
    gfx_present();
}

static void change_production(City *c, int city, int civ)
{
    int p;

    if (c->producing >= 0)
        S->units_in_production[civ][c->producing]--;
    p = choose_production(civ, city);
    if (p != 99)
        c->producing = (int8_t)p;
    if (c->producing >= 0)
        S->units_in_production[civ][c->producing]++;
}

static int buy(City *c, int civ)
{
    int cost, price;
    char msg[256];

    if (c->producing >= 0) {
        cost = S->unit_types[c->producing].cost;
        price = clamp(cost * 10 - c->shields, 0, 999);
        price = price * price / 20 + price * 2;
    } else {
        cost = building_types[-c->producing].cost;
        price = clamp(cost * 10 - c->shields, 0, 999) * 2;
        if (-c->producing > 21)
            price <<= 1;
    }
    if (c->shields == 0)
        price <<= 1;
    snprintf(msg, sizeof msg, "Cost to complete\n%s: $%d\nTreasury: $%d\n",
             c->producing >= 0 ? S->unit_type_names[c->producing] : building_types[-c->producing].name, price,
             S->treasury[civ]);
    if (c->producing >= 0 && (c->status & CITY_DISORDER))
        strcat(msg, "CIVIL DISORDER\n");
    else if (S->treasury[civ] >= price)
        strcat(msg, " Yes\n No\n");
    dialog_font = 0;
    if (dialog(msg, 100, 80, 0, 0) == 0 && S->treasury[civ] >= price) {
        c->shields = (int16_t)(cost * 10);
        S->treasury[civ] = (int16_t)(S->treasury[civ] - price);
    }
    return 1;
}

static int sell(City *c, int city, int n)
{
    int b;
    char msg[256];

    (void)city;
    for (b = imp_first; b < 24; b++) {
        if (!(c->buildings & (1UL << b)) || n-- != 0)
            continue;
        if (c->status & 0x80)
            return 1;
        snprintf(msg, sizeof msg, "Do you want to sell\nyour %s for %d$?\n No.\n Yes.\n", building_types[b + 1].name,
                 building_types[b + 1].cost * 10);
        dialog_font = 0;
        if (dialog(msg, 128, 80, 0, 0) == 1) {
            S->treasury[c->owner] = (int16_t)(S->treasury[(int8_t)c->owner] + building_types[b + 1].cost * 10);
            c->buildings &= ~(1UL << b);
            c->status |= 0x80;
            if (b + 1 == B_CITY_WALLS)
                city_screen_dirty = 1;
        }
        return 1;
    }
    return 1;
}

static void wake(int civ, int u)
{
    Unit *p = &S->units[civ][u];

    if (p->status & 0x09)
        p->moves = (uint8_t)(S->unit_types[p->type].moves * 3);
    p->status &= 0x30;
    p->goto_x = NONE8;
}

/* A worked square becomes free (an entertainer), a free one is worked (L58df). */
static int toggle_square(City *c, int city, int dx, int dy)
{
    int sq, specialists = (int)(c->worked >> 26), x, y;

    for (sq = 0; sq < 20; sq++)
        if (city_square_dx[sq] == dx && city_square_dy[sq] == dy)
            break;
    if (sq >= 20)
        return 1;
    x = wrap_x(c->x + dx);
    y = c->y + dy;
    if (!(S->visibility[x][y] & (1 << (int8_t)c->owner)))
        return 1;
    if (!(c->worked & (1UL << sq)) && city_taken[dx + 2][dy + 2])
        return 1;
    (void)city;
    c->worked ^= 1UL << sq;
    if (c->worked & (1UL << sq)) {
        if (specialists)
            specialists--;
    } else
        specialists++;
    c->worked = (c->worked & ((1UL << 26) - 1)) | ((uint32_t)specialists << 26);
    return 1;
}

/* L4863: the player's input; 1 to work the city out and draw it again, 0 to leave. */
int ui_city_screen_again(int city)
{
    City *c = &S->cities[city];
    int civ = (int8_t)c->owner, k, mx, my, i, sq, n, t;

    if (civ != S->human_civ && !show_all)
        return 0;
    calc_city = city;
    k = gfx_wait_key();
    if (k == KEY_QUIT)
        return 0;
    mx = my = 0;
    if (k == KEY_MOUSE) {
        mx = mouse_x;
        my = mouse_y;
    }
    /* the CHANGE button with the right button, or 'A': automatic building on and off */
    if ((k == KEY_MOUSE && mouse_button == 2 && mx >= 230 && mx < 270 && my >= 106 && my <= 116) || k == 'A') {
        c->status ^= CITY_AUTOBUILD;
        if (c->status & CITY_AUTOBUILD)
            change_production(c, city, civ);
        return 1;
    }
    if (k != KEY_MOUSE || (mouse_button == 1 && my >= 106 && my <= 116)) {
        if ((k == KEY_MOUSE && mx >= 296) || k == 'b')
            return buy(c, civ);
        if ((k == KEY_MOUSE && mx >= 230 && mx < 270) || k == 'c') {
            c->status &= ~CITY_AUTOBUILD;
            change_production(c, city, civ);
            return 1;
        }
        t = -1;
        if (k == 'i')
            t = 0;
        else if (k == 'h')
            t = 1;
        else if (k == 'm')
            t = 2;
        else if (k == 'v')
            t = 3;
        else if (k == KEY_MOUSE && mx >= 96 && mx < 224)
            t = (mx - 96) / 32;
        if (t == 3) {
            dialog_font = 0;
            city_view(city, -1);
            city_screen_dirty = 1;
            tab = 0;
            return 1;
        }
        if (t >= 0) {
            tab = t;
            return 1;
        }
    }
    if (k >= '1' && k <= '9') {
        i = k - '1';
        if (i < calc.free) {
            n = city_specialist(i);
            city_set_specialist(i, n < 3 ? n + 1 : 1);
        }
        return 1;
    }
    if (k == 'M') {
        if (imp_flags & 2) {
            imp_flags ^= 1;
            imp_first = (imp_flags & 1) ? imp_next : 0;
        }
        return 1;
    }
    if (k != KEY_MOUSE || mouse_button != 1)
        return 0;
    if (mx >= 288 && my < 96 && my > 88 && (imp_flags & 2)) {
        imp_flags ^= 1;
        imp_first = (imp_flags & 1) ? imp_next : 0;
        return 1;
    }
    if (mx >= 300 && my < 94)
        return sell(c, city, (my - 2) / 6 - n_wonders);
    if (mx < 200 && my < 20) {
        i = (mx - 16) / citizen_step - ((int8_t)c->size - calc.free);
        if (i >= 0 && i < calc.free) {
            n = city_specialist(i);
            city_set_specialist(i, n < 3 ? n + 1 : 1);
        }
        return 1;
    }
    if (mx >= 100 && my >= 116) {
        i = ((my - 116) / 16) * 6 + (mx - 100) / 16;
        if (i < info_n) {
            wake(civ, info_units[i]);
            return 1;
        }
    }
    if (my >= 24 && my < 104) {
        sq = 0;
        (void)sq;
        return toggle_square(c, city, mx / 16 - 10, (my - 24) / 16 - 2);
    }
    return 0;
}

/* o_03_09ac: the five cities with the most content people (wonders count 10 each). */
void top_five_cities(void)
{
    static uint8_t under[SCR_H][SCR_W];
    int score[5], who[5], i, j, c, v, w, x, y;
    char buf[64];

    for (i = 0; i < 5; i++)
        score[i] = who[i] = -1;
    for (c = 0; c < MAX_CITIES; c++) {
        if (S->cities[c].status == NONE8)
            continue;
        city_process(c, -1);
        v = (int8_t)S->cities[c].size + calc.happy - calc.unhappy;
        for (w = 1; w <= NUM_WONDERS; w++)
            if (S->wonder_city[w] == c)
                v += 10;
        for (i = 0; i < 5; i++)
            if (score[i] < v) {
                for (j = 3; j >= i; j--) {
                    score[j + 1] = score[j];
                    who[j + 1] = who[j];
                }
                score[i] = v;
                who[i] = c;
                break;
            }
    }
    gfx_cursor(0);
    memcpy(under, screen, sizeof under);
    gfx_fill(0, 0, 320, 200, 3);
    draw_text(0, 80, 13, 0, "The Top Five Cities in the World");
    draw_text(0, 80, 12, 15, "The Top Five Cities in the World");
    for (i = 0, y = 32; i < 5; i++) {
        if (score[i] == -1)
            continue;
        c = who[i];
        snprintf(buf, sizeof buf, "%d. %s (%s)", i + 1, S->city_names[S->cities[c].name],
                 S->civ_adjectives[(int8_t)S->cities[c].owner]);
        draw_text(0, 160 - text_width(0, buf) / 2, y + 3, civ_colour[(int8_t)S->cities[c].owner], buf);
        city_process(c, -1);
        citizens(c, 48, y + 10, calc.specialists, 160);
        x = 48 + clamp((int8_t)S->cities[c].size * 8, 0, 160) + 8;
        for (w = 1; w <= NUM_WONDERS; w++)
            if (S->wonder_city[w] == c) {
                if (x < 293)
                    building_icon(FIRST_WONDER + w - 1, x, y + 12);
                x += 19;
            }
        gfx_rect(8, y, 303, 25, civ_colour[(int8_t)S->cities[c].owner]);
        y += 32;
    }
    gfx_wait_key();
    gfx_cursor(1);
    memcpy(screen, under, sizeof under);
    gfx_present();
}
