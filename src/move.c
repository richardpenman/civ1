/* Unit orders and movement (original seg 0402: civ_move_units, and seg 0860). */
#include <stdio.h>
#include <stdlib.h>
#include "game.h"
#include "civ.h"
#include "city.h"
#include "map.h"
#include "unit.h"
#include "util.h"
#include "ai.h"
#include "diplomacy.h"
#include "combat.h"
#include "science.h"
#include "move.h"

int last_dir;         /* l36 in the original: the last direction moved */
int last_terrain;     /* l62 in the original: the last terrain looked at */
#include "ui.h"
#include "view.h"

#define DIP(a, b) game.s.diplomacy[a][b]
#define U(c, u) (&game.s.units[c][u])
#define UT(t) (&game.s.unit_types[t])

/* f_0402_3f80: may (x, y) be irrigated?  Water must touch it orthogonally. */
int water_near(int x, int y)
{
    int i, sx, sy, t;

    if (map_terrain(x, y) == TERRAIN_RIVER)
        return 1;
    for (i = 1; i <= 8; i += 2) {
        sx = x + around_dx[i];
        sy = y + around_dy[i];
        t = map_terrain(sx, sy);
        if (map_improvements(sx, sy) & IMP_CITY)
            continue;
        if (t == TERRAIN_RIVER || t == TERRAIN_OCEAN || (map_improvements(sx, sy) & IMP_IRRIGATION))
            return 1;
    }
    return 0;
}

/* f_0860_222e: which way an air unit should fly to reach the nearest own city or carrier. */
static int air_home_direction(int civ, int u)
{
    const Unit *p = U(civ, u);
    int best = 999, tx = 0, ty = 0, i, d, dx, dy, x, y, o, dir = 0;

    for (i = 0; i < MAX_CITIES; i++) {
        const City *c = &game.s.cities[i];
        if (c->status == NONE8 || (int8_t)c->owner != civ)
            continue;
        dx = abs(p->x - c->x);
        if (dx > 40)
            dx = 80 - dx;
        dy = abs(p->y - c->y);
        d = dx >= dy ? dx : dy;
        if (d < best) {
            best = d;
            tx = c->x;
            ty = c->y;
        }
    }
    for (i = 0; i < MAX_UNITS; i++) {
        const Unit *q = U(civ, i);
        if (q->type != UNIT_CARRIER)
            continue;
        dx = abs(p->x - q->x);
        if (dx > 40)
            dx = 80 - dx;
        dy = abs(p->y - q->y);
        d = dx >= dy ? dx : dy;
        if (d < best) {
            best = d;
            tx = q->x;
            ty = q->y;
        }
    }
    best = map_distance2(p->x - tx, p->y - ty);
    for (i = 1; i <= 8; i++) {
        x = p->x + around_dx[i];
        y = p->y + around_dy[i];
        o = map_unit_owner(x, y);
        if (o != -1 && o != civ)
            continue;
        if ((map_improvements(x, y) & IMP_CITY) && map_unit_layer_owner(x, y) != civ)
            continue;
        d = map_distance2(x - tx, y - ty);
        if (d < best) {
            best = d;
            dir = i;
        }
    }
    return dir;
}

/* f_0402_4512: when a ship unloads, the next unit to move is the first land unit after it. */
static int next_cargo(int civ, int u)
{
    int n = (int8_t)U(civ, u)->next, best = u, best_d = 999, k = 0, d;

    if (n == -1)
        return u;
    do {
        if (UT(U(civ, n)->type)->domain != DOMAIN_SEA) {
            d = n > u ? n - u : n - u + 128;
            if (d < best_d) {
                best_d = d;
                best = n;
            }
        }
        n = (int8_t)U(civ, n)->next;
    } while (n != u && ++k < 16);
    return best;
}

/* f_0860_18f2: a unit walks into a hut. */
static void enter_hut(int civ, int u)
{
    Unit *p = U(civ, u);
    int dist, c, roll, i, x, y, t, n, adv, tries;
    int human = game.s.human_civ;

    if (civ == 0 || UT(p->type)->domain == DOMAIN_AIR)
        return;
    c = nearest_city(p->x, p->y, &dist);
    roll = rnd(4);
    switch (roll) {
    case 0:
        if (dist < 4)
            goto mercenaries;
        if ((map_pixel(p->x + 80, p->y) & 7) > 4) {
            if (civ == human)
                ui_message("You have discovered\nan advanced tribe.\n", 0);
            found_city(civ, 0, p->x, p->y, 1);
            break;
        }
        /* fall through */
    case 2:
    gold:
        if (civ == human)
            ui_message("You have discovered\nvaluable metal deposits\nworth 50$\n", 0);
        game.s.treasury[civ] += 50;
        return;
    case 1:
        if (game.s.turn == 0 || game.s.year > 1000)
            goto gold;
        if (civ == human)
            ui_message("You have discovered\nscrolls of ancient wisdom.\n", 0);
        adv = rnd(NUM_ADVANCES);
        for (tries = 0; tries < 999 && adv != -1; tries++) {
            if (!civ_has_advance(civ, adv) && civ_has_advance(civ, advance_types[adv].prereq[0]) &&
                civ_has_advance(civ, advance_types[adv].prereq[1])) {
                give_advance(civ, adv, 0);
                adv = -1;
            } else {
                adv = (adv + 1) % NUM_ADVANCES;
            }
        }
        return;
    case 3:
        if (dist < 4 || game.s.city_count[civ] == 0) {
        mercenaries:
            if (civ == human)
                ui_message("You have discovered\na friendly tribe of\nskilled mercenaries.\n", 0);
            create_unit(civ, rnd(2) ? 3 : 6, p->x, p->y);
            break;
        }
        if (civ == human)
            ui_message("You have unleashed\na horde of barbarians!\n", 0);
        for (i = 1; i <= 8; i += clamp(4 - game.s.city_count[civ], 1, 4)) {
            x = p->x + around_dx[i];
            y = p->y + around_dy[i];
            if (map_unit_owner(x, y) + 1 != 0 || (map_improvements(x, y) & IMP_CITY) ||
                map_terrain(x, y) == TERRAIN_OCEAN)
                continue;
            t = terrain_types[map_terrain(x, y)].move_cost > 2 ? 3 : 6;
            n = create_unit(0, t, x, y);
            if (n != -1)          /* (the original writes through unit -1 when full) */
                U(0, n)->visible |= 1 << civ;
            if (civ == human)
                square_redraw(x, y);
        }
        return;
    }
    if (civ == human)
        square_redraw(p->x, p->y);
    (void)c;
}

