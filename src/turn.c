/* The game turn (original seg 0237 and seg 0ad5). */
#include <stdio.h>
#include "game.h"
#include "civ.h"
#include "city.h"
#include "ui.h"
#include "view.h"
#include "util.h"
#include "ai.h"
#include "diplomacy.h"
#include "endturn.h"
#include "move.h"
#include "replay.h"
#include "turn.h"
#include "unit.h"

/* f_0ad5_0008: every city of `civ` has its turn; computer players then set their rates. */
void civ_process_cities(int civ)
{
    SaveData *s = &game.s;
    int i, trouble = 0, lux, used;

    ai_unhappy_score = 0;
    ai_support_score = 0;
    for (i = 0; i < MAX_CITIES; i++) {
        City *c = &s->cities[i];
        if (c->status == NONE8 || (int8_t)c->owner != civ)
            continue;
        for (;;) {
            if (city_process(i, 0) == 0 && (int8_t)c->size > 5)
                trouble |= 1;                 /* a big city on the edge of disorder */
            if (c->status & CITY_DISORDER)
                trouble |= 2;
            if (!show_city_again)
                break;
            /* Something happened in a human city: show it (and maybe run it again). */
            city_screen(civ, i);             /* (city_process(i, 1): the screen and its input) */
            redraw_map();
            if (show_city_again != 2) {
                show_city_again = 0;
                break;
            }
            show_city_again = 0;
        }
    }
    if (civ == s->human_civ || !(s->rules_flags & RULE_GROWTH_LIMITS))
        return;

    used = s->science_rate[civ] + s->tax_rate[civ];
    lux = 10 - used;
    if ((trouble & 2) && used > 6)
        lux++;
    if ((s->turn & 3) == 0 && trouble == 0 && s->tax_rate[civ] + s->science_rate[civ] < 8)
        lux--;
    lux = clamp(lux, 0, 4);
    s->science_rate[civ] = civ_types[s->civ_type[civ]].science_bias + (10 - lux) / 2;
    if (s->treasury[civ] > s->turn + 100)
        s->science_rate[civ]++;
    if (civ_has_advance(civ, 63))      /* sic: the AI stops research once it knows Robotics */
        s->science_rate[civ] = 0;
    s->tax_rate[civ] = 10 - s->science_rate[civ] - lux;
}

int load_marker = -1;            /* g_3434: -3 right after loading a game, then -2, then 0 */
int pop_milestone = 1;           /* g_1c24: last population announced (tens or 100s + 9) */
int retired;                     /* g_c7a4: the human retired (game over by choice) */

/* o_20_0c98 without the screen: the score of `civ`.  It is added up in score[0] (sic) and
 * returned.  Citizens count with luxuries raised to 40%. */
int civ_score(int civ)
{
    SaveData *s = &game.s;
    int saved_tax = s->tax_rate[civ], citizens = 0, wonders = 0, c, w, ship, peace;
    int16_t *score = &s->score[0];

    if (s->unk_4c88 & 0x100)                         /* scoring completed */
        return *score;
    *score = 0;
    s->tax_rate[civ] = (int16_t)(6 - s->science_rate[civ]);
    for (c = 0; c < MAX_CITIES; c++) {
        const City *cy = &s->cities[c];
        if (cy->status == NONE8 || (int8_t)cy->owner != civ)
            continue;
        city_process(c, -1);
        citizens += (int8_t)cy->size + calc.happy - calc.unhappy;
    }
    s->tax_rate[civ] = (int16_t)saved_tax;
    for (w = 1; w <= NUM_WONDERS; w++)
        if (s->wonder_city[w] != -1 && s->wonder_city[w] != 128 &&
            (int8_t)s->cities[s->wonder_city[w]].owner == civ)
            wonders += 20;
    *score = (int16_t)(citizens + wonders);
    if (s->unk_ee0c[s->human_civ] != 0 && s->unk_4c84 != 0) {
        ship = s->unk_4c84 * s->unk_ee0c[s->human_civ] / 2;   /* (the human's ship, sic) */
        if (ship < 0)
            ship = 0;
        if (s->unk_ed30[s->human_civ] == s->year)
            *score += (int16_t)ship;
    }
    *score -= (int16_t)(s->pollution_count * 10);
    if (s->year > 0 && s->peace_turns != 0) {
        peace = clamp(s->peace_turns * 3, 0, 100);
        *score += (int16_t)peace;
    }
    *score += (int16_t)(s->future_tech_count * 5);
    if (retired)
        *score = (int16_t)((550 - s->turn) * 2 + s->unk_e214 * 100 + 400);
    if (*score < 0)
        *score = 0;
    return *score;
}

int status_peace;               /* the peace bar as the status panel last showed it */

