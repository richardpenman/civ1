/* Cities: yields, workers, growth, production, happiness (original seg 0d07, seg 0ad5). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "civ.h"
#include "city.h"
#include "map.h"
#include "unit.h"
#include "util.h"
#include "replay.h"
#include "science.h"
#include "ai.h"
#include "ui.h"
#include "view.h"
#include "move.h"

CityCalc calc;
int yield_scoring;            /* g_2494: tile_yield() also feeds the AI accumulators */
int ai_support_score;         /* g_f2e2 */
int ai_unhappy_score;         /* g_ea62 */
int show_city_again;          /* g_c108: 1 = reopen the city screen, 2 = also rerun */

int calc_city;                /* g_64c2: city whose squares tile_yield() evaluates */
int disorder_shown;           /* g_6b12: the disorder picture was shown this turn */
int research_bonus;           /* g_e1fe: added to everyone's research cost this turn */

#define CITY(i) (&game.s.cities[i])

/* Food, shields or trade produced by square (x, y) when worked by `city` (f_0d07_68f0). */
int tile_yield(int city, int x, int y, int kind)
{
    return tile_yield_gov(city, (int8_t)CITY(city)->owner, x, y, kind);
}

/* The original takes the government from cur_civ, normally the city's owner. */
int tile_yield_gov(int city, int gov_civ, int x, int y, int kind)
{
    const City *c = CITY(city);
    int gov = game.s.government[gov_civ];
    int t, v, imp;

    if (!map_in_bounds(x, y))
        return 0;
    t = map_terrain(x, y);
    if (has_special_resource(x, y))
        v = (&terrain_types[t + TERRAIN_SPECIAL].food)[kind];
    else
        v = (&terrain_types[t].food)[kind];

    imp = map_improvements(x, y);
    if (!(game.s.rules_flags & RULE_REAL_IMPROVEMENTS)) {
        imp = t > TERRAIN_GRASSLAND ? IMP_MINE : IMP_IRRIGATION;
        if (t != TERRAIN_PLAINS)
            imp |= IMP_ROAD;
    }
    if (t != TERRAIN_OCEAN) {
        if (kind == YIELD_FOOD && (imp & IMP_IRRIGATION))
            v += -1 - terrain_improve[t].irrigate;
        if (kind == YIELD_SHIELDS && (imp & IMP_MINE))
            v += -1 - terrain_improve[t].mine;
        if (kind == YIELD_TRADE && (imp & IMP_ROAD) && t <= TERRAIN_GRASSLAND)
            v++;
    }
    /* Only some grassland and river squares carry a shield. */
    if (kind == YIELD_SHIELDS && (t == TERRAIN_GRASSLAND || t == TERRAIN_RIVER) && ((x * 7 + y * 11) & 2))
        v = 0;
    if (v != 0 && kind == YIELD_TRADE && wonder_active_city(WONDER_COLOSSUS) == city)
        v++;
    if (imp & IMP_RAILROAD)
        v += v / 2;
    if (v > 2 && !(c->status & CITY_CELEBRATING)) {
        if (gov <= 1)                         /* anarchy, despotism */
            v--;
        if (yield_scoring)
            ai_support_score -= 2;
    }
    if (v != 0 && kind == YIELD_TRADE) {
        if (yield_scoring)
            ai_unhappy_score++;
        if (gov >= ((c->status & CITY_CELEBRATING) ? 2 : 4))
            v++;
    }
    if (imp & IMP_POLLUTION)
        v = (v + 1) / 2;
    return v < 0 ? 0 : v;
}

/* f_0d07_6761: put a citizen to work on city square `sq` and add up its yield. */
static void city_work_square(int city, int sq, long mode)
{
    const City *c = CITY(city);
    int x = c->x + city_square_dx[sq], y = c->y + city_square_dy[sq], k;

    if (mode == 1) {
        city_square_lookup(x, y);
        ui_city_work_square(city, sq);
    } else {
        yield_scoring = 1;
        map_put_pixel(x + 80, y, (int8_t)c->owner);   /* square now belongs to this civ */
    }
    for (k = 0; k < 3; k++)
        calc.yield[k] += tile_yield(city, x, y, k);
    yield_scoring = 0;
}

/* f_0d07_6c89 / 6c56: specialists are 2-bit types in City.specialists (8 at most). */
int city_specialist(int i)
{
    if (i >= 8)
        return SPEC_TAXMAN;
    return (CITY(calc_city)->specialists >> (2 * i)) & 3;
}

void city_set_specialist(int i, int type)
{
    City *c = CITY(calc_city);

    if (i >= 8)
        return;
    c->specialists = (uint16_t)((c->specialists & ~(3 << (2 * i))) | (type << (2 * i)));
}

/* f_0d07_6cb4 */
static int city_count_specialists(int type)
{
    int i, n = 0;

    for (i = 0; i < 8; i++)
        if (city_specialist(i) == type)
            n++;
    return n;
}

/* f_0d07_6ce6: happy and unhappy citizens never outnumber the working population. */
static void city_clamp_happiness(int city, int specialists)
{
    int size = (int8_t)CITY(city)->size;

    calc.happy = clamp(calc.happy, 0, size);
    calc.unhappy = clamp(calc.unhappy, 0, size);
    while (calc.happy + calc.unhappy > clamp(size - specialists, 0, 99)) {
        calc.happy = clamp(calc.happy - 1, 0, size);
        calc.unhappy = clamp(calc.unhappy - 1, 0, size);
    }
}

/* f_0d07_6b67 */
void add_pollution(int x, int y)
{
    if (map_improvements(x, y) & IMP_POLLUTION)
        return;
    map_set_improvements(IMP_POLLUTION, x, y);
    map_refresh_known(x, y);
    game.s.pollution_count++;
}

/* f_0d07_6ba2: a nuclear explosion (or meltdown) at (x, y). */
void nuclear_fallout(int x, int y)
{
    int i, sx, sy, c;

    for (i = 0; i <= 8; i++) {
        sx = wrap_x(x + around_dx[i]);
        sy = y + around_dy[i];
        if (map_improvements(sx, sy) & IMP_CITY) {
            c = city_at(sx, sy);
            CITY(c)->size -= (int8_t)CITY(c)->size / 2;
        } else if (rnd(3) != 0 && map_terrain(sx, sy) != TERRAIN_OCEAN) {
            add_pollution(sx, sy);
        }
        square_redraw(sx, sy);
    }
}

/* f_0860_0006: a human city's packed fortified units come back as real units. */
void city_unpack_fortified(int city)
{
    City *c = CITY(city);
    int civ = (int8_t)c->owner, i, u;

    if (civ != game.s.human_civ)
        return;
    quiet = 1;
    for (i = 0; i < 2; i++) {
        if (c->fortified[i] == NONE8)
            continue;
        u = create_unit(civ, c->fortified[i] & 0x3F, c->x, c->y);
        if (u == -1)
            continue;
        game.s.units[civ][u].status |= UNIT_FORTIFIED;
        if (c->fortified[i] & 0x40)
            game.s.units[civ][u].status |= UNIT_VETERAN;
        c->fortified[i] = NONE8;
    }
    quiet = 0;
    c->status |= CITY_PACK_UNITS;
}

