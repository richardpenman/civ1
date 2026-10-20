/* Starting a new game (original overlay 5: o_05_0000, o_05_0fcf, o_05_1d03). */
#include <stdio.h>
#include <string.h>
#include "game.h"
#include "civ.h"
#include "map.h"
#include "unit.h"
#include "util.h"
#include "save.h"
#include "science.h"
#include "newgame.h"
#include "turn.h"
#include "ui.h"
#include "view.h"

extern const uint8_t fresh_save[];
int no_sound_option;            /* g_1a30 == 'N' in the original's configuration */

/* Everything as the program has it before any game. */
void fresh_state(void)
{
    save_decode(fresh_save, SVE_SIZE, &game.s);
    reset_transient_state();
}

/* o_05_1d03: no spaceship. */
static void clear_spaceship(int civ)
{
    game.s.unk_ed30[civ] = -1;
    memset(&game.s.unk_far_2786_0740_bytes[civ * 180], 0xFF, 180);
}

/* o_05_0fcf: civilizations with poor starting places get extra settlers and advances.
 * Returns the civ with the best place (preferring one far from others). */
static int balance_starts(void)
{
    SaveData *s = &game.s;
    int score[MAX_CIVS] = {0}, far_mask = 0, best = 0, civ, o, d, nearest, k, t, sea, grass, river, size, pick = -1;

    for (civ = 1; civ < MAX_CIVS; civ++) {
        const Unit *u = &s->units[civ][0];
        if (u->type == NONE8)
            continue;
        nearest = 999;
        for (o = 1; o < MAX_CIVS; o++) {
            const Unit *v = &s->units[o][0];
            if (o == civ || v->type == NONE8 || map_continent(v->x, v->y) != map_continent(u->x, u->y))
                continue;
            d = map_distance(u->x, u->y, v->x, v->y);
            if (d < nearest)
                nearest = d;
        }
        if (nearest >= 10 && nearest != 999)
            far_mask |= 1 << civ;
        if (nearest >= 10) {
            if (nearest < 20)
                score[civ] += 1;
            else if (nearest == 999)
                score[civ] += 4;
            else
                score[civ] += 2;
        }
        sea = grass = river = 0;
        for (k = 0; k <= 8; k++) {
            t = map_terrain(u->x + around_dx[k], u->y + around_dy[k]);
            if (t == TERRAIN_OCEAN)
                sea++;
            if (t == TERRAIN_GRASSLAND)
                grass++;
            if (t == TERRAIN_RIVER)
                river++;
        }
        (void)sea;
        if (river > 1)
            score[civ] += 2;
        else if (grass >= 3)
            score[civ] += 1;
        size = body_area(u->x, u->y) / 50;
        if (size == 2 || size == 3)
            score[civ] += 1;
        else if (size > 3)
            score[civ] += 2;
        if (score[civ] > best)
            best = score[civ];
    }
    for (civ = 1; civ < MAX_CIVS; civ++) {
        const Unit *u = &s->units[civ][0];
        if (u->type == NONE8)
            continue;
        if (best - score[civ] > 3) {
            score[civ] += 3;
            create_unit(civ, UNIT_SETTLERS, u->x, u->y);
        }
        for (k = 0; best - score[civ] > k; k++)
            research_complete(civ, 0);
        if (score[civ] == best && (pick == -1 || (far_mask & (1 << civ))))
            pick = civ;
    }
    return pick;
}

/* o_05_0000 (after the world is made): the game's settings and every civilization's start. */
void new_game(const NewGameChoices *ch)
{
    SaveData *s = &game.s;
    int i, human, pick_random, type = 0, n;

    s->difficulty = (int16_t)(ch->difficulty == -1 ? 0 : ch->difficulty);
    s->unk_e214 = (int16_t)clamp(6 - ch->civ_count_choice, 2, 6);    /* rivals */
    s->civs_alive = (int16_t)(255 >> (6 - s->unk_e214));
    s->year = -4000;
    s->rules_flags = 15;
    for (i = 0; i < MAX_CITIES; i++) {
        s->cities[i].name = NONE8;
        s->cities[i].status = NONE8;
    }
    memset(s->city_name_x, 0xFF, sizeof s->city_name_x);
    for (i = 0; i < NUM_ADVANCES; i++)
        s->first_discoverer[i] = 0;
    for (i = 0; i <= NUM_WONDERS; i++)
        s->wonder_city[i] = -1;
    for (i = 0; i < MAX_CIVS; i++) {
        s->civ_type[i] = -1;
        clear_spaceship(i);
    }
    memset(s->visibility, 0, sizeof s->visibility);
    s->unk_19c0 = 250;
    if (no_sound_option)
        s->unk_19c0 &= ~0x10;

    /* The human's tribe: the menu lists tribes 1..n and then their alternatives 9..n+8. */
    human = ch->tribe_choice;
    if (human <= s->unk_e214)
        human++;
    else
        human = human - s->unk_e214 + 8;
    if (s->difficulty == 0)
        s->unk_19c0 |= 1;
    s->human_research = -1;
    s->unk_1c26 += (int16_t)rnd(50);
    for (i = 0; i < 12; i++) {
        s->unk_e83e[i] = -1;
        s->unk_c142[i] = 0;                         /* the palace */
    }
    for (i = 3; i <= 5; i++) {
        s->unk_e83e[6 + i] = 0;
        s->unk_e83e[i] = 0;
    }
    if (human == 0) {
        pick_random = 1;
        human = rnd(s->unk_e214) + 1;
    } else {
        pick_random = 0;
        type = human > 7;
        human &= 7;
    }
    s->human_civ = (int16_t)human;
    s->civ_adjectives[human][0] = 0;
    if (pick_random) {
        ui_custom_tribe(human);                      /* o_23_0173 */
        s->civ_type[human] = (int16_t)human;
        snprintf(s->leader_names[human], sizeof s->leader_names[human], "%s", civ_types[human].leader);
    } else {
        char name[12];
        s->civ_type[human] = (int16_t)(type ? human + 8 : human);
        n = s->civ_type[human];
        snprintf(s->leader_names[human], sizeof s->leader_names[human], "%s", civ_types[n].leader);
        snprintf(s->civ_adjectives[human], sizeof s->civ_adjectives[human], "%s", civ_types[n].adjective);
        snprintf(name, sizeof name, "%s", civ_types[n].plural);
        if (name[0] == 0)
            snprintf(name, sizeof name, "%ss", s->civ_adjectives[human]);
        snprintf(s->civ_names[human], sizeof s->civ_names[human], "%s", name);
    }
    for (i = 0; i < MAX_CIVS; i++) {
        civ_restart(i);
        memset(s->kills[6], 0, sizeof s->kills[6]);  /* (sic: always row 6) */
    }
    balance_starts();
    s->human_mask = (int16_t)(1 << human);
    s->tax_rate[human] = 5;
    s->science_rate[human] = 5;
    if (s->difficulty == 0)
        s->treasury[human] = 50;
    s->unk_ee3e = 0;
    for (i = 0; i < MAX_CIVS; i++)
        if (s->civ_type[i] >= 8)
            s->unk_ee3e |= (int16_t)(1 << i);
    ui_leader_name(human);                           /* o_23_00d6 */
    frame_screen();                         /* (the intro's border: f_0860_25e1) */
    ui_game_intro(human);
}