/* Finish or abandon terrain work (f_0860_1679 is only the view-follows-unit part). */
static void work_done(int civ, int u)
{
    view_follow(civ, U(civ, u)->x, U(civ, u)->y);
}

/* Terrain work by Settlers: one more turn on it; returns 1 when the work is done. */
static int settler_work(Unit *p, int status, int turns)
{
    p->status |= (uint8_t)status;
    p->moves = 0;
    p->unk8 = NONE8;
    p->goto_x = NONE8;
    p->special++;
    return (int8_t)p->special >= turns;
}

/* Execute one order for unit (civ, u): a direction 1..8 or a command key.  The results
 * the caller's loop needs come back in *flags (MOVE_END_UNIT, MOVE_NEXT_UNIT). */
int unit_command(int civ, int u, int key, int *next_unit)
{
    Unit *p = U(civ, u);
    int human = game.s.human_civ;
    int x0 = p->x, y0 = p->y, x, y, dir = 0, t0, t1, imp, top, owner, c, r, n, carried, k, nxt;
    int flags = 0;

    switch (key) {
    case 'D':                                         /* disband */
        p->moves = 0;
        delete_unit(civ, u);
        return MOVE_DONE;
    case 'P':                                         /* pillage */
        imp = map_improvements(x0, y0);
        if ((imp & IMP_CITY) || p->type >= UNIT_DIPLOMAT || p->type == 14) {
            ui_help_message("*PILLAGE");
            return MOVE_DONE;
        }
        c = nearest_city(x0, y0, NULL);
        if (p->moves == 0 || confirm_break_treaty((int8_t)game.s.cities[c].owner) + 1 == 0)
            return MOVE_DONE;
        if (imp & (IMP_IRRIGATION | IMP_MINE))
            map_clear_improvements(IMP_IRRIGATION | IMP_MINE, x0, y0);
        else if (imp & IMP_RAILROAD)
            map_clear_improvements(IMP_RAILROAD, x0, y0);
        else
            map_clear_improvements(IMP_ROAD, x0, y0);
        if ((int8_t)game.s.cities[c].owner == human)
            map_refresh_known(x0, y0);
        square_redraw(x0, y0);
        diplomacy_clear(human, (int8_t)game.s.cities[c].owner, DIP_PEACE);
        if (civ != (int8_t)game.s.cities[c].owner)
            ai_add_goal((int8_t)game.s.cities[c].owner, x0, y0, 1, 4);
        p->moves = 0;
        return MOVE_DONE;
    case ' ':                                         /* no more moves this turn */
        p->moves = 0;
        return MOVE_DONE;
    case 'w':                                         /* wait */
        return MOVE_DONE | MOVE_END_UNIT | MOVE_RESCAN;
    case 's':                                         /* sentry */
        p->status |= UNIT_SENTRY;
        p->moves = 0;
        redraw_unit_square(civ, u);
        return MOVE_DONE;
    case 'u':                                         /* unload */
        if ((UT(p->type)->capacity || p->type == UNIT_CARRIER) && p->next != NONE8) {
            stack_foreach(civ, u, cb_unload);
            nxt = next_cargo(civ, u);
            if (next_unit)
                *next_unit = nxt;
            return MOVE_DONE | MOVE_END_UNIT | MOVE_JUMP;
        }
        return MOVE_DONE;
    case 'h':                                         /* home city / air unit returns */
        c = nearest_city(x0, y0, &k);
        if (k == 0) {
            p->home_city = (uint8_t)c;
        } else if (UT(p->type)->domain == DOMAIN_AIR) {
            dir = air_home_direction(civ, u);
            if (dir) {
                p->status |= UNIT_ROAD;    /* (the bit doubles as "returning home" for aircraft) */
                goto move;
            }
            if (civ == human)
                p->status &= ~UNIT_ROAD;
            else {
                p->moves = 0;
                delete_unit(civ, u);
            }
        }
        return MOVE_DONE;
    case 'p':                                         /* clean pollution */
        if (p->type != UNIT_SETTLERS) {
            ui_help_message("*SETTLERS");
            return MOVE_DONE;
        }
        if (!(map_improvements(x0, y0) & IMP_POLLUTION)) {
            p->status &= 0x7D;
            p->special = 0;
            work_done(civ, u);
            return MOVE_DONE;
        }
        p->status |= UNIT_ROAD | UNIT_MINE;
        redraw_unit_square(civ, u);
        p->moves = 0;
        p->unk8 = NONE8;
        p->goto_x = NONE8;
        if (++p->special < 4)
            return MOVE_DONE;
        map_clear_improvements(IMP_POLLUTION, x0, y0);
        map_refresh_known(x0, y0);
        game.s.pollution_count--;
        redraw_unit_square(civ, u);
        p->status &= 0x7D;
        p->special = 0;
        return MOVE_DONE;
    case 'f':                                         /* fortify, or build a fortress */
        if (UT(p->type)->domain != DOMAIN_LAND)
            return MOVE_DONE;
        if (p->type != UNIT_SETTLERS) {
            p->status |= UNIT_FORTIFYING;
            p->moves = 0;
            redraw_unit_square(civ, u);
            if (civ != human && (map_improvements(x0, y0) & IMP_CITY))
                p->home_city = (uint8_t)city_at(x0, y0);
            return MOVE_DONE;
        }
        if ((map_improvements(x0, y0) & (IMP_CITY | IMP_FORTRESS)) || !civ_has_advance(civ, 45)) {
            p->status &= 0x3F;
            p->special = 0;
            work_done(civ, u);
            return MOVE_DONE;
        }
        p->status |= UNIT_IRRIGATE | UNIT_MINE;
        p->moves = 0;
        redraw_unit_square(civ, u);
        p->special++;
        t0 = map_terrain(x0, y0);                    /* sic: the original reads a stale terrain here */
        if ((int8_t)p->special < terrain_types[last_terrain].move_cost + 4)
            return MOVE_DONE;
        (void)t0;
        map_set_improvements(IMP_FORTRESS, x0, y0);
        unit_reveal(x0, y0, civ, u, 1);
        p->status &= 0x3F;
        p->special = 0;
        return MOVE_DONE;
    case 'r':                                         /* road / railroad */
        if (p->type != UNIT_SETTLERS) {
            ui_help_message("*SETTLERS");
            return MOVE_DONE;
        }
        imp = map_improvements(x0, y0);
        if (((imp & IMP_ROAD) && (!civ_has_advance(civ, ADV_RAILROAD) || (imp & IMP_CITY))) || (imp & IMP_RAILROAD)) {
            p->status &= ~UNIT_ROAD;
            p->special = 0;
            work_done(civ, u);
            return MOVE_DONE;
        }
        t0 = map_terrain(x0, y0);
        last_terrain = t0;
        if (t0 == TERRAIN_RIVER && !civ_has_advance(civ, 19))     /* Bridge Building */
            return MOVE_DONE;
        p->status |= UNIT_ROAD;
        p->moves = 0;
        redraw_unit_square(civ, u);
        p->special++;
        if ((imp & IMP_ROAD ? 4 : 2) * terrain_types[t0].move_cost > (int8_t)p->special)
            return MOVE_DONE;
        map_set_improvements(imp & IMP_ROAD ? IMP_RAILROAD : IMP_ROAD, x0, y0);
        unit_reveal(x0, y0, civ, u, 1);
        p->status &= ~UNIT_ROAD;
        p->special = 0;
        return MOVE_DONE;
    case 'i':                                         /* irrigate */
        if (p->type != UNIT_SETTLERS || !(game.s.rules_flags & RULE_REAL_IMPROVEMENTS) ||
            (map_improvements(x0, y0) & IMP_IRRIGATION)) {
            work_done(civ, u);
            if (p->type != UNIT_SETTLERS)
                ui_help_message("*SETTLERS");
            else
                p->special = 0;
            p->status &= ~UNIT_IRRIGATE;
            return MOVE_DONE;
        }
        t0 = map_terrain(x0, y0);
        last_terrain = t0;
        r = terrain_improve[t0].irrigate;
        if (r == -1) {
            work_done(civ, u);
            ui_help_message("*NOIRR");
            p->status &= ~UNIT_IRRIGATE;
            return MOVE_DONE;
        }
        if (r == -2 && !water_near(x0, y0)) {
            work_done(civ, u);
            ui_help_message("*NOWATER");
            p->status &= ~UNIT_IRRIGATE;
            return MOVE_DONE;
        }
        if (!settler_work(p, UNIT_IRRIGATE, terrain_improve[t0].irrigate_turns))
            return MOVE_DONE;
        redraw_unit_square(civ, u);
        if (r >= 0) {
            map_put_pixel(x0, y0, r);                 /* the square changes terrain */
            game.s.visibility[x0][y0] |= 1;
            map_clear_improvements(IMP_IRRIGATION | IMP_MINE, x0, y0);
        } else {
            map_clear_improvements(IMP_MINE, x0, y0);
            map_set_improvements(IMP_IRRIGATION, x0, y0);
        }
        redraw_unit_square(civ, u);
        p->status &= ~UNIT_IRRIGATE;
        p->special = 0;
        return MOVE_DONE;
    case 'm':                                         /* mine */
        if (p->type != UNIT_SETTLERS || (map_improvements(x0, y0) & IMP_MINE)) {
            work_done(civ, u);
            if (p->type != UNIT_SETTLERS)
                ui_help_message("*SETTLERS");
            else
                p->special = 0;
            p->status &= ~UNIT_MINE;
            return MOVE_DONE;
        }
        t0 = map_terrain(x0, y0);
        last_terrain = t0;
        r = terrain_improve[t0].mine;
        if (r == -1) {
            work_done(civ, u);
            ui_help_message("*NOMINE");
            p->status &= ~UNIT_MINE;
            return MOVE_DONE;
        }
        if (!settler_work(p, UNIT_MINE, terrain_improve[t0].mine_turns))
            return MOVE_DONE;
        if (r >= 0) {
            if (++p->special <= 5)
                return MOVE_DONE;
            map_put_pixel(x0, y0, r);
            game.s.visibility[x0][y0] |= 1;
            map_clear_improvements(IMP_IRRIGATION | IMP_MINE, x0, y0);
        } else {
            map_clear_improvements(IMP_IRRIGATION, x0, y0);
            map_set_improvements(IMP_MINE, x0, y0);
        }
        redraw_unit_square(civ, u);
        p->status &= ~UNIT_MINE;
        p->special = 0;
        return MOVE_DONE;
    case 'b':                                         /* build a city, or join one */
        if (p->type != UNIT_SETTLERS) {
            ui_help_message("*SETTLERS");
            return MOVE_DONE;
        }
        if (map_terrain(x0, y0) == TERRAIN_OCEAN)
            return MOVE_DONE;
        if (map_improvements(x0, y0) & IMP_CITY) {
            c = city_at(x0, y0);
            if (game.s.cities[c].size >= 10) {
                ui_help_message("*ADDCITY");
                return MOVE_DONE;
            }
            game.s.cities[c].size++;
            p->moves = 0;
            delete_unit(civ, u);
            redraw_unit_square(civ, u);
            return MOVE_DONE;
        }
        p->moves = 0;
        if (found_city(civ, last_dir, x0, y0, 1) != -1) {
            delete_unit(civ, u);
            redraw_unit_square(civ, u);
        }
        return MOVE_DONE;
    }
    if (key < 1 || key > 8)
        return 0;                                     /* (human-only keys are the UI's business) */
    dir = key;

move:
    last_dir = dir;
    x = wrap_x(x0 + around_dx[dir]);
    y = clamp(y0 + around_dy[dir], 0, MAP_H - 1);
    t0 = map_terrain(x0, y0);
    t1 = map_terrain(x, y);
    last_terrain = t1;
    imp = map_improvements(x, y);
    top = unit_at(x, y);
    owner = stack_owner;

    /* Barbarian raiders wreck what they stand on. */
    if (civ == 0 && p->type != UNIT_DIPLOMAT && !(map_improvements(x0, y0) & IMP_CITY) &&
        (map_improvements(x0, y0) & (IMP_IRRIGATION | IMP_MINE)) && p->type != 3 && p->type != 7 &&
        (int8_t)game.s.cities[nearest_city(x0, y0, NULL)].owner == human) {
        map_clear_improvements(IMP_IRRIGATION | IMP_MINE, x0, y0);
        if (game.s.visibility[x0][y0] & (1 << human)) {
            map_refresh_known(x0, y0);
            square_redraw(x0, y0);
        }
        p->moves = 0;
        return MOVE_DONE;
    }

    if (UT(p->type)->domain == DOMAIN_LAND) {
        /* Zones of control. */
        if (top == -1 && in_enemy_zone(civ, x0, y0) && in_enemy_zone(civ, x, y) &&
            p->type < UNIT_DIPLOMAT && t0 != TERRAIN_OCEAN) {
            if (civ == human)
                ui_sound(37);
            if (game.s.unk_19c0 & 1)
                ui_help_message("*ZOC");
            p->goto_x = NONE8;
            return MOVE_DONE;
        }
        if (p->type >= UNIT_DIPLOMAT) {
            c = city_at(x, y);
            if (c != -1) {
                City *ct = &game.s.cities[c];
                if ((int8_t)ct->owner != civ && t0 == TERRAIN_OCEAN)
                    return MOVE_DONE;
                if (p->type == UNIT_DIPLOMAT && civ != (int8_t)ct->owner) {
                    if ((int8_t)ct->owner == human) {
                        p->visible |= 1 << human;
                        view_follow(human, p->x, p->y);
                        ui_animate_move(civ, u, dir);
                    }
                    diplomat_enters_city(c, civ, u);
                    return MOVE_DONE;
                }
                if (p->type == UNIT_CARAVAN) {
                    k = map_distance(x, y, game.s.cities[p->home_city].x, game.s.cities[p->home_city].y);
                    /* (no question when it is our own city building an ordinary improvement nearby) */
                    if (!((int8_t)ct->owner == civ && (uint8_t)ct->producing >= 232 && k < 10 &&
                          map_continent(game.s.cities[p->home_city].x, game.s.cities[p->home_city].y) ==
                          map_continent(x, y))) {
                        r = 1;                /* a foreign city: trade route */
                        if ((int8_t)ct->owner == civ) {
                            char menu[128];
                            int wonder = (uint8_t)ct->producing < 232;
                            snprintf(menu, sizeof menu, "Will you?\n Keep moving\n Establish trade route\n%s",
                                     wonder ? " Help build WONDER.\n" : "");
                            r = ui_menu(menu, wonder && k < 10 ? 2 : 0);   /* no route that close */
                        }
                        if (r == 1 && caravan_arrive(civ, u, c))
                            return MOVE_DONE;
                        if (r == 2) {
                            ct->shields += UT(p->type)->cost * 10;     /* help build a wonder */
                            delete_unit(civ, u);
                            return MOVE_DONE;
                        }
                    }
                }
            } else if (p->type == UNIT_DIPLOMAT && top != -1 && civ != owner) {
                diplomat_bribe(owner, top, civ);
                p->goto_x = NONE8;
                owner = stack_owner;  /* (the original carries on with whatever unit_at() left) */
            }
        }
    }

    if (t1 == TERRAIN_OCEAN && UT(p->type)->domain == DOMAIN_LAND) {
        /* Boarding a ship. */
        if (civ == owner && top != -1 && stack_room(civ, top) > 0) {
            p->status |= UNIT_SENTRY;
            p->status &= 0xF3;
            p->moves = 3;
            goto step;
        }
        return MOVE_DONE;
    }
    if (top != -1 && civ != owner) {
        /* Attack. */
        if (t0 == TERRAIN_OCEAN && UT(p->type)->domain == DOMAIN_LAND) {
            ui_help_message("*AMPHIB");
            p->goto_x = NONE8;
            return MOVE_DONE;
        }
        if (t1 != TERRAIN_OCEAN && p->type == UNIT_SUBMARINE)
            return MOVE_DONE;
        if (civ != human && UT(p->type)->attack == 0)
            return MOVE_DONE;
        if (p->type == UNIT_DIPLOMAT)
            return MOVE_DONE;
        if (UT(U(owner, top)->type)->domain == DOMAIN_AIR && p->type != 14 && !(imp & IMP_CITY)) {
            ui_help_message("*FIGHTER");
            p->goto_x = NONE8;
            return MOVE_DONE;
        }
        if (p->moves < 3) {
            if (civ == human) {
                char msg[96];
                snprintf(msg, sizeof msg, "Attack at\n%d/3 strength?\n Cancel\n Attack\n", (int8_t)p->moves);
                if (ui_ask(msg, 0) != 1) {
                    p->goto_x = NONE8;
                    return MOVE_DONE;
                }
            } else if (p->moves < 2) {
                return MOVE_DONE;
            }
        }
        if (owner == human)
            view_follow(human, x, y);
        top = stack_best_defender(owner, top);
        if (civ == human || owner == human)
            count_losses = 1;
        r = unit_attack(civ, u, owner, top, 1);
        p->goto_x = NONE8;
        count_losses = 0;
        if (r == -1)
            return MOVE_DONE;
        if (p->type == NONE8) {
            p->moves = 0;
            return MOVE_DONE | MOVE_GONE;
        }
        p->moves = (uint8_t)((int8_t)p->moves - 3 < 0 ? 0 : (int8_t)p->moves - 3);
        if (p->type == 15)                            /* bombers end their move */
            p->moves = 0;
        if (imp & IMP_CITY) {
            City *ct;
            c = city_at(x, y);
            ct = &game.s.cities[c];
            if ((int8_t)ct->owner != human)
                ct->status |= CITY_AUTOBUILD;
            /* A city that is attacked shrinks, unless walled or defended from the sea. */
            if (p->type != NONE8 && !(ct->buildings & BIT(B_CITY_WALLS)) && t0 != TERRAIN_OCEAN &&
                !(game.s.difficulty == 0 && owner == human))
                ct->size--;
            if (ct->size == 0) {
                destroy_city(c, x, y);
                civ_check_destroyed(owner, civ);
                if (civ == human && (top = unit_at(x, y)) != -1)
                    U(owner, top)->visible |= 1 << human;
            }
            if (civ == human || owner == human || show_all) {
                ct->visible_size = ct->size;
                square_redraw(x, y);
            }
            if (map_unit_owner(x, y) + 1 == 0 && owner != human)
                ai_rechoose_production(owner, map_continent(x, y));
        }
        return MOVE_DONE;
    }

step:
    /* Ships stay at sea (except in cities); land units may not enter foreign cities. */
    if (t1 != TERRAIN_OCEAN && !(imp & IMP_CITY) && UT(p->type)->domain == DOMAIN_SEA) {
        p->goto_x = NONE8;
        return MOVE_DONE;
    }
    if (UT(p->type)->domain != DOMAIN_LAND && (imp & IMP_CITY) && map_unit_layer_owner(x, y) != civ) {
        if (p->type == UNIT_NUCLEAR) {
            delete_unit(civ, u);
            nuclear_attack(civ, x, y);
            return MOVE_DONE;
        }
        ui_help_message("*OCCUPY");
        p->goto_x = NONE8;
        return MOVE_DONE;
    }
    if (p->moves == 0)
        return MOVE_DONE;

    /* Movement cost. */
    if (UT(p->type)->domain == DOMAIN_LAND) {
        if ((map_improvements(x0, y0) & imp & IMP_ROAD) && p->moves != 0) {
            if ((map_improvements(x0, y0) & IMP_RAILROAD) && civ == human && p->goto_x == NONE8)
                ;                                     /* railroad: free (human, no goto) */
            else
                p->moves--;
        } else if (p->moves != 3 && rnd(terrain_types[t1].move_cost * 3) > (int8_t)p->moves) {
            p->moves = 0;                             /* not enough moves for that terrain */
            return MOVE_DONE;
        } else {
            p->moves -= (uint8_t)(terrain_types[t1].move_cost * 3);
        }
    } else {
        p->moves -= 3;
    }
    if ((int8_t)p->moves < 0)
        p->moves = 0;

    /* Computer players do not walk straight back where they came from. */
    if (civ != human) {
        if (p->unk8 != NONE8 && ((int8_t)p->unk8 ^ 4) == dir && stack_sum_of(civ, u, 2) <= 2) {
            p->unk8 = NONE8;
            p->goto_x = NONE8;
            p->moves = 0;
            return MOVE_DONE;
        }
        p->unk8 = (uint8_t)dir;
    }
    /* ...nor leave a ship with too few escorts. */
    if (top != -1 && civ == owner && civ != human && !(imp & IMP_CITY) &&
        (UT(p->type)->domain == DOMAIN_LAND || (UT(p->type)->domain == DOMAIN_AIR && p->special != 0)) &&
        stack_sum_of(civ, top, 2) >= (p->moves ? 4 : 2))
        return MOVE_DONE;

    /* Crossing into someone's land. */
    n = map_pixel(x + 80, y);
    if (n != 0 && n < 8 && t1 != TERRAIN_OCEAN && t0 != TERRAIN_OCEAN && p->type < UNIT_DIPLOMAT &&
        (imp & 0x0F) && n != civ && (civ == human || n == human) && UT(p->type)->domain == DOMAIN_LAND &&
        map_unit_owner(x, y) != civ) {
        if (civ == human) {
            p->goto_x = NONE8;
            if (confirm_break_treaty(n) + 1 == 0)
                return MOVE_DONE;
            diplomacy_clear(civ, n, DIP_PEACE);
        } else if (n == human && (DIP(n, civ) & DIP_PEACE)) {
            p->moves = 0;
            return MOVE_DONE;
        }
    }
    if (n != 0 && n < 8 && civ != n && (map_improvements(x, y) & 0x0E))
        p->visible |= 1 << n;

    owner = map_unit_layer_owner(x, y);
    if (civ == 0 && t1 != TERRAIN_OCEAN && (game.s.visibility[x0][y0] & (1 << human)) &&
        !(p->visible & (1 << human)) && (int8_t)game.s.cities[nearest_city(x0, y0, NULL)].owner == human)
        p->visible |= game.s.visibility[x0][y0];
    if ((imp & IMP_CITY) && owner != civ && (game.s.visibility[x][y] & (1 << human)) &&
        ((DIP(human, civ) & DIP_EMBASSY) || (DIP(human, owner) & DIP_EMBASSY)))
        p->visible |= 1 << human;
    if (t1 == TERRAIN_OCEAN && (map_improvements(x, y) & IMP_IRRIGATION))
        p->visible |= 1 << owner;
    if ((p->visible & (1 << human)) || civ == human || (show_all && civ != 0)) {
        if (civ == human || (game.s.unk_19c0 & 32)) {
            view_follow(human, x, y);
            ui_animate_move(civ, u, dir);
        } else {
            terrain_redraw(x0, y0);
        }
    }

    /* The move itself, with any cargo. */
    carried = (int8_t)p->next;
    unit_remove_from_square(civ, u, x0, y0);
    unit_place(civ, u, x, y);
    p->x = (uint8_t)x;
    p->y = (uint8_t)y;
    p->visible = 0;
    if (civ != human && UT(p->type)->domain != DOMAIN_AIR)
        p->status &= UNIT_GOTO | UNIT_VETERAN;
    if (n != 0 && n < 8 && n != civ)
        map_put_pixel(x + 80, y, 0);                  /* the land claim is broken */
    if (p->goto_x == x && p->goto_y == y) {
        p->unk8 = NONE8;
        p->goto_x = NONE8;
        if (civ != human)
            p->moves = 0;
    }
    if (t0 == TERRAIN_OCEAN && t1 != TERRAIN_OCEAN && UT(p->type)->domain != DOMAIN_AIR)
        p->moves = 0;                                 /* landing ends the move */
    if ((UT(p->type)->capacity || p->type == UNIT_CARRIER) && carried != -1) {
        int moved = 0, nn;
        for (k = 0; k < 20 && carried != -1; k++) {
            Unit *q = U(civ, carried);
            nn = (int8_t)q->next;
            if ((!UT(p->type)->capacity) == UT(q->type)->domain &&
                ((q->status & UNIT_SENTRY) || t0 == TERRAIN_OCEAN ||
                 (civ != human && !(q->status & (UNIT_FORTIFYING | UNIT_FORTIFIED)) && q->next != NONE8))) {
                unit_remove_from_square(civ, carried, x0, y0);
                unit_place(civ, carried, x, y);
                q->x = (uint8_t)x;
                q->y = (uint8_t)y;
                q->visible = 0;
                q->unk8 = NONE8;
                if (UT(p->type)->capacity)
                    q->status |= UNIT_SENTRY;
                if (UT(q->type)->fuel)
                    q->special = (uint8_t)(UT(q->type)->fuel - 1);
                moved++;
            }
            carried = nn;
            if (carried == -1 || (UT(p->type)->capacity <= moved && p->type != UNIT_CARRIER && civ != 0))
                break;
        }
    }
    if ((imp & IMP_CITY) && owner != civ) {
        p->visible |= 1 << owner;
        city_capture(civ, city_at(x, y), 0);
        map_set_units_present(x, y);
    }
    map_set_unit_layer_owner(civ, x, y);
    unit_reveal(x, y, civ, u, 1);
    if (has_hut(t1, x, y)) {
        enter_hut(civ, u);
        game.s.visibility[x][y] |= 1;
    }
    if (t1 == TERRAIN_OCEAN && UT(p->type)->domain != DOMAIN_SEA)
        stack_foreach(civ, u, cb_reset_ships);        /* boarding: the ship waits */
    if (civ == 0 && t1 != TERRAIN_OCEAN && (game.s.visibility[x][y] & (1 << human)) &&
        !(p->visible & (1 << human)) && (int8_t)game.s.cities[nearest_city(x, y, NULL)].owner == human)
        p->visible |= game.s.visibility[x][y];
    if ((p->visible & (1 << human)) || civ == human || (show_all && civ != 0)) {
        if (civ == human || (game.s.unk_19c0 & 32)) {
            view_follow(human, x, y);
            redraw_unit_square(civ, u);
        } else {
            redraw_unit_square(civ, u);
        }
    }
    flags |= MOVE_DONE | MOVE_MOVED;
    /* Aircraft land in cities and on carriers. */
    if ((imp & IMP_CITY) && UT(p->type)->fuel) {
        p->status &= ~UNIT_ROAD;
        p->moves = 0;
        p->goto_x = NONE8;
    }
    if (UT(p->type)->fuel && UT(p->type)->domain == DOMAIN_AIR && stack_count_type(civ, u, UNIT_CARRIER)) {
        p->status &= ~UNIT_ROAD;
        p->moves = 0;
        p->special = (uint8_t)UT(p->type)->fuel;
    }
    return flags;
}