/* f_0860_00c6: fold up to two fortified home units of a human city into the city record. */
void city_pack_fortified(int city)
{
    City *c = CITY(city);
    int civ = (int8_t)c->owner, saved, u, any = 0;

    if (civ != game.s.human_civ)
        return;
    saved = count_losses;
    count_losses = 0;
    quiet = 1;
    for (u = 0; u < MAX_UNITS; u++) {
        Unit *p = &game.s.units[civ][u];
        uint8_t *slot;
        if (p->type == NONE8 || p->x != c->x || p->y != c->y || (int8_t)p->home_city != city ||
            !(p->status & UNIT_FORTIFIED))
            continue;
        if (!any) {
            any = 1;            /* the first one stays as a real unit */
            continue;
        }
        if (c->fortified[0] == NONE8)
            slot = &c->fortified[0];
        else if (c->fortified[1] == NONE8)
            slot = &c->fortified[1];
        else
            break;
        *slot = p->type;
        if (p->status & UNIT_VETERAN)
            *slot |= 0x40;
        delete_unit(civ, u);
    }
    quiet = 0;
    count_losses = saved;
    c->status &= ~CITY_PACK_UNITS;
}

/* f_0ad5_0190 */
void destroy_city(int city, int x, int y)
{
    City *c = CITY(city);
    int owner = (int8_t)c->owner, i, k, top, sx, sy, dist;

    replay_log(1, 4, -1, c->name, x, y);
    if (c->producing >= 0)
        game.s.units_in_production[owner][c->producing]--;
    game.s.city_count[owner]--;
    c->status = NONE8;
    map_set_improvements(0, x, y);
    if (game.s.visibility[x][y] & (1 << game.s.human_civ))
        map_refresh_known(x, y);
    for (i = 0; i < MAX_CITIES; i++)
        for (k = 0; k < 3; k++)
            if (game.s.cities[i].trade_routes[k] == city)
                game.s.cities[i].trade_routes[k] = NONE8;
    for (k = 1; k <= NUM_WONDERS; k++)
        if (game.s.wonder_city[k] == city)
            game.s.wonder_city[k] = 128;          /* destroyed */
    for (k = 0; k < MAX_UNITS; k++)
        if (game.s.units[owner][k].type != NONE8 && (int8_t)game.s.units[owner][k].home_city == city)
            delete_unit(owner, k);
    top = unit_at(x, y);
    if (top != -1)
        stack_foreach(owner, top, cb_delete_nonland);
    /* Hand the surrounding squares back to whoever owned them before. */
    for (k = 0; k < 45; k++) {
        sx = wrap_x(x + around_dx[k]);
        sy = y + around_dy[k];
        nearest_city(sx, sy, &dist);
        if (k > 20 && dist <= 3)
            continue;
        map_put_pixel(sx + 80, sy, map_pixel(sx + 80, sy + 50));
    }
}

static void city_text(char *buf, size_t size, const char *before, int city, const char *after)
{
    snprintf(buf, size, "%s%s%s", before, game.s.city_names[CITY(city)->name], after);
}

static void change_production(int civ, int city, int choice)
{
    City *c = CITY(city);

    if (c->producing >= 0)
        game.s.units_in_production[civ][c->producing]--;
    c->producing = (int8_t)choice;
    if (c->producing >= 0)
        game.s.units_in_production[civ][c->producing]++;
}

/* Cost of the current build in shields.  The original reads the unit table with a signed
 * index, so "unit" -1 (the Palace) costs wonder_obsolete[17]'s 127; reproduced here. */
static int unit_cost_signed(int type)
{
    return type >= 0 ? game.s.unit_types[type].cost : (type == -1 ? 127 : 0);
}

/* Which squares this city may work: the [5][5] neighbourhood marks squares other cities
 * already use, `blocked` covers the city's own 21 squares. */
static void city_find_blocked(int city, uint8_t taken[5][5], uint8_t blocked[21])
{
    City *c = CITY(city);
    int civ = (int8_t)c->owner, i, x, y, o;

    calc.enemy_near = 0;
    for (i = 0; i < 21; i++) {
        blocked[i] = 0;
        x = c->x + city_square_dx[i];
        y = c->y + city_square_dy[i];
        if (civ != 0 && !((1 << civ) & (int8_t)game.s.visibility[x][y]))
            blocked[i] = 1;
        o = map_unit_owner(x, y);
        if (o != -1 && o != civ) {
            taken[city_square_dx[i] + 2][city_square_dy[i] + 2] = 1;
            blocked[i] = 1;
            if (o == game.s.human_civ)
                calc.enemy_near = 1;
        }
        if (taken[city_square_dx[i] + 2][city_square_dy[i] + 2])
            blocked[i] = 1;
        if (blocked[i])
            c->worked &= ~(1UL << i);
    }
}

/* Count the units a city supports (f_0d07_0043, twice): packed units first, then real ones. */
static void city_count_support(int city, int full)
{
    City *c = CITY(city);
    int civ = (int8_t)c->owner, size = (int8_t)c->size, i;

    calc.support = 0;
    calc.shield_upkeep = 0;
    if (full) {
        calc.away = 0;
        calc.settlers = 0;
    }
    for (i = 0; i < 2; i++)
        if (c->fortified[i] != NONE8)
            calc.support++;
    if (size < calc.support)
        calc.shield_upkeep = calc.support - size;
    if (!full)
        return;
    for (i = 0; i < MAX_UNITS; i++) {
        const Unit *u = &game.s.units[civ][i];
        if (u->type == NONE8 || (int8_t)u->home_city != city)
            continue;
        if (u->type < UNIT_DIPLOMAT) {
            calc.support++;
            if (size < calc.support ||
                (game.s.government[civ] > 1 && (game.s.rules_flags & RULE_REAL_IMPROVEMENTS)))
                calc.shield_upkeep++;
            if (u->type != UNIT_SETTLERS &&
                (game.s.unit_types[u->type].domain == DOMAIN_AIR || u->x != c->x || u->y != c->y))
                calc.away++;
        }
        if (u->type == UNIT_SETTLERS)
            calc.settlers++;
    }
}

