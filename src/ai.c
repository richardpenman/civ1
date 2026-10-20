#include "game.h"
#include "map.h"
#include "unit.h"
#include "util.h"
#include "ai.h"
#include "city.h"

AiGoals ai_goals[MAX_CIVS];
int ai_diplomat_civs;
int ai_at_war;

/* f_15da_364f: open slot `from` by moving later goals down one. */
static void ai_shift_goals(int civ, int from)
{
    AiGoals *g = &ai_goals[civ];
    int i;

    for (i = AI_GOALS - 2; i >= from; i--) {
        if (g->prio[i] == 0)
            continue;
        g->x[i + 1] = g->x[i];
        g->y[i + 1] = g->y[i];
        g->type[i + 1] = g->type[i];
        g->prio[i + 1] = g->prio[i];
    }
}

/* f_15da_34c8 */
void ai_add_goal(int civ, int x, int y, int type, int prio)
{
    AiGoals *g = &ai_goals[civ];
    int i, u, d;

    for (i = 0; i < AI_GOALS; i++)
        if (g->x[i] == x && g->y[i] == y && (int8_t)g->type[i] == type && g->prio[i] <= prio)
            return;

    /* A computer player sends the first suitable unit in range straight there. */
    if (civ == cur_player && civ != game.s.human_civ && (type == 3 || type == 4)) {
        for (u = 0; u < MAX_UNITS; u++) {
            Unit *p = unit_ptr(civ, u);
            if (p->type == NONE8 || p->moves == 0 || utype(p->type)->role != type)
                continue;
            if (map_continent(p->x, p->y) != map_continent(x, y))
                continue;
            d = map_distance(p->x, p->y, x, y);
            if ((int8_t)p->moves < d * 2)
                continue;
            p->goto_x = (uint8_t)x;
            p->goto_y = (uint8_t)y;
            p->status |= UNIT_GOTO;
            p->status &= 0xF0;
            break;
        }
    }

    for (i = 0; i < AI_GOALS; i++) {
        if (g->type[i] != NONE8 && g->prio[i] >= prio)
            continue;
        ai_shift_goals(civ, i);
        g->x[i] = (int8_t)x;
        g->y[i] = (int8_t)y;
        g->type[i] = (uint8_t)type;
        g->prio[i] = (int8_t)prio;
        return;
    }
}

/* f_15da_369e */
void ai_clear_continent_goals(int civ)
{
    int i;

    for (i = 0; i < 16; i++) {
        game.s.ai_spot_type[civ][i] = -1;
        game.s.ai_spot_value[civ][i] = 0;
    }
}

/* f_15da_3931 */
void city_rechoose_production(int city)
{
    City *c = &game.s.cities[city];
    int civ = (int8_t)c->owner;

    if (c->producing >= 0)
        game.s.units_in_production[civ][c->producing]--;
    c->producing = (int8_t)choose_production(civ, city);
    if (c->producing >= 0)
        game.s.units_in_production[civ][c->producing]++;
}

/* f_15da_38d4 */
void ai_rechoose_production(int civ, int continent)
{
    int i;

    for (i = 0; i < MAX_CITIES; i++) {
        const City *c = &game.s.cities[i];
        if ((int8_t)c->owner != civ || c->status == NONE8)
            continue;
        if (continent == -1 || map_continent(c->x, c->y) == continent)
            city_rechoose_production(i);
    }
}

/* f_15da_387c: forget points of interest of `type` within `range` of (x, y). */
void ai_forget_spot(int civ, int type, int x, int y, int range)
{
    int i;

    for (i = 0; i < 16; i++)
        if (game.s.ai_spot_type[civ][i] == type &&
            map_distance(x, y, game.s.ai_spot_x[civ][i], game.s.ai_spot_y[civ][i]) <= range)
            game.s.ai_spot_type[civ][i] = -1;
}

/* f_15da_430c: a human city has lost its last defender: rival transports at sea head there. */
void ai_go_for_undefended(int city)
{
    const City *c = &game.s.cities[city];
    int civ, u;

    for (civ = 1; civ < MAX_CIVS; civ++) {
        if (civ == game.s.human_civ || (game.s.diplomacy[civ][game.s.human_civ] & DIP_PEACE))
            continue;
        for (u = 0; u < MAX_UNITS; u++) {
            Unit *p = &game.s.units[civ][u];
            if (p->type == NONE8 || !utype(p->type)->capacity || p->next == NONE8 ||
                map_terrain(p->x, p->y) != TERRAIN_OCEAN)
                continue;
            if (utype(p->type)->moves * 3 <= map_distance(p->x, p->y, c->x, c->y))
                continue;
            p->goto_x = c->x;
            p->goto_y = c->y;
        }
    }
}
