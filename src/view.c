/* The part of the map view that game logic depends on (seg 1b05, seg 0860).  Redrawing the
 * view consumes a random number (it starts its dissolve at a random square), and the view's
 * position decides when it is redrawn, so both are part of the game state. */
#include "game.h"
#include "map.h"
#include "util.h"
#include "move.h"
#include "ui.h"
#include "turn.h"
#include "unit.h"
#include <string.h>
#include "civ.h"

int units_hidden;          /* g_ec1c: squares are drawn without units */

/* f_1b05_0004 */
void view_center(int civ, int x, int y)
{
    int start;

    int k, n, sx, sy;

    status_report();                       /* (f_0237_107a, first thing) */
    view_x = wrap_x(x);
    view_y = clamp(y, 0, 38);
    start = rnd(256);
    /* The squares are drawn in a dissolve order; each visible one looks up its units. */
    for (k = 0, n = start; k < 256; k++, n = (n * 5 + 1) & 255) {
        sx = n & 15;
        sy = n >> 4;
        if (sy >= 12 || sx >= 15)
            continue;
        if (!show_all && !(game.s.visibility[wrap_x(sx + view_x)][sy + view_y] & (1 << civ)))
            continue;
        if (!units_hidden)
            unit_at(wrap_x(sx + view_x), sy + view_y);
    }
    ui_draw_view(civ, view_x, view_y, start);
}

/* f_0860_1679: recentre if (x, y) is at or beyond the edge of the 15 x 11 view. */
void view_follow(int civ, int x, int y)
{
    int out = 0;

    if (x < 16 && view_x > 64)
        x += 80;
    if (x < view_x + 2 || x > view_x + 13)
        out = 1;
    if (y < view_y + 2 || y > view_y + 9)
        out = 1;
    if (out)
        view_center(civ, wrap_x(x - 8), y - 6);
}

/* redraw_map (f_0237_1b40): the whole screen, the view where it is (so a random number). */
void redraw_map(void)
{
    view_center(game.s.human_civ, view_x, view_y);
    ui_redraw_map();
}

/* f_0860_25e1: a border around the screen in one of two styles (so a random number). */
void frame_screen(void)
{
    ui_frame_screen(rnd(2));
}

/* o_21_0000: the newspaper.  `city` names the paper (< 0: the human's capital, -1 also any
 * of the human's cities); unless it is -2 the page is drawn with a crowd first.  The crowd,
 * the paper's name, "EXTRA!"/"FLASH" and the headline (*NEWSA..N) are random. */
void newspaper(int city, const char *text)
{
    SaveData *s = &game.s;
    News n;
    int k, crowd = city != -2;

    if (city < 0)
        for (k = 0; k < MAX_CITIES; k++) {
            City *c = &s->cities[k];
            if (c->status == NONE8 || (int8_t)c->owner != s->human_civ)
                continue;
            if ((c->buildings & BIT(B_PALACE)) || city == -1)
                city = k;
        }
    n.city = city;
    n.crowd = crowd;
    for (k = 0; k < 16; k++)
        n.crowd_y[k] = crowd ? (uint8_t)(100 - rnd(2)) : 0;
    n.title = rnd(4);
    n.extra = rnd(2) != 0;
    n.headline = rnd(14);
    ui_newspaper(&n, text);
}

/* o_19_0000's layout: the city view re-seeds the random generator with the sum of the city
 * name's characters and lays the city out with it: houses wander out from the bottom
 * middle, two roads and two rivers cross, then wonders and buildings find free places.
 * grid[x * 12 + y] (19 x 12): -1 empty, -2 road/river, 0..9 houses, 15 part of a large
 * building, 16.. buildings (bit b-16 of the city's), 65.. wonders. */