/* Assign citizens to squares (or keep the human's choices) and add up the yields. */
static void city_assign_workers(int city, long mode, uint8_t blocked[21])
{
    City *c = CITY(city);
    int civ = (int8_t)c->owner, size = (int8_t)c->size, i, free, best, best_food, best_rest, f, s, t, score;
    int settler_food = calc.settler_food;
    uint32_t newbits = 0;
    int first = 1;

    for (i = 0; i < 4; i++)
        calc.yield[i] = 0;
    free = size + 1;

    /* Computer players rethink their squares every fourth turn (unless troubled). */
    if ((game.s.human_mask & (1 << civ)) || ((city + game.s.turn) & 3) != 0 ||
        (c->status & CITY_DISORDER) || calc.enemy_near) {
        /* Keep the squares already worked, if they still fit. */
        for (i = 0; i < 21; i++)
            if (c->worked & (1UL << i))
                free--;
        if (free >= 0) {
            free = size + 1;
            for (i = 0; i < 21 && free != 0; i++) {
                if (!(c->worked & (1UL << i)))
                    continue;
                blocked[i] = 1;
                city_work_square(city, i, mode);
                free--;
                newbits |= 1UL << i;
            }
        } else {
            if (mode == 0 && civ == game.s.human_civ) {
                char msg[96];
                city_text(msg, sizeof msg, "Population decrease\nin ", city, ".\n");
                ui_message(msg, 4);
                show_city_again = 1;
            }
            free = size + 1;
        }
        if (free == calc.specialists || free == 0)
            goto done;
    }

    /* The city square, then the best food squares while food is short. */
    if (free > 0 && !blocked[20]) {
        blocked[20] = 1;
        city_work_square(city, 20, mode);
        newbits |= 1UL << 20;
        free--;
    }
    while ((size * 2 + settler_food * calc.settlers > free / 2 + calc.yield[YIELD_FOOD] || (int8_t)c->size < 3) &&
           free > calc.specialists) {
        /* The original tests an uninitialised "best" here on the first pass; it is
         * practically never -1, so the loop always runs at least once. */
        if (!first && best == -1)
            break;
        first = 0;
        best = -1;
        best_rest = 0;
        best_food = 0;
        for (i = 0; i < 21; i++) {
            int x, y;
            if (blocked[i])
                continue;
            x = c->x + city_square_dx[i];
            y = c->y + city_square_dy[i];
            f = tile_yield(city, x, y, YIELD_FOOD);
            score = tile_yield(city, x, y, YIELD_SHIELDS) * 2;
            if (free == 1 && calc.yield[YIELD_SHIELDS] == 0 && score == 0)
                continue;
            score += tile_yield(city, x, y, YIELD_TRADE);
            if (f > best_food || (f == best_food && score > best_rest)) {
                best_food = f;
                best_rest = score;
                best = i;
            }
        }
        if (best == -1)
            break;
        blocked[best] = 1;
        city_work_square(city, best, mode);
        newbits |= 1UL << best;
        free--;
    }

    /* Then by a weighted value; computer players in disorder make one more entertainer. */
    if (civ != game.s.human_civ && (c->status & CITY_DISORDER))
        calc.specialists++;
    while (free > calc.specialists) {
        int wf, ws, wt;
        best = -1;
        best_rest = 0;
        for (i = 0; i < 21; i++) {
            int x, y;
            if (blocked[i])
                continue;
            x = c->x + city_square_dx[i];
            y = c->y + city_square_dy[i];
            wf = clamp(calc.yield[YIELD_FOOD] - size * 2 - settler_food * calc.settlers, 1, 99);
            f = tile_yield(city, x, y, YIELD_FOOD);
            score = f * (16 / wf);
            s = tile_yield(city, x, y, YIELD_SHIELDS);
            ws = clamp(calc.yield[YIELD_SHIELDS] - calc.shield_upkeep, 1, 99);
            score += (int8_t)c->size * 3 / ws * s;
            t = tile_yield(city, x, y, YIELD_TRADE);
            wt = clamp(calc.yield[YIELD_TRADE], 1, 99);
            score += size * 2 / wt * t;
            if (score > best_rest) {
                best_rest = score;
                best = i;
            }
        }
        if (best == -1)
            break;
        blocked[best] = 1;
        city_work_square(city, best, mode);
        newbits |= 1UL << best;
        free--;
    }
done:
    c->worked = ((uint32_t)free << 26) + newbits;
    calc.free = free;
}

/* first_discoverer[i]; the original also reads index -1, the last word of the far block
 * saved just before it. */
static int first_discoverer_at(int i)
{
    if (i < 0)
        return game.s.unk_far_2786_0000[1198] | game.s.unk_far_2786_0000[1199] << 8;
    return game.s.first_discoverer[i];
}

/* Turn food into growth or famine (mode 0).  Returns 1 if the city is gone. */
static int city_food(int city, int box)
{
    City *c = CITY(city);
    int civ = (int8_t)c->owner, u, settler = -1;
    char msg[128];

    c->status &= 0x7F;
    if (c->food < 0) {
        for (u = 0; u < MAX_UNITS; u++) {
            const Unit *p = &game.s.units[civ][u];
            if (p->type == UNIT_SETTLERS && (int8_t)p->home_city == city) {
                settler = u;
                break;
            }
        }
        if (civ == game.s.human_civ) {
            city_text(msg, sizeof msg, "Food storage exhausted\nin ", city,
                      settler != -1 ? "!\nSettlers lost.\n" : "!\nFamine feared.\n");
            newspaper(city, msg);
        }
        if (settler != -1) {
            delete_unit(civ, settler);
        } else if (--c->size == 0) {
            destroy_city(city, c->x, c->y);
            civ_check_destroyed(civ, 0);
            return 1;
        }
        c->food = 0;
        redraw_if_seen(c->x, c->y);
    }
    if (((int8_t)c->size + 1) * box <= c->food) {
        c->size++;
        c->food = (c->buildings & BIT(B_GRANARY)) ? c->size * 5 + 5 : 0;
        if (c->size > 10 && !(c->buildings & BIT(B_AQUEDUCT)) && (game.s.rules_flags & RULE_GROWTH_LIMITS)) {
            c->size--;
            if (civ == game.s.human_civ) {
                city_text(msg, sizeof msg, "", city, " requires an AQUEDUCT\nfor further growth.\n");
                ui_message(msg, 4);
            }
        }
        redraw_if_seen(c->x, c->y);
    }
    return 0;
}

/* AI: a new caravan goes straight to the richest foreign city (f_0d07_0043). */
static void ai_new_caravan(int civ, int unit)
{
    int i, best = -1, best_trade = -1;

    for (i = 0; i < MAX_CITIES; i++) {
        const City *o = CITY(i);
        if ((int8_t)o->owner == civ || o->status == NONE8 || (int8_t)o->owner == game.s.human_civ)
            continue;
        if ((int8_t)o->trade > best_trade) {
            best_trade = (int8_t)o->trade;
            best = i;
        }
    }
    if (best != -1)       /* (the original goes ahead with city -1 and reads stray memory) */
        caravan_arrive(civ, unit, best);
}

/* AI: a new diplomat goes off towards a nearby human city.  Returns 0 to refund it. */
static int ai_new_diplomat(int city, int unit)
{
    int civ = (int8_t)CITY(city)->owner, i, u, d, best = -1, best_d = 32767, ties = 0, target = 0;

    for (i = 0; i < MAX_CITIES; i++) {
        const City *o = CITY(i);
        if ((int8_t)o->owner != game.s.human_civ || o->status == NONE8)
            continue;
        u = nearest_unit(civ, unit, o->x, o->y, &d);
        d = abs(d - 3);
        if (o->status & 0x20)
            d = 3;
        if (best_d > d) {
            /* closer */
        } else if (best_d == d) {
            if (rnd(++ties) != 0)
                continue;
        } else {
            continue;
        }
        if (map_terrain(game.s.units[civ][u].x, game.s.units[civ][u].y) == TERRAIN_OCEAN)
            continue;
        if (map_continent(game.s.units[civ][u].x, game.s.units[civ][u].y) != map_continent(o->x, o->y))
            continue;
        if (best_d == d)
            ties = 1;
        else
            best_d = d;
        best = u;
        target = i;
    }
    if (best_d > 3 || (game.s.diplomacy[civ][game.s.human_civ] & DIP_PEACE))
        return 0;
    u = create_unit(civ, UNIT_DIPLOMAT, game.s.units[civ][best].x, game.s.units[civ][best].y);
    if (u != -1) {
        game.s.units[civ][u].goto_x = CITY(target)->x;
        game.s.units[civ][u].goto_y = CITY(target)->y;
    }
    return 1;
}