int inciting;              /* g_28a4 */
int view_x, view_y;        /* g_e3ec / g_e67e: top-left square of the map view */
int fast_view;             /* g_c198: no animations (1) */
int (*ai_unit_command_hook)(int civ, int unit);
int ui_activate_unit;
int midturn_loaded;        /* g_ee80: a game loaded mid-turn keeps its movement points */

static int advised;         /* l8: a hint was given this turn */
static int asked;           /* l6: some unit waited for the player's orders */
char ui_rplc[2][40];        /* $RPLC1 / $RPLC2 in the next message (g_3068 ...) */

/* o_04_00af: hints for the human's settlers (they only show with help enabled, but the
 * view moves first, which takes a random number). */
static void settler_advice(int civ, int u)
{
    Unit *p = U(civ, u);
    int t = map_terrain(p->x, p->y), good, best, k, c, d, imp;

    good = terrain_types[t].food + terrain_types[t].shields >= 2;
    best = map_pixel(p->x + 80, p->y);
    if (best < 10)
        good = 0;
    for (k = 1; k <= 8; k++)
        if (map_pixel(p->x + around_dx[k] + 80, p->y + around_dy[k]) > best)
            good = 0;
    if (good) {
        view_center(game.s.human_civ, p->x - 7, p->y - 6);
        ui_help_message("*BUILDCITY");
        return;
    }
    c = nearest_city(p->x, p->y, &d);
    if (c == -1 || (int8_t)game.s.cities[c].owner != civ || d != 1)
        return;
    imp = map_improvements(p->x, p->y);
    snprintf(ui_rplc[0], sizeof ui_rplc[0], "%s", game.s.city_names[game.s.cities[c].name]);
    if (t == TERRAIN_HILLS && !(imp & (IMP_IRRIGATION | IMP_MINE)))
        ui_help_message("*MINING");
    if (t == TERRAIN_PLAINS && water_near(p->x, p->y) && !(imp & (IMP_IRRIGATION | IMP_MINE))) {
        ui_help_message("*IRRIGATE");
        return;
    }
    if (terrain_types[t].move_cost == 1 && !(imp & IMP_ROAD) && t != TERRAIN_RIVER &&
        game.s.units_active[civ][UNIT_SETTLERS] < 2)
        ui_help_message("*ROAD");
}