void city_view_layout(int city, int8_t *grid, int *river_col)
{
    SaveData *s = &game.s;
    City *c = &s->cities[city];
    int8_t a[19 * 12 + 16];         /* (the original also writes a little past its array) */
    const char *name = s->city_names[c->name];
    int sum = 0, i, x, y, n, tries, d, v, col, row, f, w, b;
    int half_adv = s->advances_known[(int8_t)c->owner] / 2, size = (int8_t)c->size;

    for (i = 0; name[i]; i++)
        sum += (signed char)name[i];
    game_srand((unsigned)sum & 0xFFFF);
    memset(a, -1, sizeof a);

    /* houses */
    tries = 0;
    x = 9;
    y = 11;
    n = 0;
    while (size * 8 > n) {
        if (a[x * 12 + y] != -1) {
            if (++tries < 999)
                n--;
        } else {
            d = distance(x - 9, y - 10);
            v = clamp((half_adv - d) / 3 - rnd(2), half_adv / 6, size / 4 + 6);
            a[x * 12 + y] = (int8_t)(v > 9 ? 9 : v);
            if (d * d > size * 8) {
                x = 9;
                y = 11;
            }
        }
        d = rnd(8) + 1;
        x = clamp(around_dx[d] + x, 0, 18);
        y = clamp(around_dy[d] + y, 0, 11);
        n++;
    }
    /* two roads down, two rivers across */
    col = rnd(2) + 4;
    for (i = 2; i < 17; i++)
        a[i * 12 + col] = -2;
    a[18 * 12 + col] = -1;
    a[col] = -1;
    col = rnd(2) + 8;
    for (i = 2; i < 17; i++)
        a[i * 12 + col] = -2;
    a[18 * 12 + col] = -1;
    a[col] = -1;
    if (river_col)
        *river_col = col;
    row = rnd(4) + 9;
    for (i = 1; i < 12; i++)
        a[(row - (i + 1) / 2) * 12 + i] = -2;
    row = rnd(4) + 14;
    for (i = 1; i < 12; i++)
        a[(row - (i + 1) / 2) * 12 + i] = -2;
    if (city == s->wonder_city[WONDER_GREAT_WALL]) {
        a[0] = a[12] = a[24] = a[1] = a[13] = a[2] = 15;
    }
    /* wonders (but the Pyramids, Colossus, Great Wall and Hoover Dam, drawn elsewhere) */
    for (w = 1; w <= 21; w++) {
        if (s->wonder_city[w] != city || (w < 16 && ((1 << w) & 0x808A)))
            continue;
        for (tries = 0; ; ) {
            x = rnd(15) + 1;
            y = rnd(10) + 1;
            i = x * 12 + y;
            f = 0;
            if (a[i] >= 0 || a[i + 12] >= 0 || a[i + 24] >= 0 || a[i - 11] >= 0 || a[i + 1] >= 0 || a[i + 13] >= 0)
                f |= 1;
            if (tries > 400)
                f |= 1;
            if (a[i] >= 15 || a[i + 12] >= 15 || a[i + 24] >= 15 || a[i - 11] >= 15 || a[i + 1] >= 15 ||
                a[i + 13] >= 15 || a[i + 25] >= 15)
                f |= 2;
            if (a[i] == -2 || a[i + 12] == -2 || a[i - 11] == -2 || a[i + 1] == -2 || a[i + 13] == -2)
                f |= 2;
            if (f == 1 || ++tries >= 500)
                break;
        }
        if (tries >= 500)
            continue;
        a[i] = (int8_t)(w + 64);
        a[i + 12] = a[i + 24] = a[i + 1] = a[i + 13] = a[i + 25] = 15;
        a[i - 11] = a[i + 11] = a[i + 23] = a[i + 35] = 15;
        if (y < 7 && !(a[i + 14] & 7))
            a[i + 14] = -1;
    }
    /* buildings, from the last (not walls, ...) */
    for (b = 23; b >= 1; b--) {
        if (!(c->buildings & (1UL << b)) || b == 7 || b == 8 || b == 12 || b == 18 || b == 19)
            continue;
        for (tries = 0; ; ) {
            x = rnd(16) + 1;
            y = rnd(10) + 1;
            i = x * 12 + y;
            f = 0;
            if (a[i] >= 0 || a[i + 12] >= 0 || a[i - 11] >= 0 || a[i + 1] >= 0 || a[i + 13] >= 0)
                f |= 1;
            if (tries > 350)
                f |= 1;
            if (a[i] >= 15 || a[i + 12] >= 15 || a[i - 11] >= 15 || a[i + 1] >= 15 || a[i + 13] >= 15)
                f |= 2;
            if (a[i] == -2 || a[i + 12] == -2 || a[i - 11] == -2 || a[i + 1] == -2 || a[i + 13] == -2)
                f |= 2;
            if (f == 1 || ++tries >= 500)
                break;
        }
        if (tries >= 500)
            continue;
        a[i] = (int8_t)(b + 15);
        a[i + 12] = a[i + 1] = a[i + 13] = a[i - 11] = 15;
        if (y < 7 && !(a[i + 14] & 7))
            a[i + 14] = -1;
    }
    if (grid)
        memcpy(grid, a, 19 * 12);
}

