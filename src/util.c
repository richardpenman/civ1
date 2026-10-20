#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include "game.h"
#include "map.h"
#include "util.h"

static unsigned long rand_seed = 1;

unsigned long game_rand_state(void)
{
    return rand_seed;
}

void game_srand(unsigned seed)
{
    rand_seed = seed;
}

/* Set CIV_TRACE_RAND=1 to list every random number's caller (debugging against the original). */
#ifdef __GNUC__
static void trace_rand(void *where)
{
    static int on = -1;
    Dl_info info;

    if (on < 0)
        on = getenv("CIV_TRACE_RAND") != NULL;
    if (on && dladdr(where, &info) && info.dli_sname)
        fprintf(stderr, "rand < %s+%#lx\n", info.dli_sname, (unsigned long)((char *)where - (char *)info.dli_saddr));
}
#endif

static void *rand_caller;

int game_rand(void)
{
#ifdef __GNUC__
    trace_rand(rand_caller ? rand_caller : __builtin_return_address(0));
    rand_caller = NULL;
#endif
    rand_seed = (rand_seed * 0x343FDUL + 0x269EC3UL) & 0xFFFFFFFFUL;
    return (int)((rand_seed >> 16) & 0x7FFF);
}

/* f_1dde_005b */
int rnd(int n)
{
#ifdef __GNUC__
    rand_caller = __builtin_return_address(0);
#endif
    return (int)(((long)n * game_rand()) >> 15);
}

/* f_1dde_007a */
int clamp(int v, int lo, int hi)
{
    if (v < lo)
        v = lo;
    if (v > hi)
        v = hi;
    return v;
}

/* f_1dde_0206 */
int distance(int dx, int dy)
{
    dx = abs(dx);
    dy = abs(dy);
    return dx > dy ? dx + dy / 2 : dy + dx / 2;
}

/* f_1dde_0287 (original argument order is x1, x2, y1, y2) */
int map_distance(int x1, int y1, int x2, int y2)
{
    int dx = abs(x1 - x2);

    if (dx > MAP_W / 2)
        dx = MAP_W - dx;
    return distance(dx, abs(y1 - y2));
}

/* f_1dde_0241 */
int map_distance2(int dx, int dy)
{
    dx = abs(dx);
    if (dx > MAP_W / 2)
        dx = MAP_W - dx;
    dy = abs(dy);
    return dx > dy ? 2 * dx + dy : 2 * dy + dx;
}

/* f_1b05_180b */
int sign(int v)
{
    return v > 0 ? 1 : v < 0 ? -1 : 0;
}

/* f_1dde_00b8 */
int city_at(int x, int y)
{
    int i;

    for (i = 0; i < MAX_CITIES; i++) {
        const City *c = &game.s.cities[i];
        if (c->status != NONE8 && c->x == x && c->y == y)
            return i;
    }
    return -1;
}

/* f_1dde_0100; the original leaves the distance in g_6c1a */
int nearest_city(int x, int y, int *dist)
{
    int i, d, best = 999, which = -1;

    for (i = 0; i < MAX_CITIES; i++) {
        const City *c = &game.s.cities[i];
        if (c->status == NONE8)
            continue;
        d = map_distance(x, y, c->x, c->y);
        if (d < best) {
            best = d;
            which = i;
        }
    }
    if (dist)
        *dist = best;
    return which;
}

/* f_1dde_0175 */
int nearest_unit(int civ, int exclude, int x, int y, int *dist)
{
    int i, d, best = 999, which = -1;

    for (i = 0; i < MAX_UNITS; i++) {
        const Unit *u = &game.s.units[civ][i];
        if (u->type == NONE8 || i == exclude)
            continue;
        d = map_distance(x, y, u->x, u->y);
        if (d < best) {
            best = d;
            which = i;
        }
    }
    if (dist)
        *dist = best;
    return which;
}

/* f_1dde_02cb: a size-n city holds 1+2+...+n tens of thousands of people. */
int civ_population(int civ)
{
    int i, k, total = 0;

    for (i = 0; i < MAX_CITIES; i++) {
        const City *c = &game.s.cities[i];
        if ((int8_t)c->owner != civ || c->status == NONE8)
            continue;
        for (k = 1; k <= (int8_t)c->size; k++)
            total += k;
    }
    return total;
}
