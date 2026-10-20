/* What a city should build next (f_0ad5_041c): the human's menu and both sets of advice,
 * and the computer players' choice. */
#include <stdio.h>
#include <string.h>
#include "game.h"
#include "civ.h"
#include "city.h"
#include "map.h"
#include "unit.h"
#include "util.h"
#include "ai.h"
#include "ui.h"

#define MAX_CHOICES 72
#define CHOICE_MORE 99
#define CHOICE_BACK 98

static void append(char *buf, size_t size, const char *s)
{
    size_t n = strlen(buf);

    snprintf(buf + n, size - n, "%s", s);
}

/* f_0ad5_14d2: " (n turns" to build `cost` (x10 shields) here; the caller closes it. */
static void append_turns(char *buf, size_t size, int city, int cost)
{
    int surplus = clamp(calc.yield[YIELD_SHIELDS] - calc.shield_upkeep, 1, 99);
    int need = clamp((cost * 10 - game.s.cities[city].shields - 1) / surplus + 1, 1, 999);
    char tmp[24];

    snprintf(tmp, sizeof tmp, " (%d turn%s", need, need == 1 ? "" : "s");
    append(buf, size, tmp);
}

/* Score one building for the advisors and the AI; lower is better, 99 = not wanted. */
static int building_score(int civ, int city, int b, int food_surplus, int *military)
{
    const City *c = &game.s.cities[city];
    int score = 99;

    *military = 0;
    switch (b) {
    case -27:                                         /* (the switch also covers Caravan */
    case -26:                                         /*  and Diplomat; see below) */
        break;
    case B_BARRACKS:
        score = 5;
        *military = 1;
        break;
    case B_GRANARY:
        score = calc.yield[YIELD_SHIELDS] < 3 ? 8 : 4;
        break;
    case B_TEMPLE:
        if (c->status & CITY_DISORDER)
            score = -4;
        break;
    case B_MARKETPLACE:
        score = 10 - calc.yield[YIELD_TRADE] / 2;
        if (c->status & CITY_DISORDER)
            score = -3;
        break;
    case B_LIBRARY:
    case B_BANK:
        score = 10 - calc.yield[YIELD_TRADE] / 3;
        break;
    case B_COURTHOUSE:
        score = 12 - calc.corruption * 2;
        break;
    case B_CITY_WALLS:
        score = 10 - (int8_t)c->size / 2;
        if (c->buildings & BIT(B_PALACE))
            score -= 4;
        *military = 1;
        break;
    case B_AQUEDUCT:
        if ((int8_t)c->size >= 10 - food_surplus / 2)
            score = 14 - (int8_t)c->size - food_surplus / 2;
        break;
    case B_CATHEDRAL:
        if (c->status & CITY_DISORDER)
            score = -2;
        break;
    case B_UNIVERSITY:
        score = 10 - calc.yield[YIELD_TRADE] / 4;
        break;
    case B_COLOSSEUM:
        if (c->status & CITY_DISORDER)
            score = -1;
        break;
    case B_FACTORY:
    case B_HYDRO_PLANT:
        score = 13 - calc.yield[YIELD_SHIELDS] / 2;
        break;
    case B_MFG_PLANT:
    case B_RECYCLING:
    case B_NUCLEAR_PLANT:
        score = 12 - calc.yield[YIELD_SHIELDS] / 8;
        break;
    case B_POWER_PLANT:
        score = 12 - calc.yield[YIELD_SHIELDS] / 5;
        break;
    case B_SS_STRUCTURAL:
        if (!(game.s.unk_4c88 & ((1 << civ) + (1 << game.s.human_civ))))
            score = (game.s.unk_4c88 & (1 << (game.s.human_civ + 8))) ? 2 : 4;
        break;
    default:
        break;
    }
    return score;
}