/* A unit is complete (f_0d07_0043).  Returns 1 if the city is gone. */
static int city_finish_unit(int city, int box)
{
    City *c = CITY(city);
    int civ = (int8_t)c->owner, type = c->producing, u = -1;
    char msg[128];

    c->shields -= game.s.unit_types[type].cost * box;
    if (civ == game.s.human_civ || type != UNIT_DIPLOMAT)
        u = create_unit(civ, type, c->x, c->y);
    if (!(game.s.first_discoverer[type] & 8)) {
        replay_log(6, 2, civ, type, 0, 0);
        game.s.first_discoverer[type] |= 8;
    }
    if ((c->buildings & BIT(B_BARRACKS)) && u != -1)
        game.s.units[civ][u].status |= UNIT_VETERAN;
    if (u != -1 && type == UNIT_SETTLERS && ((int8_t)c->size > 1 || game.s.city_count[civ] > 1)) {
        if (--c->size == 0) {
            destroy_city(city, c->x, c->y);
            create_unit(civ, type, c->x, c->y);
            civ_check_destroyed(civ, 0);
            return 1;
        }
    }
    if (civ == game.s.human_civ) {
        c->shields = 0;
        if (u != -1 && (type == UNIT_SETTLERS || type >= UNIT_DIPLOMAT)) {
            show_city_again = 1;
            snprintf(msg, sizeof msg, "%s builds %s.\n", game.s.city_names[c->name], game.s.unit_type_names[type]);
            ui_message(msg, 3);
        }
        return 0;
    }
    if (type == UNIT_CARAVAN)
        ai_new_caravan(civ, u);
    if (type == UNIT_DIPLOMAT && !ai_new_diplomat(city, u))
        c->shields += game.s.unit_types[type].cost * box;
    if (type == UNIT_NUCLEAR && game.s.units_active[civ][UNIT_NUCLEAR] == 1)
        game.s.unk_e204[game.s.human_civ] = -1;
    city_rechoose_production(city);
    return 0;
}

/* A building, wonder or spaceship part is complete (f_0d07_0043).  Returns the
 * building, or -1 if it was already there. */
static int city_finish_building(int city, int box)
{
    City *c = CITY(city);
    int civ = (int8_t)c->owner, b = -c->producing, i, parts;
    int human = game.s.human_civ;
    char msg[160];

    if (b > 24) {
        if (game.s.wonder_city[b - 24] == -1) {
            game.s.wonder_city[b - 24] = (int16_t)city;
            replay_log(10, 2, civ, b - 24, 0, 0);
        } else {
            b = -1;
        }
    } else if (c->buildings & BIT(b)) {
        b = -1;
    }
    if (b == -1)
        return -1;
    if (b == 41) {                                     /* Manhattan Project */
        city_text(msg, sizeof msg, "Sensors report a\nNUCLEAR WEAPONS test\nnear ", city, "!\n");
        newspaper(-1, msg);
    }
    if (b < 22)
        c->buildings |= BIT(b);
    c->shields -= building_types[-c->producing].cost * box;

    if ((game.s.human_mask & (1 << civ)) || b > 24) {
        if (game.s.human_mask & (1 << civ))
            show_city_again = 1;
        else
            game.s.visibility[c->x][c->y] = 0xFF;      /* everyone hears of a wonder */
        snprintf(msg, sizeof msg, "%s builds\n%s.\n", game.s.city_names[c->name], building_types[b].name);
        if (civ == human && (game.s.unk_19c0 & 8) && (b <= 21 || b > 24) &&
            !(c->status & CITY_AUTOBUILD) && b != B_PALACE)
            city_view(city, b);
        else
            newspaper(city, msg);
        c->shields = 0;
    } else {
        if (b == B_PALACE)
            c->producing = -2;
        c->producing = (int8_t)choose_production(civ, city);
        if (c->producing >= 0)
            game.s.units_in_production[civ][c->producing]++;
    }

    if (b >= 22 && b <= 24 && !(game.s.unk_4c88 & (1 << civ))) {
        if (civ == human) {
            spaceship_part_human(civ, b - 22);
        } else {
            parts = spaceship_part_ai(civ, b - 22);
            if (parts && b < 24) {
                c->shields += building_types[b].cost * box;
                if (c->producing <= -22 && c->producing >= -24)
                    c->producing = (int8_t)(-b - 1);
                parts--;
            }
            if (!(game.s.unk_4c88 & (1 << civ)) && game.s.unk_4c86 >= 40) {
                int launch = 0;
                if (parts) {
                    launch = 1;
                } else if (civ_has_advance(human, 40)) {
                    /* The original indexes the treasury with the alive mask here; any
                     * index beyond the 8 civs reads unrelated memory, taken as 0. */
                    int t = game.s.civs_alive < MAX_CIVS ? game.s.treasury[game.s.civs_alive] : 0;
                    if (game.s.unk_4c86 > 75 || game.s.civ_rank[human] > game.s.civ_rank[civ])
                        launch = 1;
                    else if ((game.s.unk_4c88 & (1 << human)) &&
                             game.s.unk_ed30[civ] - game.s.year <= game.s.unk_ed30[human] - game.s.year)
                        launch = 1;
                    else if ((game.s.unk_4c88 & (1 << (human + 8))) && t > 1000)
                        launch = 1;
                }
                if (launch)
                    spaceship_launch(civ);
            } else if (parts) {
                spaceship_launch(civ);
            }
        }
    }

    if (b == B_PALACE) {                               /* Palace moved */
        for (i = 0; i < MAX_CITIES; i++)
            if ((int8_t)CITY(i)->owner == civ)
                CITY(i)->buildings &= ~BIT(B_PALACE);
        if (civ == human || (game.s.diplomacy[human][civ] & DIP_EMBASSY)) {
            snprintf(msg, sizeof msg, "%s%s capital\nmoved to %s.\n", civ == human ? "" : "Diplomats report:\n",
                     game.s.civ_adjectives[civ], game.s.city_names[c->name]);
            ui_message(msg, 5);
        }
        game.s.capital_x[civ] = c->x;
        c->buildings |= BIT(B_PALACE);
    }
    if (b == 38) {                                     /* Darwin's Voyage */
        research_complete(civ, 0);
        research_complete(civ, 0);
    }
    if (b == 43 && civ == human) {                     /* Apollo Program shows every city */
        for (i = 0; i < MAX_CITIES; i++) {
            City *o = CITY(i);
            if (o->status == NONE8 || (int8_t)o->owner == civ)
                continue;
            o->visible_size = o->size;
            game.s.visibility[o->x][o->y] |= 1 << civ;
            map_refresh_known(o->x, o->y);
        }
    }
    return b;
}