/* f_0237_107a: the status panel; new population records are announced. */
void status_report(void)
{
    SaveData *s = &game.s;
    int pop, old;
    char msg[160];

    if (fast_view == 1)
        return;
    status_peace = s->peace_turns;
    ui_status_panel();
    pop = civ_population(s->human_civ);
    old = pop_milestone;
    msg[0] = 0;
    if (pop < 100) {
        if (pop_milestone * 10 < pop) {
            snprintf(msg, sizeof msg, "%d00,000", pop / 10);
            pop_milestone = pop / 10;
        }
    } else if ((pop_milestone - 9) * 100 < pop) {
        snprintf(msg, sizeof msg, "%d,000,000", pop / 100);
        pop_milestone = pop / 100 + 9;
    }
    if (old < pop_milestone && load_marker >= -1) {
        char text[200];
        snprintf(text, sizeof text, "The population of the\nfertile %s empire now\nexceeds %s citizens.\n",
                 s->civ_adjectives[s->human_civ], msg);
        ui_message(text, 4);
    }
}

/* o_17_0000: the human's palace grows by one part.  Sections 1..7 are the castle (levels
 * unk_e83e[] -1 none .. 4, styles unk_c142[] 0..2); a section is offered if it can still grow
 * and stands or has a built neighbour.  9..11 are the gardens (levels up to 3).  An empty
 * section chosen away from the middle moves inwards until it touches the palace. */
int palace_section_open(int i)
{
    const int16_t *lv = game.s.unk_e83e;

    if (i >= 9 && i <= 11)
        return lv[i] < 3;
    if (i < 1 || i > 7 || lv[i] >= 4)
        return 0;
    return lv[i - 1] > 0 || lv[i + 1] > 0 || lv[i] >= 0;
}

void palace_improve(void)
{
    int16_t *lv = game.s.unk_e83e;
    int i, any = 0, sec, style = 0;


    for (i = 1; i <= 11; i++)
        if (i != 8 && palace_section_open(i))
            any = 1;
    sec = any ? ui_palace(&style) : -1;
    /* (a click is taken for any section that can grow: an empty one then moves inwards) */
    if ((sec >= 1 && sec <= 7 && lv[sec] < 4) || (sec >= 9 && sec <= 11 && lv[sec] < 3)) {
        if (sec < 9) {
            while (sec < 4 && lv[sec + 1] <= 0 && lv[sec] == -1)
                sec++;
            while (sec > 4 && lv[sec - 1] <= 0 && lv[sec] == -1)
                sec--;
        } else
            style = 0;                    /* (the original stores an unset local here) */
        lv[sec] = (int16_t)clamp(lv[sec] + 1, 1, 4);
        game.s.unk_c142[sec] = (int16_t)style;
    }
    redraw_map();
}