int choose_production(int civ, int city)
{
    City *c = &game.s.cities[city];
    int human = game.s.human_civ;
    int choices[MAX_CHOICES], n, n_units, text_units = 0;
    int page = 0, advised = 0;
    int i, k, v, b, t, cont, strategy, wanted, hoover, food_surplus, saved_count, defend;
    int here[NUM_UNIT_TYPES], on_cont[NUM_UNIT_TYPES];
    int best = 0, best_score, best_unit = -1, best_unit_score, best_bld = -1, best_bld_score;
    int score, military, mult, f;
    char text[2048];

    city_process(city, -1);
restart:
    n = 1;
    choices[0] = 0;
    snprintf(text, sizeof text, "What shall we build in %s?\n ", game.s.city_names[c->name]);
    if (page != 2) {
        for (i = 0; i < NUM_UNIT_TYPES; i++) {
            const UnitType *u = &game.s.unit_types[i];
            char tmp[64];
            if (i == UNIT_NUCLEAR && (game.s.wonder_city[WONDER_MANHATTAN] == -1 || !civ_has_advance(civ, 27)))
                continue;
            if (!civ_has_advance(civ, u->prereq) || civ_has_advance(civ, u->obsolete_by))
                continue;
            if (!(c->status & CITY_COASTAL) && u->domain == DOMAIN_SEA)
                continue;
            append(text, sizeof text, game.s.unit_type_names[i]);
            append_turns(text, sizeof text, city, u->cost);
            snprintf(tmp, sizeof tmp, ", ADM:%d/%d/%d)\n ", u->attack, u->defense, u->moves);
            append(text, sizeof text, tmp);
            choices[n++] = i;
        }
    }
    n_units = n;
    text_units = (int)strlen(text);

    hoover = 0;
    if (civ_has_wonder(civ, WONDER_HOOVER_DAM)) {
        const City *w = &game.s.cities[game.s.wonder_city[WONDER_HOOVER_DAM]];
        if (map_continent(w->x, w->y) == map_continent(c->x, c->y))
            hoover = 1;
    }
    if (game.s.rules_flags & RULE_GROWTH_LIMITS) {
        for (b = 1; b <= 24; b++) {
            if (!civ_has_advance(civ, building_types[b].prereq) || (c->buildings & BIT(b)))
                continue;
            if (b == B_COURTHOUSE && (c->buildings & BIT(B_PALACE)))
                continue;
            if (b == B_FACTORY && (c->buildings & BIT(B_MFG_PLANT)))
                continue;
            if (b == B_BANK && !(c->buildings & BIT(B_MARKETPLACE)) && civ_has_advance(civ, building_types[B_MARKETPLACE].prereq))
                continue;
            if (b == B_UNIVERSITY && !(c->buildings & BIT(B_LIBRARY)) && civ_has_advance(civ, building_types[B_LIBRARY].prereq))
                continue;
            if (b == B_HYDRO_PLANT && !(c->status & CITY_HYDRO_OK))
                continue;
            if (b == B_POWER_PLANT && !(c->buildings & BIT(B_FACTORY)))
                continue;
            if ((b == B_POWER_PLANT || b == B_HYDRO_PLANT || b == B_NUCLEAR_PLANT) &&
                (hoover || (c->buildings & (BIT(B_POWER_PLANT) | BIT(B_HYDRO_PLANT) | BIT(B_NUCLEAR_PLANT)))))
                continue;
            if (b > B_NUCLEAR_PLANT &&
                (game.s.wonder_city[WONDER_APOLLO] == -1 || (game.s.unk_4c88 & (1 << civ))))
                continue;
            append(text, sizeof text, building_types[b].name);
            append_turns(text, sizeof text, city, building_types[b].cost);
            append(text, sizeof text, ")\n ");
            choices[n++] = -b;
        }
    }
    for (b = FIRST_WONDER; b < NUM_BUILDINGS; b++) {
        int w = b - FIRST_WONDER + 1, obs = wonder_obsolete[w];
        if (!civ_has_advance(civ, building_types[b].prereq) || game.s.wonder_city[w] != -1)
            continue;
        if (obs != ADVANCE_NEVER && (game.s.first_discoverer[obs] & 7))
            append(text, sizeof text, "*");
        append(text, sizeof text, building_types[b].name);
        append_turns(text, sizeof text, city, building_types[b].cost);
        append(text, sizeof text, ")\n ");
        choices[n++] = -b;
    }
    if (civ == human && !(c->status & CITY_AUTOBUILD) && n > 30) {
        if (page == 0) {
            page = 1;
            goto restart;
        }
        text[text_units] = 0;
        append(text, sizeof text, "more...\n");
        choices[n_units] = CHOICE_MORE;
    }
    if (page == 2) {
        append(text, sizeof text, "more...\n");
        choices[n++] = CHOICE_BACK;
    }

    /* The advisors' (and the computer's) view. */
    cont = map_continent(c->x, c->y);
    strategy = game.s.unk_f5b8[civ][cont];
    wanted = strategy == 1 || strategy == 2;
    if (ai_flag_8f98 && strategy == 5)
        wanted = 1;
    food_surplus = calc.yield[YIELD_FOOD] - (game.s.government[civ] > 1 ? 2 : 1) * calc.settlers - (int8_t)c->size * 2;
    for (t = 0; t < NUM_UNIT_TYPES; t++) {
        here[t] = 1;
        on_cont[t] = 0;
    }
    saved_count = game.s.unit_count[civ];
    game.s.unit_count[civ] = 0;
    for (i = 0; i < MAX_CITIES; i++) {
        const City *o = &game.s.cities[i];
        if (o->status == NONE8 || (int8_t)o->owner != civ || map_continent(o->x, o->y) != cont)
            continue;
        if (o->producing >= 0 && i != city)
            here[o->producing]++;
        for (k = 0; k < 2; k++)
            if (o->fortified[k] != NONE8) {
                game.s.unit_count[civ]++;
                on_cont[o->fortified[k] & 0x3F]++;
            }
    }
    for (i = 0; i < MAX_UNITS; i++) {
        const Unit *u = &game.s.units[civ][i];
        if (u->type == NONE8)
            continue;
        game.s.unit_count[civ]++;
        if (map_continent(u->x, u->y) == cont)
            on_cont[u->type]++;
    }
    /* (These two early returns leave unit_count[] as the temporary count, as the original does.) */
    if (game.s.unit_count[civ] == 0 && civ != human && game.s.government[civ] <= 1)
        return 1;                                            /* Militia */
    if (civ == 0)
        return (game.s.first_discoverer[56] & 7) ? 7 : 3;   /* barbarians: Knights / Legion */

    defend = 0;
    t = unit_at(c->x, c->y);
    if (t == -1)
        defend = 2;
    else if (stack_count_role(civ, t, 2) < (int8_t)c->size / 4 + wanted)
        defend = 1;

    best_score = best_unit_score = best_bld_score = 999;
    for (k = 1; k < n; k++) {
        v = choices[k];
        if (v < 0 || v >= UNIT_DIPLOMAT) {
            score = 99;
            military = 0;
            if (v == UNIT_CARAVAN) {
                if (c->trade_routes[2] == NONE8) {
                    score = 10 - calc.yield[YIELD_TRADE] / 2;
                    if (c->trade_routes[0] != NONE8)
                        score += 2;
                    if (c->trade_routes[1] != NONE8)
                        score += 2;
                    if (score < 3)
                        score = 3;
                }
                if (civ == human && (c->status & CITY_AUTOBUILD))
                    score = 99;
            } else if (v == UNIT_DIPLOMAT) {
                if (human > 0 && !(game.s.diplomacy[civ][human] & DIP_PEACE) &&
                    game.s.units_in_production[civ][UNIT_DIPLOMAT] <= 0) {
                    int w = rnd(5);
                    if ((ai_diplomat_civs & (1 << civ)) &&
                        (game.s.advances[human][w] | game.s.advances[civ][w]) != game.s.advances[civ][w])
                        score = clamp(game.s.advances_known[civ] - game.s.advances_known[human] + 10, 5, 10);
                }
            } else if (v <= 0 && v >= -22) {
                score = building_score(civ, city, -v, food_surplus, &military);
            }
            mult = wanted == military ? 10 : 20;
            score *= mult;
            score = score * 3 / (civ_types[game.s.civ_type[civ]].science_bias + 3);
            if (c->buildings & BIT(B_BARRACKS))
                score += score / 4;
        } else {
            const UnitType *u = &game.s.unit_types[v];
            if (v == UNIT_CARRIER)
                continue;
            if (v == UNIT_SETTLERS && (game.s.government[civ] > 1 ? 2 : 1) >= food_surplus &&
                (game.s.units_active[civ][UNIT_SETTLERS] != 0 || here[UNIT_SETTLERS] > 1))
                continue;
            if (v == 1 && game.s.government[civ] > 1)
                continue;
            if (v == UNIT_NUCLEAR && (game.s.units_active[civ][UNIT_NUCLEAR] >= 4 ||
                                      game.s.units_in_production[civ][UNIT_NUCLEAR] >= 2))
                continue;
            if (v == 14 && (game.s.units_active[civ][14] + game.s.units_in_production[civ][14] >=
                            game.s.units_active[human][15] / 2 ||
                            (game.s.diplomacy[civ][human] & DIP_PEACE)))
                continue;
            if (u->domain == DOMAIN_LAND) {
                score = here[v] * 2 + on_cont[v];
            } else {
                if (game.s.unk_6c2c[civ][cont] < 8)
                    continue;
                score = game.s.unk_6c2c[civ][cont] *
                        (game.s.units_in_production[civ][v] * 2 + game.s.units_active[civ][v]) /
                        (game.s.unit_count[civ] + 1);
            }
            if (defend) {
                if (u->role != 2)
                    continue;
                score = 2 / defend;
            }
            if (u->role != 2 && u->role != 0) {
                score <<= 1;
                if (strategy != u->role)
                    score <<= 1;
            }
            if (u->role == 5) {
                if (strategy != 5)
                    continue;
                score = score * 3 / 2;
            }
            if (v == UNIT_SETTLERS)
                f = ((c->buildings & BIT(B_GRANARY)) ? 4 : 6) - civ_types[game.s.civ_type[civ]].science_bias;
            else
                f = 16;
            /* 16-bit arithmetic, as in the original */
            score = (int16_t)((int16_t)(f * (u->cost + 2)) * score) /
                    (int16_t)((u->attack + u->defense) * (u->moves + 1));
            if (u->role == 3)
                score *= wanted ? 4 : 2;
            if (u->domain == DOMAIN_AIR)
                score <<= 1;
            if (game.s.unit_count[civ] > 120)
                score <<= 2;
            if ((!wanted && calc.yield[YIELD_SHIELDS] <= calc.support) || !ai_at_war)
                score <<= 1;
        }
        if (score < best_score) {
            best = k;
            best_score = score;
        }
        if (v < 0 || v >= UNIT_DIPLOMAT) {
            if (score < best_bld_score) {
                best_bld_score = score;
                best_bld = k;
            }
        } else if (score < best_unit_score) {
            best_unit_score = score;
            best_unit = k;
        }
    }
    game.s.unit_count[civ] = saved_count;

    if (!(game.s.human_mask & (1 << civ))) {
        if (c->producing == -1)
            return -1;
        return choices[best];
    }
    if (c->status & CITY_AUTOBUILD)
        return best_bld != -1 && best_bld_score <= 500 ? choices[best_bld] : CHOICE_MORE;
    if ((game.s.unk_19c0 & 1) && !advised) {
        ui_production_advice(city, best_unit == -1 ? -999 : choices[best_unit],
                             best_bld == -1 ? -999 : choices[best_bld]);
        advised = 1;
        goto restart;
    }
    k = ui_choose(text, n > 20 || page != 0, -1, -1, -1, 0);
    v = choices[k + 1];
    if (v == CHOICE_MORE) {
        page = 2;
        goto restart;
    }
    if (v == CHOICE_BACK) {
        page = 1;
        goto restart;
    }
    if (ui_help_requested()) {
        ui_encyclopedia(v < 0 ? -v : 2);
        goto restart;
    }
    return v;
}