/* AI: buy part of what a city builds (f_0d07_0043). */
static void ai_buy(int city, int box, int last_built)
{
    City *c = CITY(city);
    int civ = (int8_t)c->owner, gold = game.s.treasury[civ], spend = 0, strategy;

    if (c->status & CITY_AUTOBUILD)
        city_rechoose_production(city);
    strategy = game.s.unk_f5b8[civ][map_continent(c->x, c->y)];
    if ((strategy == 1 || strategy == 2 || strategy == 5) && last_built != 0 && c->producing >= 0 &&
        game.s.unit_types[c->producing].role == strategy)
        spend = gold / 64;
    if ((game.s.unk_4c88 & (1 << (game.s.human_civ + 8))) && -c->producing >= 22 && -c->producing <= 24)
        spend = gold / 128;
    if ((c->status & CITY_DISORDER) && c->producing < 0 && c->shields != 0)
        spend = clamp(building_types[-c->producing].cost * box - c->shields, 0, gold / 8);
    if ((map_unit_owner(c->x, c->y) + 1 == 0 || (c->status & CITY_AUTOBUILD)) && c->producing >= 0 && c->shields != 0)
        spend = clamp(game.s.unit_types[c->producing].cost * box - c->shields, 0, gold / 3);
    if (c->producing == -1 && c->shields != 0)
        spend = clamp(unit_cost_signed(-1) * box - c->shields, 0, gold / 3);
    if (c->status == 25 && game.s.units_active[civ][UNIT_NUCLEAR] == 0 && c->shields != 0)  /* sic: status */
        spend = clamp(unit_cost_signed(c->producing) * box - c->shields, 0, gold / 4);
    if (gold > 2000)
        spend += gold / 512;
    c->shields += spend;
    game.s.treasury[civ] -= spend * 2;
    c->status &= ~CITY_AUTOBUILD;
}

/* Production for one turn (mode 0).  Returns 1 if the city is gone. */
static int city_production(int city, int box)
{
    City *c = CITY(city);
    int civ = (int8_t)c->owner, choice;
    int last;     /* the original reuses one local: shield surplus, or the building just finished */

    c->food += calc.yield[YIELD_FOOD] - calc.settler_food * calc.settlers - (int8_t)c->size * 2;
    last = calc.yield[YIELD_SHIELDS] - calc.shield_upkeep;
    if (c->status & CITY_DISORDER)
        last = 0;
    c->shields += last;

    if (c->producing >= 0) {
        if (game.s.unit_types[c->producing].cost * box <= c->shields) {
            if (c->producing == UNIT_SETTLERS && c->size == 1 && game.s.difficulty == 0)
                goto buy;      /* Chieftain: a size-1 city waits for its settlers */
            if (city_finish_unit(city, box))
                return 1;
        }
    } else if (building_types[-c->producing].cost * box <= c->shields) {
        last = city_finish_building(city, box);
        if (c->status == NONE8)
            return 1;
    }

    if (civ == game.s.human_civ && (c->status & CITY_AUTOBUILD) &&
        (c->shields == 0 || (c->producing < 0 && last == -1))) {
        choice = choose_production(civ, city);
        if (choice != 99) {
            show_city_again = 0;
            change_production(civ, city, choice);
        } else {
            c->status &= ~CITY_AUTOBUILD;
        }
    }
buy:
    if (!(game.s.human_mask & (1 << civ)))
        ai_buy(city, box, last);
    return 0;
}

/* Corruption, trade routes and the luxury/tax/science split. */
static void city_trade(int city)
{
    City *c = CITY(city);
    int civ = (int8_t)c->owner, gov = game.s.government[civ], k, o, sci;

    if (gov == GOV_COMMUNISM)
        calc.capital_distance = 10;
    calc.corruption = calc.yield[YIELD_TRADE] * calc.capital_distance * 3 / (gov * 20 + 80);
    if (c->buildings & (BIT(B_COURTHOUSE) | BIT(B_PALACE)))
        calc.corruption /= 2;
    if (gov == GOV_DEMOCRACY)
        calc.corruption = 0;
    c->trade = (uint8_t)(calc.yield[YIELD_TRADE] - calc.corruption);
    for (k = 0; k < 3; k++) {
        o = c->trade_routes[k];
        if (o == NONE8)
            continue;
        if ((int8_t)CITY(o)->owner != civ)
            calc.yield[YIELD_TRADE] += ((int8_t)CITY(o)->trade + calc.yield[YIELD_TRADE] + 4) / 8;
        else
            calc.yield[YIELD_TRADE] += ((int8_t)CITY(o)->trade + calc.yield[YIELD_TRADE] + 4) / 16;
    }
    calc.corruption = calc.yield[YIELD_TRADE] * calc.capital_distance * 3 / (gov * 20 + 80);
    if (gov == GOV_DEMOCRACY)
        calc.corruption = 0;
    if (c->buildings & BIT(B_COURTHOUSE))
        calc.corruption /= 2;

    k = calc.yield[YIELD_TRADE] - calc.corruption;
    calc.lux = clamp(((10 - game.s.science_rate[civ] - game.s.tax_rate[civ]) * k + 5) / 10, 0, calc.yield[YIELD_TRADE]);
    calc.tax = clamp((game.s.tax_rate[civ] * k + 5) / 10, 0, calc.yield[YIELD_TRADE] - calc.lux - calc.corruption);
    calc.science = calc.yield[YIELD_TRADE] - calc.lux - calc.tax - calc.corruption;
    calc.tax += city_count_specialists(SPEC_TAXMAN) * 2;
    calc.science += city_count_specialists(SPEC_SCIENTIST) * 2;
    calc.lux += city_count_specialists(SPEC_ENTERTAINER) * 2;
    if (c->buildings & BIT(B_MARKETPLACE)) {
        calc.lux += calc.lux / 2;
        calc.tax += calc.tax / 2;
    }
    if (c->buildings & BIT(B_BANK)) {
        calc.lux += calc.lux / 2;
        calc.tax += calc.tax / 2;
    }
    sci = calc.science;
    if (c->buildings & BIT(B_LIBRARY)) {
        sci += calc.science / 2;
        if (civ_has_wonder(civ, WONDER_NEWTON))
            sci += calc.science / 3;
    }
    if (c->buildings & BIT(B_UNIVERSITY)) {
        sci += calc.science / 2;
        if (civ_has_wonder(civ, WONDER_NEWTON))
            sci += calc.science / 3;
    }
    if (wonder_active_city(WONDER_COPERNICUS) == city)
        sci += sci;
    calc.science = sci;
}