/* The human's next order for unit u: standing orders (roads, irrigation, mines, fortress,
 * cleaning up, a goto) carry on by themselves; otherwise the player decides. */
static int human_unit_command(int civ, int u)
{
    Unit *p = U(civ, u);
    int cmd;

    if (!(p->status & (UNIT_ROAD | UNIT_IRRIGATE | UNIT_MINE)) && p->goto_x == NONE8) {
        ui_unit_panel(civ, u);              /* (f_0402_4010, before any advice) */
        if (!advised && game.s.turn == 0) {
            snprintf(ui_rplc[0], sizeof ui_rplc[0], "%s", game.s.civ_names[civ]);
            ui_help_message("*FIRSTMOVE");
            advised = 1;
        }
        if (!advised && p->type == UNIT_SETTLERS) {
            settler_advice(civ, u);
            advised = 1;
        }
        asked = 1;
        terrain_redraw(p->x, p->y);          /* (f_0860_0aa8 blinks the unit: its square is drawn) */
        return ui_human_command(civ, u);
    }
    cmd = 'r';
    if (p->status & UNIT_IRRIGATE)
        cmd = p->type != UNIT_SETTLERS ? 'm' : 'i';
    if (p->status & UNIT_MINE) {
        cmd = 'm';
        if (p->status & UNIT_IRRIGATE)
            cmd = 'f';
        if (p->status & UNIT_ROAD)
            cmd = 'p';
    }
    if (UT(p->type)->domain == DOMAIN_AIR)
        cmd = 'h';
    return cmd;
}

