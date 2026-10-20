/* A computer player's plan for the turn (original seg 15da: ai_strategy, ai_strategy2) and
 * changes of government (f_14f6_04a9). */
#include <stdio.h>
#include <string.h>
#include "game.h"
#include "civ.h"
#include "city.h"
#include "map.h"
#include "unit.h"
#include "util.h"
#include "ai.h"
#include "replay.h"
#include "ui.h"
#include "view.h"

#define DIP(a, b) game.s.diplomacy[a][b]
#define CONT(arr, civ, cont) (((int16_t *)(arr))[(civ) * 16 + (cont)])
#define BLOCK(x, y) ai_block_flags[((x) >> 2) * 13 + ((y) >> 2)]

/* f_14f6_04a9: `civ` adopts government `gov`.  The human falls into anarchy with 0 and is
 * asked to choose otherwise. */
void set_government(int civ, int gov)
{
    SaveData *s = &game.s;
    int old = s->government[civ], i, n, choice[6], pick;
    char msg[256];

    if (s->human_mask & (1 << civ)) {
        for (;;) {
            snprintf(msg, sizeof msg, "Select type of\nGovernment...\n ");
            for (n = 0, i = 1; i < 6; i++) {
                if (!civ_has_wonder(civ, WONDER_PYRAMIDS)) {
                    if (i == GOV_MONARCHY && !civ_has_advance(civ, ADV_MONARCHY))
                        continue;
                    if (i == GOV_COMMUNISM && !civ_has_advance(civ, 43))
                        continue;
                    if (i == GOV_REPUBLIC && !civ_has_advance(civ, 44))
                        continue;
                    if (i == GOV_DEMOCRACY && !civ_has_advance(civ, 4))
                        continue;
                }
                choice[n++] = i;
                strncat(msg, government_names[i], sizeof msg - strlen(msg) - 1);
                strncat(msg, "\n ", sizeof msg - strlen(msg) - 1);
            }
            if (gov == 0) {
                pick = 0;
                break;
            }
            i = ui_choose(msg, 0, 100, 64, -1, 0);
            if (i >= 0) {
                pick = choice[i];
                break;
            }
        }
        s->government[civ] = (int16_t)pick;
        ui_government_picture();                     /* o_05_1adf */
        redraw_map();
        if (pick != GOV_ANARCHY) {                   /* o_05_1b8b: the news, the new cabinet */
            snprintf(msg, sizeof msg, "%s government\nchanged to %s!\n", s->civ_adjectives[civ],
                     government_names[pick]);
            newspaper(-2, msg);
            ui_new_government(civ);
        }
    } else {
        if (!(s->government[civ] == gov && !(DIP(civ, 0) & 4)) && (DIP(game.s.human_civ, civ) & DIP_EMBASSY)) {
            /* The bit 4 of the civ's own "barbarian" entry marks a revolution in progress. */
            if (DIP(civ, 0) & 4) {
                snprintf(msg, sizeof msg, "%s government\nchanged to %s!\n", s->civ_adjectives[civ],
                         government_names[gov]);
                DIP(civ, 0) &= ~4;
            } else {
                snprintf(msg, sizeof msg, "The %s government\nhas been overthrown!\n", s->civ_adjectives[civ]);
                DIP(civ, 0) |= 4;
            }
            ui_message(msg, 1);
        }
        s->government[civ] = (int16_t)gov;
        s->tax_rate[civ] = (int16_t)(4 - gov / 2);
    }
    if (old != s->government[civ]) {
        replay_log(8, 2, civ, s->government[civ], 0, 0);
        for (i = 0; i < MAX_CIVS; i++)
            *(uint8_t *)&DIP(civ, i) &= ~8;
    }
}

/* ai_strategy2 (f_15da_3452): this turn's goals start from the remembered spots. */
void ai_reset_goals(int civ)
{
    AiGoals *g = &ai_goals[civ];
    int i;

    for (i = 0; i < AI_GOALS; i++) {
        g->type[i] = NONE8;
        g->prio[i] = 0;
    }
    for (i = 0; i < 16; i++) {
        if ((uint8_t)game.s.ai_spot_type[civ][i] == NONE8 || (uint8_t)game.s.ai_spot_type[civ][i] > 2)
            continue;
        ai_add_goal(civ, game.s.ai_spot_x[civ][i], game.s.ai_spot_y[civ][i], game.s.ai_spot_type[civ][i],
                    (int8_t)game.s.ai_spot_value[civ][i]);
    }
}

/* ai_strategy (f_15da_000c): strength per continent, a policy for each continent,
 * government, disbanding, and goals for idle units. */