static void city_happiness(int city, int specialists, long mode)
{
    City *c = CITY(city);
    int civ = (int8_t)c->owner, gov = game.s.government[civ], size = (int8_t)c->size, i, u, top, n;

    /* (mode 1: the HAPPY page of the city screen shows each step, ui_city_happy) */
    calc.happy = 0;
    calc.unhappy = civ == game.s.human_civ ? size + game.s.difficulty - 6 : size - 3;
    city_clamp_happiness(city, specialists);
    if (mode == 1)
        ui_city_happy(city, HAPPY_ROW, 0);
    calc.happy = calc.lux / 2;
    city_clamp_happiness(city, specialists);
    if (mode == 1 && calc.happy != 0)
        ui_city_happy(city, HAPPY_LUX_ROW, 0);
    if (c->buildings & BIT(B_COLOSSEUM)) {
        calc.unhappy -= 3;
        if (mode == 1)
            ui_city_happy(city, HAPPY_BUILDING, B_COLOSSEUM);
    }
    if (civ_has_advance(civ, ADV_RELIGION)) {
        calc.unhappy -= (c->buildings & BIT(B_CATHEDRAL)) ? (civ_has_wonder(civ, WONDER_MICHELANGELO) ? 6 : 4) : 0;
        if (mode == 1 && (c->buildings & BIT(B_CATHEDRAL)))
            ui_city_happy(city, HAPPY_BUILDING, B_CATHEDRAL);
    }
    if (c->buildings & BIT(B_TEMPLE)) {
        if (civ_has_advance(civ, ADV_MYSTICISM))
            calc.unhappy -= 2;
        else if (civ_has_advance(civ, ADV_CEREMONIAL_BURIAL))
            calc.unhappy--;
        if (civ_has_wonder(civ, WONDER_ORACLE))
            calc.unhappy -= civ_has_advance(civ, ADV_MYSTICISM) ? 2 : 1;
        if (mode == 1)
            ui_city_happy(city, HAPPY_BUILDING, B_TEMPLE);
    }
    city_clamp_happiness(city, specialists);
    if (mode == 1 && (c->buildings & (BIT(B_TEMPLE) | BIT(B_CATHEDRAL) | BIT(B_COLOSSEUM))))
        ui_city_happy(city, HAPPY_ROW, 0);
    if (mode == 1)
        ui_city_happy(city, HAPPY_NEXT, 0);

    if (gov < GOV_REPUBLIC) {
        /* Martial law. */
        for (i = 0; i < 2; i++)
            if (calc.unhappy != 0 && c->fortified[i] != NONE8 &&
                game.s.unit_types[c->fortified[i] & 0x3F].attack > 0)
                calc.unhappy--;
        top = unit_at(c->x, c->y);
        /* (a broken stack ring never leads back to top: the original hangs; stop) */
        for (u = top, n = 0; u != -1 && n < 1000; n++) {
            if (game.s.unit_types[game.s.units[civ][u].type].attack > 0) {
                if (calc.unhappy > 0)
                    calc.unhappy--;
                if (mode == 1)
                    ui_city_happy(city, HAPPY_UNIT, game.s.units[civ][u].type + civ * 32 + 64);
            }
            u = (int8_t)game.s.units[civ][u].next;
            if (u == top)
                u = -1;
        }
    } else {
        n = !civ_has_wonder(civ, WONDER_WOMENS_SUFFRAGE);
        if (gov == GOV_DEMOCRACY)
            n++;
        if (n) {
            calc.unhappy += n * calc.away;
            if (mode == 1)
                ui_city_happy(city, HAPPY_AWAY, n * calc.away);
        }
    }
    city_clamp_happiness(city, specialists);
    if (mode == 1)
        ui_city_happy(city, HAPPY_ROW, 0);
    n = calc.happy - calc.unhappy;

    if (civ_has_wonder(civ, WONDER_HANGING_GARDENS))
        calc.happy++;
    if (civ_has_wonder(civ, WONDER_CURE_FOR_CANCER))
        calc.happy++;
    if (wonder_active_city(WONDER_SHAKESPEARE) == city)
        calc.unhappy = 0;
    if (civ_has_wonder(civ, WONDER_BACH)) {
        int w = game.s.wonder_city[WONDER_BACH];
        if (map_continent(c->x, c->y) == map_continent(CITY(w)->x, CITY(w)->y))
            calc.unhappy -= 2;
    }
    city_clamp_happiness(city, specialists);
    if (mode == 1 && calc.happy - calc.unhappy != n)
        ui_city_happy(city, HAPPY_WONDERS_ROW, 0);
    if (!(game.s.rules_flags & RULE_REAL_IMPROVEMENTS))
        calc.happy = calc.unhappy = 0;
}

/* Units the city cannot pay for are disbanded, farthest first (mode 0 only). */
static void city_support(int city, long mode)
{
    City *c = CITY(city);
    int civ = (int8_t)c->owner, i, k, far, far_d, d;
    char msg[160];

again:
    city_count_support(city, 0);
    for (i = 0; i < MAX_UNITS; i++) {
        const Unit *u = &game.s.units[civ][i];
        if (u->type == NONE8)
            continue;
        if ((int8_t)u->home_city != city) {
            if (mode == 1)
                ui_city_unit(city, civ, i, 0);           /* (only the INFO page's list) */
            continue;
        }
        if (u->type < UNIT_DIPLOMAT) {
            calc.support++;
            if ((int8_t)c->size < calc.support ||
                (game.s.government[civ] > 1 && (game.s.rules_flags & RULE_REAL_IMPROVEMENTS)))
                calc.shield_upkeep++;
        }
        if (mode == 1)
            ui_city_unit(city, civ, i, 1);
        if (mode != 0)
            continue;
        if (calc.yield[YIELD_SHIELDS] >= calc.shield_upkeep &&
            !((c->status & CITY_DISORDER) && ((i + game.s.turn) & 7) == 0 && civ != game.s.human_civ &&
              game.s.government[civ] >= GOV_REPUBLIC))
            continue;
        far = -1;
        far_d = -1;
        for (k = 0; k < MAX_UNITS; k++) {
            const Unit *v = &game.s.units[civ][k];
            if (v->type == NONE8 || (int8_t)v->home_city != city || v->type >= UNIT_DIPLOMAT)
                continue;
            d = map_distance(c->x, c->y, v->x, v->y);
            if (d > far_d) {
                far_d = d;
                far = k;
            }
        }
        if (calc.yield[YIELD_SHIELDS] >= calc.shield_upkeep) {
            /* A computer player's unhappy city sends a far-away soldier home for good. */
            if (ai_flag_8f98 && game.s.advances_known[civ] < game.s.advances_known[game.s.human_civ])
                ai_unhappy_score = -999;
            else if (far_d > 0 && game.s.unit_types[game.s.units[civ][far].type].domain == DOMAIN_LAND &&
                     game.s.units[civ][far].type != UNIT_SETTLERS) {
                delete_unit(civ, far);
                c->status &= ~CITY_DISORDER;
            }
            continue;
        }
        if (civ == game.s.human_civ) {
            snprintf(msg, sizeof msg, "%s can't support\n%s.\n Unit Disbanded.\n", game.s.city_names[c->name],
                     game.s.unit_type_names[game.s.units[civ][far].type]);
            ui_message(msg, 3);
        }
        delete_unit(civ, far);
        goto again;
    }
    if (mode != 0)
        return;
    if (calc.yield[YIELD_SHIELDS] < calc.support)
        ai_support_score += (calc.support - calc.yield[YIELD_SHIELDS]) * 5;
    ai_support_score += clamp(calc.support, 0, (int8_t)c->size);
    ai_unhappy_score -= ((c->buildings & BIT(B_MARKETPLACE) ? 5 : 7) -
                         civ_types[game.s.civ_type[civ]].science_bias) * calc.away;
}