/* o_19_0000: the city view (mode: -1 looked at, -2 conquered/celebrating, -3 founded, or the
 * wonder just built). */
void city_view(int city, int mode)
{
    int8_t grid[19 * 12];
    int col;

    city_view_layout(city, grid, &col);
    ui_city_view(city, mode, grid);
}

/* Drawing a square looks up the units on it (unit_at), and that leaves stack_owner set to
 * their owner (and clears a stale unit marker from the map).  Later code reads stack_owner,
 * so the engine performs the lookups of each of the original's drawing calls. */
static int in_view(int x, int y)
{
    int px = wrap_x(x - view_x) * 16 + 80, py = (y - view_y) * 16 + 8;

    return px >= 80 && px < 320 && py >= 8 && py <= 192;
}

/* f_1b05_03b1's lookup: a visible city's square, drawn in the view. */
void city_square_lookup(int x, int y)
{
    SaveData *s = &game.s;
    int imp, c;

    if (fast_view == 1 || units_hidden || !in_view(x, y))
        return;
    imp = show_all ? map_improvements(x, y) : map_known_improvements(x, y);
    if (!(imp & IMP_CITY))
        return;
    for (c = 0; c < MAX_CITIES; c++)
        if (s->cities[c].status != NONE8 && s->cities[c].x == x && s->cities[c].y == y)
            break;
    if (c == MAX_CITIES)
        return;                           /* (the original reads city[-1]) */
    if ((int8_t)s->cities[c].owner == s->human_civ || s->cities[c].visible_size != 0 || show_all)
        unit_at(x, y);
}

/* f_1b05_03b1: a square without its units. */
void terrain_redraw(int x, int y)
{
    city_square_lookup(x, y);
    ui_redraw_terrain(x, y);
}

/* f_1b05_11c6: a square and its units. */
void square_redraw(int x, int y)
{
    if (!units_hidden)
        unit_at(x, y);
    ui_redraw_square(x, y);
}

/* f_0402_3e8c: a square, if the human can see it. */
void redraw_if_seen(int x, int y)
{
    if ((game.s.visibility[x][y] & (1 << game.s.human_civ)) || show_all)
        square_redraw(x, y);
}

/* f_0402_3ec3: a unit's square, if it is the human's or the human can see it. */
void redraw_unit_square(int civ, int u)
{
    const Unit *p = &game.s.units[civ][u];

    if (civ == game.s.human_civ || (p->visible & (1 << game.s.human_civ)))
        square_redraw(p->x, p->y);
}

/* The end of unit_reveal (f_0860_01dc) for the human: the square and the two rings around it
 * that the civilization can see. */
void reveal_redraw(int x, int y, int civ)
{
    int k, sx, sy;

    square_redraw(x, y);
    for (k = 9; k < 49; k++) {
        sx = wrap_x(x + around_dx[k]);
        sy = y + around_dy[k];
        if (map_in_bounds(sx, sy) && (game.s.visibility[sx][sy] & (1 << civ)))
            square_redraw(sx, sy);
    }
    ui_redraw_around(x, y);
}

/* The display of a fight (f_1a19_000e): the attacker's square is drawn first; if it is in
 * the view the fight is shown and both squares are drawn again with their units. */
void combat_display(int civ, int u, int dciv, int du, int won)
{
    const Unit *a = &game.s.units[civ][u], *d = &game.s.units[dciv][du];

    view_follow(game.s.human_civ, d->x, d->y);
    if (!in_view(a->x, a->y))
        return;
    city_square_lookup(a->x, a->y);
    ui_combat(civ, u, dciv, du, won);
    square_redraw(a->x, a->y);
    square_redraw(d->x, d->y);
}