/* game_turn_loop (f_0237_0068): one whole turn.  Returns quit_flag. */
int game_turn(void)
{
    SaveData *s = &game.s;
    int human = s->human_civ, gold = s->treasury[human], c, k, w, palace, arriving, ranks[4], pop;

    umax_shown = 0;
    disorder_shown = 0;
    s->unk_7fcc = 0;
    for (c = 0; c < MAX_CIVS; c++)
        if ((s->civs_alive & (1 << c)) && s->unk_7fcc < s->advances_known[c])
            s->unk_7fcc = s->advances_known[c];
    if (s->turn == 0) {
        view_x = s->units[human][0].x - 7;
        view_y = s->units[human][0].y - 6;
        redraw_map();
        if (s->difficulty == 0) {
            ui_tutorial_start();
            redraw_map();
        }
        ui_play_theme(1);
    }

    for (c = 0; c < MAX_CIVS; c++) {
        s->score[c] = 0;
        if (!(s->civs_alive & (1 << c)))
            continue;
        if (c < human && midturn_loaded)              /* (a game saved during the human's move) */
            continue;
        ui_turn_marker(1);
        if (!midturn_loaded)
            civ_process_cities(c);
        if (s->treasury[c] > 30000)
            s->treasury[c] = 30000;
        ai_diplomacy(c);
        ai_strategy(c);
        ai_reset_goals(c);
        ui_turn_marker(0);
        if (c == human) {
            status_report();
            if (s->city_count[c] == 0 && s->settler_count[c] == 0) {
                ui_civilization_lost();
                quit_flag = -1;
            }
            if (s->year > 0)
                s->peace_turns++;
        }
        if (!quit_flag)
            civ_move_units(c);
        midturn_loaded = 0;
        if (quit_flag && !retired) {
            if (quit_flag == 2) {
                ui_game_over(-1);
                if (!(s->unk_4c88 & 0x100))
                    redraw_map();            /* (the score screen, o_20_0c98, ends so) */
                ui_final_screens();
            }
            return quit_flag;
        }
    }

    barbarians_appear();
    world_events();
    if (s->turn > 50) {
        /* (the loop counter doubles as the city number: usually one try, sic) */
        for (k = 0; k < 2; k++) {
            k = rnd(MAX_CITIES);
            if (s->cities[k].status == NONE8 || s->city_count[(int8_t)s->cities[k].owner] <= 1)
                continue;
            if ((int8_t)s->cities[k].size >= 5)
                city_disaster(k);
        }
    }
    civ_ranking();
    if (!quit_flag && s->city_count[human] > 1 && s->treasury[human] < 100 &&
        (s->treasury[human] - gold) * 10 + s->treasury[human] < 0 && (s->human_mask & (1 << human)))
        ui_help_message("*TAXES");

    s->turn++;
    if (s->year < 1000)
        s->year += 20;
    else if (s->year < 1500)
        s->year += 10;
    else if (s->year < 1750)
        s->year += 5;
    else if (s->year < 1850 && !(s->unk_4c88 & 0xFE))
        s->year += 2;
    else
        s->year++;
    if (s->year == 0) {                               /* there is no year 0: research doubles */
        for (c = 0; c < MAX_CIVS; c++)
            s->research_progress[c] <<= 1;
        s->year = 1;
    }
    if (s->year == 21)
        s->year = 20;

    if (s->turn % 50 == 0 && human != -1) {
        if (s->unk_19c0 & 2)
            ui_autosave((s->turn / 50 - 1) % 6 + 4);
        s->unk_7fcc = 0;
        for (c = 1; c < MAX_CIVS; c++)
            if (s->unk_7fcc < s->advances_known[c])
                s->unk_7fcc = s->advances_known[c];
        research_bonus = 0;
        if (s->year >= 0)
            research_bonus = clamp(s->unk_7fcc - s->turn / 9, 0, 6);
        pop = civ_population(human);
        replay_log(11, 3, s->city_count[human], pop / 256, pop & 255, 0);
        for (k = 0; k < 4; k++)
            ranks[k] = 0;
        for (c = 1; c < MAX_CIVS; c++) {
            if (!(s->civs_alive & (1 << c)))
                continue;
            if (s->civ_rank[c] & 1)
                ranks[s->civ_rank[c] / 2] |= c << 4;
            else
                ranks[s->civ_rank[c] / 2] |= c;
        }
        replay_log(12, 4, ranks[0], ranks[1], ranks[2], ranks[3]);
    }
    if (s->turn >= s->unk_1c26) {
        s->unk_1c26 = (int16_t)(rnd(40) + s->turn + 20);
        historian_report();
    }

    for (w = 1; w <= NUM_WONDERS; w++)
        if (s->wonder_city[w] != -1 && s->wonder_city[w] != 128)
            s->score[(int8_t)s->cities[s->wonder_city[w]].owner] += 25;
    if (load_marker == -3)
        load_marker = -2;
    else {
        s->score[human] -= (int16_t)(s->pollution_count * 10);
        if (load_marker == -2)
            load_marker = 0;
    }
    palace = 0;
    for (c = 0; c < MAX_CITIES; c++)
        if (s->cities[c].status != NONE8 && (int8_t)s->cities[c].owner == human &&
            (s->cities[c].buildings & BIT(B_PALACE)))
            palace = 1;
    if (palace && (s->unk_19c0 & 128) && !(s->unk_4c88 & 0x100) &&
        civ_score(human) >= palace_threshold(s->unk_1c20 + 1)) {
        s->unk_1c20++;
        if (s->unk_1c20 <= 37)
            palace_improve();
    }
    status_report();

    arriving = -1;
    for (c = 1; c < MAX_CIVS; c++)
        if ((s->unk_4c88 & (1 << c)) && s->unk_ed30[c] == s->year)
            arriving = c;
    if (s->year == 2080 - s->difficulty * 20) {
        char msg[160];
        snprintf(msg, sizeof msg, "%s %s\nplans retirement\nin 20 years.\n",
                 leader_titles[s->government[human]], s->leader_names[human]);
        newspaper(-1, msg);
    }
    if (arriving == -1 && !retired && s->year != 2100 - s->difficulty * 20)
        return quit_flag;

    /* The end: a spaceship arrives, the human retires, or time runs out. */
    if (retired) {
        arriving = -1;
        newspaper(-1, "*** GAME OVER ***\nYour civilization\nhas conquered\nthe entire planet!\n");
        ui_replay();                          /* o_09_0dde, which ends redrawing the map */
        redraw_map();
    }
    if (arriving != -1) {
        char msg[160];
        ui_spaceship_arrives(arriving);
        redraw_map();
        snprintf(msg, sizeof msg, "*** GAME OVER ***\n%s spaceship\narrives at Alpha Centauri.\n",
                 s->civ_adjectives[arriving]);
        newspaper(-1, msg);
    }
    if (s->year == 2100 - s->difficulty * 20) {
        char msg[160];
        snprintf(msg, sizeof msg, "*** GAME OVER ***\n%s dynasty\nends after glorious\n6000 year reign!\n",
                 s->leader_names[human]);
        ui_play_theme(civ_types[s->civ_type[human]].unk4);
        newspaper(-1, msg);
        ui_play_theme(1);
    }
    if (!(s->unk_4c88 & 0x100)) {
        ui_game_over(arriving);               /* f_0237_08a2: its score screen redraws the map */
        redraw_map();
    }
    redraw_map();
    s->unk_4c88 |= 0x100;
    if (!retired)
        quit_flag = -(ui_ask("*FAME", 0) - 1);
    else
        quit_flag = 1;
    if (quit_flag)
        ui_final_screens();
    return quit_flag;
}