/* Disorder, celebration, taxes, science, pollution, upkeep (mode 0 end of turn). */
static void city_end_turn(int city, int box)
{
    City *c = CITY(city);
    int civ = (int8_t)c->owner, gov = game.s.government[civ], human = game.s.human_civ;
    int mood = calc.happy - calc.unhappy, sci, mult, k, b, x, y;
    char msg[160];

    if (civ != 0) {
        if (mood < 0) {
            if (game.s.human_mask & (1 << civ)) {
                city_text(msg, sizeof msg, "Civil Disorder in\n", city, "! Mayor\nflees in panic.\n");
                if ((game.s.unk_19c0 & 8) && !(c->status & CITY_DISORDER) && !disorder_shown) {
                    city_view(city, -2);
                    redraw_map();
                    disorder_shown = 1;
                } else {
                    ui_message(msg, 4);
                }
                if (!(c->status & CITY_DISORDER)) {
                    show_city_again = 1;
                    if (game.s.unk_19c0 & 1)
                        ui_help_message("*DISORDER");
                }
            }
            if (!(c->status & CITY_DISORDER)) {
                c->status |= CITY_DISORDER;
                if (!(game.s.human_mask & (1 << civ)))
                    change_production(civ, city, choose_production(civ, city));
                redraw_if_seen(c->x, c->y);
            } else if (gov == GOV_DEMOCRACY) {
                city_text(msg, sizeof msg, "Discontented citizens of\n", city, " revolt:\nGovernment Collapses!\n");
                if (civ == human) {
                    newspaper(city, msg);
                    game.s.diplomacy[civ][0] |= 4;
                }
                set_government(civ, 0);
            }
        } else {
            if (c->status & CITY_DISORDER) {
                c->status &= ~CITY_DISORDER;
                if (game.s.human_mask & (1 << civ)) {
                    city_text(msg, sizeof msg, "Order restored\nin ", city, ".\n");
                    ui_message(msg, 4);
                } else {
                    change_production(civ, city, choose_production(civ, city));
                }
                redraw_if_seen(c->x, c->y);
            }
            if (gov != GOV_ANARCHY)
                game.s.treasury[civ] += calc.tax;
        }
        if (calc.unhappy == 0 && c->size > 2 && ((int8_t)c->size + 1) / 2 <= calc.happy && gov != GOV_ANARCHY) {
            if (!(c->status & CITY_CELEBRATING)) {
                if ((game.s.human_mask & (1 << civ)) && gov != GOV_ANARCHY) {
                    snprintf(msg, sizeof msg, "'We love the %s'\nday celebrated in\n%s!\n",
                             leader_titles[gov], game.s.city_names[c->name]);
                    if (game.s.unk_19c0 & 8) {
                        city_view(city, -2);
                        redraw_map();
                    } else
                        newspaper(city, msg);
                }
                c->status |= CITY_CELEBRATING;
            } else if (gov >= GOV_REPUBLIC && (int8_t)c->size * 2 + calc.settler_food * calc.settlers < calc.yield[YIELD_FOOD] &&
                       (c->size < 10 || (c->buildings & BIT(B_AQUEDUCT)))) {
                c->size++;                       /* rapture growth */
            }
        } else if (mood >= 0 && (c->status & CITY_CELEBRATING)) {
            if ((game.s.human_mask & (1 << civ)) && gov != GOV_ANARCHY) {
                snprintf(msg, sizeof msg, "'We love the %s'\ncelebration canceled\nin %s.\n",
                         leader_titles[gov], game.s.city_names[c->name]);
                newspaper(city, msg);
            }
            c->status &= ~CITY_CELEBRATING;
        }
    }

    /* Science. */
    sci = calc.science;
    if (civ == human && ((game.s.human_research != -1 && civ_has_advance(civ, game.s.human_research)) ||
                         (game.s.human_research == -1 && game.s.research_progress[civ] != 0))) {
        game.s.human_research = -1;
        if (game.s.rules_flags & 8)
            research_complete(civ, 0);
        game.s.research_progress[civ] = 0;
    }
    if (gov == GOV_ANARCHY)
        sci = 0;
    game.s.research_progress[civ] += sci;
    if (civ == human && game.s.difficulty == 0 && (first_discoverer_at(game.s.human_research) & 7))
        game.s.research_progress[civ] += sci;
    if (civ_has_wonder(civ, WONDER_SETI))
        game.s.research_progress[civ] += sci / 2;
    mult = (civ == human ? game.s.difficulty * 2 + 6 : 14 - game.s.difficulty) + research_bonus;
    if (civ == human && 11 - game.s.advances_known[civ] > mult)
        mult = 11 - game.s.advances_known[civ];
    if ((civ != human || show_city_again == 0) &&
        game.s.research_progress[civ] / (game.s.year < 0 ? 1 : 2) > game.s.advances_known[civ] * mult) {
        game.s.research_progress[civ] = 0;
        if (game.s.rules_flags & 8)
            research_complete(civ, 0);
    }
    game.s.score[civ] += (int8_t)c->size + calc.happy - calc.unhappy;
    if (civ != human)
        return;

    /* Pollution. */
    k = calc.yield[YIELD_SHIELDS] / calc.pollution_divisor - 20 + (int8_t)c->size * calc.pollution_factor / 4;
    if (k * 2 > rnd(256 - game.s.advances_known[civ] * game.s.difficulty / 2)) {
        int sq = rnd(20);
        x = c->x + city_square_dx[sq];
        y = c->y + city_square_dy[sq];
        if (!(map_improvements(x, y) & IMP_POLLUTION) && map_terrain(x, y) != TERRAIN_OCEAN &&
            !(map_improvements(x, y) & IMP_CITY)) {
            add_pollution(x, y);
            view_center(human, x - 8, y - 6);
            square_redraw(x, y);
            city_text(msg, sizeof msg, "Pollution near ", city, ".\n");
            ui_message(msg, 6);
        }
    }
    /* Nuclear plant meltdown. */
    if ((c->status & CITY_DISORDER) && (c->buildings & BIT(B_NUCLEAR_PLANT)) && rnd(3) == 0 &&
        !civ_has_advance(civ, ADV_FUSION_POWER)) {
        view_center(human, c->x - 8, c->y - 6);
        ui_nuclear_explosion(c->x, c->y);
        nuclear_fallout(c->x, c->y);
        c->buildings &= ~BIT(B_NUCLEAR_PLANT);
        city_text(msg, sizeof msg, "Nuclear Catastrophe\nin ", city, "!\nContamination feared!\n");
        newspaper(city, msg);
    }
    /* Upkeep, buildings 2..24 (the Palace is free). */
    for (b = B_BARRACKS; b <= 24; b++) {
        if (!(c->buildings & BIT(b)) || gov == GOV_ANARCHY)
            continue;
        game.s.treasury[civ] -= building_types[b].upkeep;
        if (b == B_BARRACKS) {
            if (civ_has_advance(civ, ADV_GUNPOWDER))
                game.s.treasury[civ]--;
            if (civ_has_advance(civ, ADV_COMBUSTION))
                game.s.treasury[civ]--;
        }
        if (game.s.treasury[civ] < 0) {
            game.s.treasury[civ] = 0;
            c->buildings &= ~BIT(b);
            snprintf(msg, sizeof msg, "%s\ncan't maintain\n%s.\n", game.s.city_names[c->name], building_types[b].name);
            ui_message(msg, 4);
            game.s.treasury[civ] += building_types[b].cost * 10;
        }
    }
    (void)box;
}

/* f_0d07_0043: everything about one city.  mode 0 runs its turn, 1 draws the city screen,
 * -1 only works out the numbers (left in `calc`).  Returns happy - unhappy. */
static int city_process_body(int city, long mode);

/* The city screen (mode 1) draws the city's squares in a view of its own (the city 5 squares
 * from the left, 3 from the top), looking up the units there as it goes (see view.c). */
