#include <stdio.h>
#include "game.h"
#include "map.h"
#include "civ.h"
#include "unit.h"
#include "util.h"
#include "ai.h"
#include "replay.h"
#include "science.h"
#include "ui.h"

/* f_0ad5_2293 */
int civ_has_advance(int civ, int advance)
{
    if (advance == ADVANCE_NONE)
        return 1;
    if (advance >= NUM_ADVANCES - 1 || civ == 0)
        return 0;
    return (game.s.advances[civ][advance >> 4] >> (advance % 16)) & 1;
}

/* f_0d07_6b27: a wonder stops working for everybody once any civ learns its obsoleting advance. */
int wonder_active_city(int wonder)
{
    int c;

    for (c = 1; c < MAX_CIVS; c++)
        if (civ_has_advance(c, wonder_obsolete[wonder]))
            return -1;
    return game.s.wonder_city[wonder];
}

/* f_0d07_6acb */
int civ_has_wonder(int civ, int wonder)
{
    int city = wonder_active_city(wonder);

    return city != -1 && (int8_t)game.s.cities[city].owner == civ;
}

int civ_unk_e12c[MAX_CIVS];   /* g_e12c */
int earth_map;              /* g_e68a: playing on the Earth map */
int quit_flag;              /* g_eb68 */
int game_won_conquest;      /* g_c7a4 */

static void name_civ(int civ)
{
    const CivType *t = &civ_types[game.s.civ_type[civ]];
    SaveData *s = &game.s;

    snprintf(s->leader_names[civ], sizeof s->leader_names[civ], "%s", t->leader);
    if (civ != s->human_civ || s->civ_adjectives[civ][0] == 0) {
        snprintf(s->civ_adjectives[civ], sizeof s->civ_adjectives[civ], "%s", t->adjective);
        if (t->plural[0])
            snprintf(s->civ_names[civ], sizeof s->civ_names[civ], "%s", t->plural);
        else
            snprintf(s->civ_names[civ], sizeof s->civ_names[civ], "%ss", t->adjective);
    }
}

/* o_05_07c2: (re)start civilization `civ`: everything reset and, unless it is the
 * game's first turn, it reappears somewhere new as the alternate civ of its colour. */