/* f_0402_000a: `civ` moves its units.  Computer players decide with ai_unit_command();
 * the human player's orders come from the user interface. */
void civ_move_units(int civ)
{
    SaveData *s = &game.s;
    int human = s->human_civ, u, l30, l40, l46 = 0, l70 = 127, revealed = 0, tries, cmd, flags, nxt, k, land;
    int ai = !(s->human_mask & (1 << civ));

    cur_player = civ;
    advised = asked = 0;
    if (civ == human) {
        s->human_mask = (int16_t)(1 << human);
        ai = 0;
        if (s->turn == 20 || s->turn == 60)
            ui_help_message("*HELP1");
        if (s->turn == 40 || s->turn == 80)
            ui_help_message("*HELP2");
    }
    for (u = 0; u < MAX_UNITS; u++) {
        Unit *p = U(civ, u);
        if (p->type == NONE8 || midturn_loaded == 1)
            continue;
        p->moves = (uint8_t)(UT(p->type)->moves * 3);
        if (UT(p->type)->domain == DOMAIN_SEA &&
            (civ_has_wonder(civ, WONDER_LIGHTHOUSE) || civ_has_wonder(civ, WONDER_MAGELLAN)))
            p->moves += 3;
    }
    do {
        l30 = 1;
        for (u = 0; u < MAX_UNITS; u++) {
            Unit *p;
            if (quit_flag)
                break;
            if (u == l70)
                l46 = 1;
            p = U(civ, u);
            if (p->type == NONE8)
                continue;
            if (p->type >= NUM_UNIT_TYPES)
                p->type = 1;
            if (!revealed)
                unit_reveal(p->x, p->y, civ, u, 0);
            if (p->status & (UNIT_SENTRY | UNIT_FORTIFIED))
                continue;
            if (p->moves == 0)
                continue;
            if (p->status & UNIT_FORTIFYING) {
                int c;
                p->status = (uint8_t)((p->status & ~UNIT_FORTIFYING) | UNIT_FORTIFIED);
                p->moves = 0;
                redraw_unit_square(civ, u);
                if (civ != human && (map_improvements(p->x, p->y) & IMP_CITY)) {
                    c = city_at(p->x, p->y);
                    if (game.s.cities[c].size >= 3)
                        p->home_city = (uint8_t)c;
                }
                /* (enemy_adjacent called with two arguments again: the third is the turn
                 * loop's SI, &treasury[civ] for computer players and civ*2 for the human) */
                if ((map_improvements(p->x, p->y) & IMP_CITY) && (c = city_at(p->x, p->y)) == (int8_t)p->home_city &&
                    !enemy_adjacent(p->x, p->y, civ == human ? civ * 2 : (int16_t)(0xC0F6 + civ * 2)))
                    city_pack_fortified(c);
                continue;
            }
            if (!ai && !(p->status & (UNIT_ROAD | UNIT_IRRIGATE | UNIT_MINE)) &&
                !(view_x < p->x && p->x <= view_x + 14 && view_y < p->y && p->y < view_y + 10)) {
                /* The human's units on screen come first; then the view goes to the others. */
                if (!l46) {
                    l30 = 0;
                    continue;
                }
                view_center(civ, p->x - 7, p->y - 6);
                l70 = (u - 1) & 127;
                l46 = 0;
            }
            tries = 0;
            l40 = 0;
            for (;;) {
                if (!ai) {
                    cmd = human_unit_command(civ, u);
                    if (quit_flag)
                        break;
                    if (cmd == CMD_ACTIVATE) {
                        u = ui_activate_unit;
                        p = U(civ, u);
                        l70 = (u - 1) & 127;
                        l46 = 0;
                        l30 = 0;
                        if (p->moves > 0 && !l40)
                            continue;
                        break;
                    }
                } else {
                    cmd = ai_unit_command_hook ? ai_unit_command_hook(civ, u) : ai_unit_command(civ, u);
                    if (cmd != 0)
                        p->goto_x = NONE8;
                    if (++tries > 4) {
                        cmd = ' ';
                        p->unk8 = NONE8;
                        p->goto_x = NONE8;
                    }
                }
                if (p->goto_x != NONE8)
                    cmd = goto_step(civ, u);
                else
                    p->unk8 = NONE8;
                nxt = u;
                flags = unit_command(civ, u, cmd, &nxt);
                if (flags & MOVE_MOVED)
                    tries = 0;
                if (flags & (MOVE_END_UNIT | MOVE_RESCAN))
                    l40 = 1;
                if (flags & MOVE_RESCAN)
                    l46 = 1;
                if (flags & MOVE_JUMP) {
                    l30 = 0;
                    u = nxt - 1;                  /* (the end-of-unit checks below see unit nxt-1) */
                    l70 = u & 127;
                    l46 = 0;
                }
                if (u < MAX_UNITS && U(civ, u)->moves > 0 && !l40 && !quit_flag)
                    continue;
                break;
            }
            if (u >= MAX_UNITS || u < 0)
                continue;
            p = U(civ, u);
            if (p->type == NONE8)
                continue;
            if (p->moves > 0) {
                l30 = 0;
                continue;
            }
            if (UT(p->type)->fuel) {
                if ((map_improvements(p->x, p->y) & IMP_CITY) || stack_count_type(civ, u, UNIT_CARRIER))
                    p->special = (uint8_t)UT(p->type)->fuel;
                p->special--;
                if ((int8_t)p->special < 0) {
                    delete_unit(civ, u);
                    ui_help_message("*FUEL");
                }
            }
            if (p->type == UNIT_TRIREME && civ == human && rnd(2) != 0) {
                land = 0;
                for (k = 1; k <= 8; k++)
                    if (map_terrain(p->x + around_dx[k], p->y + around_dy[k]) != TERRAIN_OCEAN) {
                        land = 1;
                        break;
                    }
                if (!land) {
                    delete_unit(civ, u);
                    ui_help_message("*TRIREME");
                }
            }
        }
        /* "End of Turn": the player looks around (and may wake units) before going on;
         * the original waits when no unit needed orders, or always with the option on. */
        if (l30 != 0 && !quit_flag && civ == human && (s->human_mask & (1 << civ)) &&
            (!asked || (s->unk_19c0 & 4)) && ui_end_of_turn(civ)) {
            l30 = 0;
            l46 = 1;
            asked = 1;
        }
    } while (l30 == 0 && !quit_flag);
    if (civ == human)
        ui_unit_panel(civ, -1);                     /* (L3e56: the panel is emptied) */
    (void)revealed;
    (void)l46;
}