void ai_strategy(int civ)
{
    SaveData *s = &game.s;
    AiGoals *g = &ai_goals[civ];
    int i, k, u, c, o, cont, scale, gov, taken[AI_GOALS];

    for (i = 0; i < 16; i++) {
        CONT(s->unk_6b16, civ, i) = 0;
        CONT(s->unk_8f9a, civ, i) = 0;
        CONT(s->unk_6c2c, civ, i) = 0;
    }
    memset(ai_block_flags, 0, sizeof ai_block_flags);
    s->military_power[civ] = 0;
    s->settler_count[civ] = 0;
    s->unit_count[civ] = 0;

    for (u = 0; u < MAX_UNITS; u++) {
        Unit *p = unit_ptr(civ, u);
        const UnitType *t;
        if (p->type == NONE8 || p->type == UNIT_NUCLEAR)
            continue;
        if (p->type == UNIT_SETTLERS)
            s->settler_count[civ]++;
        else
            s->unit_count[civ]++;
        t = utype(p->type);
        s->military_power[civ] += t->attack + t->defense;
        p->status &= ~UNIT_GOTO;
        if (enemy_in_zone(civ, p->x, p->y))
            p->status |= UNIT_GOTO;                  /* (busy: no new goal) */
        if (map_terrain(p->x, p->y) != TERRAIN_OCEAN) {
            cont = map_continent(p->x, p->y);
            CONT(s->unk_6c2c, civ, cont) += t->defense;
            CONT(s->unk_8f9a, civ, cont) += t->attack;
        } else if (t->domain == DOMAIN_LAND)
            p->status |= UNIT_GOTO;
        BLOCK(p->x, p->y) |= (uint8_t)(2 << t->role);
    }
    scale = clamp(s->unit_count[civ] / 8, 3, 99);

    s->citizen_count[civ] = 0;
    s->city_count[civ] = 0;
    for (i = 0; i < NUM_UNIT_TYPES; i++)
        s->units_in_production[civ][i] = 0;
    for (c = 0; c < MAX_CITIES; c++) {
        City *cy = &s->cities[c];
        o = (int8_t)cy->owner;
        if (cy->status == NONE8)
            continue;
        if (o == civ) {
            s->city_count[civ]++;
            CONT(s->unk_6b16, civ, map_continent(cy->x, cy->y))++;
            s->citizen_count[civ] += (int8_t)cy->size;
            if (cy->producing >= 0)
                s->units_in_production[civ][cy->producing]++;
            BLOCK(cy->x, cy->y) |= 1;
            for (k = 0; k < 2; k++) {
                int t;
                if (cy->fortified[k] == NONE8)
                    continue;
                t = cy->fortified[k] & 63;
                s->military_power[civ] += utype(t)->attack + utype(t)->defense;
                cont = map_continent(cy->x, cy->y);
                CONT(s->unk_6c2c, civ, cont) += utype(t)->defense;
                CONT(s->unk_8f9a, civ, cont) += utype(t)->attack;
            }
        } else if (!(s->turn * s->difficulty <= 200 && !((1 << civ) & (int8_t)s->visibility[cy->x][cy->y]) &&
                     game.s.human_civ == o) &&
                   (DIP(civ, o) & 0x102) != 2 && ((c + s->turn) & 3) != 0) {
            /* Someone else's city: a target, harder behind walls. */
            ai_add_goal(civ, cy->x, cy->y, 1, (cy->buildings & BIT(B_CITY_WALLS)) ? 3 : 5);
        }
        if (unit_at(cy->x, cy->y) == -1 && cy->fortified[0] == NONE8)
            ai_add_goal(civ, cy->x, cy->y, 2, o == civ ? 4 : 2);
    }

    s->land_count[civ] = 0;
    for (i = 0; i < 260; i++)
        if (ai_block_flags[i])
            s->land_count[civ]++;

    CONT(s->unk_f5b8, civ, 0) = 0;
    CONT(s->unk_f5b8, civ, 15) = 0;
    ai_diplomat_civs &= ~(1 << civ);
    ai_at_war = 0;
    for (o = 1; o < MAX_CIVS; o++)
        if ((DIP(civ, o) & 3) == 1)
            ai_at_war = 1;

    for (cont = 1; cont < 15; cont++) {
        int threat = 0, strong = 0, contact = 0, cities = 0, defence = 0, old = CONT(s->unk_f5b8, civ, cont);
        int16_t *pol = &CONT(s->unk_f5b8, civ, cont);
        int my_att = CONT(s->unk_8f9a, civ, cont), my_def = CONT(s->unk_6c2c, civ, cont);
        for (o = 1; o < MAX_CIVS; o++) {
            int their_def = CONT(s->unk_6c2c, o, cont), their_att = CONT(s->unk_8f9a, o, cont);
            if (their_def != 0 && (my_att / 4 < their_att || CONT(s->unk_6b16, o, cont) != 0) &&
                ((DIP(civ, o) & 3) == 1 || (DIP(civ, o) & 0x100))) {
                if (their_def < my_att || their_att < my_def || CONT(s->unk_6b16, civ, cont) == 0)
                    threat++;
                else
                    strong++;
            }
            if (their_def != 0 && o == game.s.human_civ && (DIP(civ, o) & DIP_PEACE) &&
                my_att / 2 + my_def < their_att)
                strong++;
            defence += their_def;
            cities += CONT(s->unk_6b16, o, cont);
            if (their_def != 0)
                contact |= (DIP(civ, o) & DIP_PEACE) ? 1 : 2;
        }
        if (CONT(s->unk_6b16, 0, cont) != 0)
            threat++;
        if (((my_def + defence) * 2 > s->land_area[cont] ||
             (CONT(s->unk_6b16, civ, cont) + cities) * 6 + 2 > s->unk_c10e[cont]) &&
            civ_has_advance(civ, 7))                   /* Map Making: settle elsewhere */
            *pol = 5;
        else
            *pol = 0;
        if (threat)
            *pol = 1;
        if (strong)
            *pol = 2;
        if (my_def == 0 && CONT(s->unk_6b16, civ, cont) == 0 && contact != 1)
            *pol = 1;
        if (*pol != old && !(s->human_mask & (1 << civ)))
            ai_rechoose_production(civ, cont);
        if (my_def != 0 && CONT(s->unk_6b16, game.s.human_civ, cont) > 1)
            ai_diplomat_civs |= 1 << civ;
    }

    if (civ == 0 || (s->human_mask & (1 << civ)))
        return;

    if (((civ + s->turn) & 7) == 0) {
        gov = 0;
        if (ai_support_score > 0)
            gov = GOV_DESPOTISM;
        else if (civ_has_advance(civ, 44) && ai_unhappy_score >= 0)
            gov = GOV_REPUBLIC;
        else if (civ_has_advance(civ, 43) && s->city_count[civ] > 10)
            gov = GOV_COMMUNISM;
        else if (civ_has_advance(civ, ADV_MONARCHY))
            gov = GOV_MONARCHY;
        if (gov)
            set_government(civ, gov);
    }

    /* Idle units: disband the obsolete, send the rest towards goals. */
    for (i = 0; i < AI_GOALS; i++)
        taken[i] = scale;
    for (u = 0; u < MAX_UNITS; u++) {
        Unit *p = unit_ptr(civ, u);
        int best = 9999, pick = -1, x, y, d;
        if (p->type == NONE8 || (p->status & UNIT_GOTO) || p->type == UNIT_NUCLEAR)
            continue;
        x = p->x;
        y = p->y;
        cont = map_continent(x, y);
        if (((u + s->turn) & 15) == 0 &&
            (civ_has_advance(civ, utype(p->type)->obsolete_by) ||
             ((s->government[civ] > 1 || CONT(s->unk_f5b8, civ, cont) == 5) && p->type == 1)) &&
            !enemy_in_zone(civ, x, y) && (!(map_improvements(x, y) & IMP_CITY) || p->next != NONE8) &&
            map_terrain(x, y) != TERRAIN_OCEAN && CONT(s->unk_f5b8, civ, cont) != 2) {
            delete_unit(civ, u);
            continue;
        }
        for (i = 0; i < AI_GOALS; i++) {
            int prio = g->prio[i];
            if (g->type[i] == NONE8)
                continue;
            if (!(utype(p->type)->role == (int8_t)g->type[i] && map_continent(g->x[i], g->y[i]) == cont)) {
                if (p->type != 15 || !(utype(15)->role == (int8_t)g->type[i] || (int8_t)g->type[i] == 3))
                    continue;
            }
            if (p->type == 15) {                       /* bombers: odd priorities, alternate turns */
                if (!(g->prio[i] & 1) || !((i + (s->turn >> 1)) & 1))
                    continue;
                d = map_distance(x, y, g->x[i], g->y[i]);
                d = (int16_t)((utype(p->type)->moves >= d ? 2 : 4) * d) / (prio + 1);
            } else {
                d = map_distance(x, y, g->x[i], g->y[i]);
                d = (int16_t)(taken[i] * d) / (prio + 1);
            }
            if (p->status & (UNIT_FORTIFYING | UNIT_FORTIFIED)) {
                /* Fortified units only leave for something important. */
                if (prio < 2)
                    continue;
                if (!(prio * scale >= d || taken[i] == scale))
                    continue;
                if (map_improvements(p->x, p->y) & IMP_CITY)
                    continue;
            }
            if (d >= best)
                continue;
            if (prio * 4 >= d / scale) {
                best = d;
                pick = i;
            }
        }
        if (pick == -1)
            continue;
        p->goto_x = (uint8_t)g->x[pick];
        p->goto_y = (uint8_t)g->y[pick];
        p->status = (uint8_t)((p->status | UNIT_GOTO) & 0xF0);
        taken[pick]++;
    }
}