static void city_screen_lookups(int city)
{
    const City *c = CITY(city);
    int civ = (int8_t)c->owner, sq, x, y, o;

    city_square_lookup(c->x, c->y);
    for (sq = 0; sq < 21; sq++) {
        x = c->x + city_square_dx[sq];
        y = c->y + city_square_dy[sq];
        if (civ == game.s.human_civ && !(game.s.visibility[wrap_x(x)][y] & (1 << civ)))
            continue;
        o = map_unit_owner(x, y);
        if (o != -1 && o != civ) {
            if (!units_hidden)
                unit_at(x, y);
        } else
            city_square_lookup(x, y);
    }
}

int city_process(int city, long mode)
{
    int saved_x = view_x, saved_y = view_y, r;

    if (mode != 1 || CITY(city)->status == NONE8)
        return city_process_body(city, mode);
    view_x = CITY(city)->x - 5;
    view_y = CITY(city)->y - 3;
    city_screen_lookups(city);
    r = city_process_body(city, mode);
    view_x = saved_x;
    view_y = saved_y;
    return r;
}

static int city_process_body(int city, long mode)
{
    City *c = CITY(city);
    uint8_t taken[5][5], blocked[21];
    int i, k, civ, d, size, box, x, y, specialists;

    if (c->status == NONE8)
        return 0;
    memset(taken, 0, sizeof taken);
    calc_city = city;

    /* Squares worked by neighbouring cities; distance to the nearest own Palace. */
    calc.capital_distance = (c->buildings & BIT(B_PALACE)) ? 0 : 32;
    if (c->shields > 999 || c->shields < 0)
        c->shields = 0;
    for (i = 0; i < MAX_CITIES; i++) {
        const City *o = CITY(i);
        if (o->status == NONE8 || i == city)
            continue;
        d = map_distance(c->x, c->y, o->x, o->y);
        if (o->owner == c->owner && (o->buildings & BIT(B_PALACE)) && d < calc.capital_distance)
            calc.capital_distance = d;
        if (d > 5)
            continue;
        for (k = 0; k <= 20; k++) {
            if (!(o->worked & (1UL << k)) && k != 20)
                continue;
            x = o->x + city_square_dx[k];
            y = o->y + city_square_dy[k];
            if (abs(x - c->x) <= 2 && abs(y - c->y) <= 2)
                taken[x - c->x + 2][y - c->y + 2] = 1;
        }
    }
    taken[2][2] = 0;                                  /* the city's own square is never taken */

    civ = (int8_t)c->owner;
    cur_civ = civ;
    calc.settler_food = game.s.government[civ] > 1 ? 2 : 1;
    calc.pollution_factor = 0;
    if (!(c->buildings & BIT(B_MASS_TRANSIT))) {
        calc.pollution_factor += civ_has_advance(civ, ADV_INDUSTRIALIZATION);
        calc.pollution_factor += civ_has_advance(civ, ADV_AUTOMOBILE);
        calc.pollution_factor += civ_has_advance(civ, ADV_MASS_PRODUCTION);
        calc.pollution_factor += civ_has_advance(civ, ADV_PLASTICS);
    }
    box = 10;
    calc.specialists = (int)(c->worked >> 26);
    if (civ != game.s.human_civ) {
        /* Computer players: smaller boxes, and they rethink their specialists. */
        box = 16 - game.s.difficulty * 2;
        if (game.s.year >= 0 && game.s.civ_rank[game.s.human_civ] == 7 && game.s.difficulty > 1)
            box -= 2;
        if (mode == 0) {
            if (c->status & CITY_DISORDER)
                c->worked = 0;
            else
                calc.specialists = 0;
        }
    }
    calc.box = box;
    if (mode == 1) {
        city_unpack_fortified(city);        /* so they show on the city screen */
        memcpy(city_taken, taken, sizeof city_taken);
        ui_city_screen_begin(city);
    }

    if (mode == 0 && city_food(city, box))
        return 0;

    calc.free = (int8_t)c->size + 1;
    city_count_support(city, 1);
    city_find_blocked(city, taken, blocked);
    city_assign_workers(city, mode, blocked);

    /* Unused workers become specialists (entertainers unless set otherwise). */
    specialists = calc.free;
    for (i = 0; i < specialists; i++)
        if (city_specialist(i) == 0)
            city_set_specialist(i, SPEC_ENTERTAINER);
    for (i = specialists; i < 8; i++)
        city_set_specialist(i, 0);

    /* Factory / power: extra shields, and how much they pollute. */
    {
        int factory = 0, power = 0;
        calc.pollution_divisor = 1;
        if (c->buildings & BIT(B_FACTORY))
            factory = 2;
        if (c->buildings & BIT(B_MFG_PLANT))
            factory = 4;
        if (c->buildings & BIT(B_POWER_PLANT))
            power = 2;
        if (c->buildings & BIT(B_HYDRO_PLANT)) {
            calc.pollution_divisor = 2;
            power = 2;
        }
        if (c->buildings & BIT(B_NUCLEAR_PLANT)) {
            calc.pollution_divisor = 2;
            power = 2;
        }
        if (civ_has_wonder(civ, WONDER_HOOVER_DAM)) {
            int w = game.s.wonder_city[WONDER_HOOVER_DAM];
            if (map_continent(CITY(w)->x, CITY(w)->y) == map_continent(c->x, c->y)) {
                calc.pollution_divisor = 2;
                power = 2;
            }
        }
        if (c->buildings & BIT(B_RECYCLING))
            calc.pollution_divisor = 3;
        power = clamp(power, 0, factory);
        calc.yield[YIELD_SHIELDS] += factory * calc.yield[YIELD_SHIELDS] / 4 + power * calc.yield[YIELD_SHIELDS] / 4;
    }

    if (mode == 0 && city_production(city, box))
        return 0;
    if (mode == 1)
        ui_city_screen_resources(city);

    city_trade(city);
    city_happiness(city, specialists, mode);
    city_support(city, mode);
    if (mode == 1)
        ui_city_screen_end(city);
    if (mode == 0)
        city_end_turn(city, box);

    /* The original calls enemy_adjacent(civ, x, y) with only (x, y): the city's x is taken
     * as the civ, its y as x, and the caller's saved SI (city * 28 in the turn loop) as y. */
    if (mode != 1 && (c->status & CITY_PACK_UNITS) && !enemy_adjacent(c->x, c->y, city * 28))
        city_pack_fortified(city);      /* (the city screen does this when it closes) */
    size = calc.happy - calc.unhappy;
    return size;
}

uint8_t city_taken[5][5];
int city_screen_dirty;           /* g_6ae4: the city screen changed the map (redraw it after) */

/* f_0ad5_03e5: the human looks at a city.  The screen is city_process() in mode 1. */
void city_screen(int civ, int city)
{
    (void)civ;
    ui_city_screen(civ, city);
    city_screen_dirty = 0;
    do
        city_process(city, 1);       /* (the original's loop is inside city_process) */
    while (ui_city_screen_again(city));
    if ((CITY(city)->status & CITY_PACK_UNITS) && !enemy_adjacent(CITY(city)->x, CITY(city)->y, city * 28))
        city_pack_fortified(city);
    if (city_screen_dirty)
        redraw_map();
}