int civ_restart(int civ)
{
    SaveData *s = &game.s;
    int human = s->human_civ, i, k, x = 0, y = 0, tries, d, best, cont, ok;
    int on_cont[16];

    if (civ == human && s->turn != 0)
        return human;                    /* (whatever AX held: the human's number) */
    s->unk_ee0c[civ] = 0;
    s->unk_6c1c[civ] = 0;
    s->advances_known[civ] = 0;
    s->research_progress[civ] = 0;
    s->treasury[civ] = 0;
    s->government[civ] = GOV_DESPOTISM;
    s->tax_rate[civ] = 4;
    s->science_rate[civ] = 4;
    for (cont = 0; cont < 16; cont++) {
        s->unk_f5b8[civ][cont] = 0;
        on_cont[cont] = 0;
        for (k = 1; k < MAX_CIVS; k++)
            on_cont[cont] += s->unk_6b16[k][cont];
    }
    s->city_count[civ] = 0;
    s->unit_count[civ] = 0;
    s->land_count[civ] = 1;
    s->settler_count[civ] = 1;
    s->unk_e204[civ] = -1;
    for (i = 0; i < NUM_ADVANCES; i++)
        s->advance_source[civ][i] = NONE8;
    for (i = 0; i < MAX_UNITS; i++) {
        s->units[civ][i].type = NONE8;
        s->units[civ][i].status = 0;
    }
    civ_unk_e12c[civ] = 0;
    s->score[civ] = 0;
    if (civ == human)
        for (y = 0; y < MAP_H; y++)
            for (x = 0; x < MAP_W; x++)
                map_put_pixel(x + 240, y, 0);           /* the human's minimap */
    ai_clear_continent_goals(civ);
    for (k = 0; k < MAX_CIVS; k++) {
        s->diplomacy[civ][k] = 0;
        s->diplomacy[k][civ] = 0;
    }
    for (k = 0; k < 5; k++)
        s->advances[civ][k] = 0;
    if (s->turn != 0) {
        /* A newcomer gets some of what the human knows (not what the human found first). */
        for (i = 0; i < NUM_ADVANCES; i++)
            if (civ_has_advance(human, i) && (s->first_discoverer[i] & 7) &&
                (s->first_discoverer[i] & 7) != human && rnd(2) == 0)
                give_advance(civ, i, civ);
    }
    if (civ == 0 || !(s->civs_alive & (1 << civ)))
        goto clear_counts;

    if (civ != human) {
        if (s->turn == 0) {
            /* Pick a civilization of this colour that nobody else plays. */
            do {
                s->civ_type[civ] = (int16_t)rnd(16);
                ok = (s->civ_type[civ] & 7) == civ;
                for (k = 0; k < MAX_CIVS; k++)
                    if (k != civ && s->civ_type[k] == s->civ_type[civ])
                        ok = 0;
            } while (!ok);
        } else {
            s->civ_type[civ] ^= 8;
        }
    }

    tries = 0;
    for (;;) {
        x = rnd(64) + 8;
        y = rnd(34) + 8;
        best = nearest_city(x, y, &d);
        d = best != -1 ? d : 30;
        if (s->turn == 0)
            for (k = 1; k < civ; k++) {
                int dd = map_distance(x, y, s->units[k][0].x, s->units[k][0].y);
                if (dd < d)
                    d = dd;
            }
        k = map_pixel(x + 80, y);                        /* land value */
        tries++;
        if (tries > 2000 && (civ != human || s->turn != 0))
            break;
        if (map_terrain(x, y) == TERRAIN_OCEAN)
            continue;
        if (12 - tries / 32 > k)
            continue;
        if (d < 10 - tries / 64)
            continue;
        cont = map_continent(x, y);
        if (s->unk_c10e[cont] < 32 - (s->turn / 16 + tries / 64))
            continue;
        if (s->year > 0 && on_cont[cont] != 0)
            continue;
        if (has_hut(map_terrain(x, y), x, y))
            continue;
        break;
    }
    if (s->turn == 0 && earth_map) {
        x = earth_start[s->civ_type[civ]][0];
        y = earth_start[s->civ_type[civ]][1];
        tries = 0;
    }
    if (s->turn != 0) {
        if (s->civ_type[civ] >= 8 && (s->unk_ee3e & (1 << civ)))
            tries = 2000;
        if (s->civ_type[civ] < 8 && !(s->unk_ee3e & (1 << civ)))
            tries = 2000;
    }
    if (civ != human) {
        s->science_rate[civ] = civ_types[s->civ_type[civ]].science_bias + 3;
        s->tax_rate[civ] = 9 - s->science_rate[civ];
    }
    if ((350 - s->difficulty * 50 < s->turn || tries >= 2000) && civ != human) {
        /* Too late, or no room: this colour is out of the game. */
        s->civ_type[civ] ^= 8;
        s->civs_alive &= ~(1 << civ);
        if (((1 << human) | 1) == s->civs_alive) {
            quit_flag = 1;
            game_won_conquest = 1;
        }
        return 0;
    }
    map_set_unit_layer_owner(civ, x, y);
    create_unit(civ, UNIT_SETTLERS, x, y);
    s->capital_x[civ] = (int16_t)x;
    for (i = 0; i < MAX_UNITS; i++)
        if (s->units[0][i].type != NONE8 && map_distance(x, y, s->units[0][i].x, s->units[0][i].y) <= 8)
            delete_unit(0, i);                              /* barbarians make room */
    name_civ(civ);
    s->advances_known[civ] = 1;
clear_counts:
    for (i = 0; i < NUM_UNIT_TYPES; i++) {
        s->units_lost[civ][i] = 0;
        s->units_active[civ][i] = 0;
        s->units_in_production[civ][i] = 0;
    }
    return 1;
}

/* o_05_0e67: a civilization that has lost its last city is destroyed (and may restart). */
void civ_check_destroyed(int civ, int by)
{
    SaveData *s = &game.s;
    int i, x, y;
    char msg[160];

    if (civ == 0 || civ == s->human_civ)
        return;
    for (i = 0; i < MAX_CITIES; i++)
        if (s->cities[i].status != NONE8 && (int8_t)s->cities[i].owner == civ)
            return;
    snprintf(msg, sizeof msg, "%s\ncivilization\ndestroyed\nby %s!\n", s->civ_adjectives[civ], s->civ_names[by]);
    ui_message(msg, by == s->human_civ ? 3 : (s->diplomacy[s->human_civ][civ] & DIP_EMBASSY) ? 5 : 2);
    replay_log(13, 2, civ, by, 0, 0);
    for (i = 0; i < MAX_UNITS; i++)
        if (s->units[civ][i].type != NONE8)
            delete_unit(civ, i);
    for (x = 0; x < MAP_W; x++)
        for (y = 0; y < MAP_H; y++)
            s->visibility[x][y] &= ~(1 << civ);
    civ_restart(civ);
}
