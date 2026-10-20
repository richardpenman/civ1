/* Computer players' orders for single units (original seg 15da: f_15da_0c76 and the
 * barbarian variant f_15da_3aa8).  The result is a command for unit_command(): a
 * direction 1..8, a letter ('b' found or join a city, 'f' fortify, 'h' home, 'i' irrigate,
 * 'm' mine, 'p' clean up pollution, 'P' pillage, 'r' road, 'u' unload, ' ' skip a turn) or
 * 0 to keep following the goto. */
#include <stdlib.h>
#include "game.h"
#include "civ.h"
#include "city.h"
#include "map.h"
#include "unit.h"
#include "util.h"
#include "ai.h"
#include "combat.h"
#include "move.h"
#include "ui.h"
#include "view.h"
#include <stdio.h>

#define DIP(a, b) game.s.diplomacy[a][b]
#define CITY(i)   (&game.s.cities[i])

uint8_t ai_block_flags[260];     /* far 2786:1498, 20 x 13 blocks of 4x4 squares (ai_strategy) */

/* A byte of far segment 2786 around the block table, which the AI indexes without bounds. */
static int far_2786_byte(int off)
{
    if (off >= 0xfe8 && off < 0x1498)
        return game.s.unk_far_2786_0fe8[off - 0xfe8];
    if (off >= 0x1498 && off < 0x159c)
        return ai_block_flags[off - 0x1498];
    if (off >= 0x159c && off < 0x175c)
        return ((const uint8_t *)game.s.units_lost)[off - 0x159c];
    return 0;
}

static int block_flags(int bx, int by)
{
    return far_2786_byte(0x1498 + bx * 13 + by);
}

/* The type byte read signed (most places) or unsigned (a few). */
#define STYPE(p) ((int8_t)(p)->type)
#define UTYPE(p) ((int)(p)->type)

/* 1 << n computed in a byte register (`shl al, cl`). */
static int bit8(int n)
{
    n &= 31;
    return n < 8 ? (1 << n) & 0xFF : 0;
}

/* Strategy of `civ` for continent `cont` (unk_f5b8: 0 settle, 1 attack, 2 defend, 5 ...). */
static int policy(int civ, int cont)
{
    return ((const int16_t *)game.s.unk_f5b8)[civ * 16 + cont];
}

#define CONT_WORD(arr, civ, cont) (((const int16_t *)(arr))[(civ) * 16 + (cont)])

/* terrain_improve[t].unk4, or .unk5 once the civilization knows Monarchy (sic). */
static int improve_worth(int civ, int t)
{
    return civ_has_advance(civ, ADV_MONARCHY) ? terrain_improve[t].unk5 : terrain_improve[t].unk4;
}

/* f_15da_377a */
static void ai_shift_spots(int civ, int from)
{
    int i;

    for (i = 14; i >= from; i--) {
        if (game.s.ai_spot_value[civ][i] == 0)
            continue;
        game.s.ai_spot_x[civ][i + 1] = game.s.ai_spot_x[civ][i];
        game.s.ai_spot_y[civ][i + 1] = game.s.ai_spot_y[civ][i];
        game.s.ai_spot_type[civ][i + 1] = game.s.ai_spot_type[civ][i];
        game.s.ai_spot_value[civ][i + 1] = game.s.ai_spot_value[civ][i];
    }
}

/* f_15da_36cc: remember a point of interest. */
void ai_add_spot(int civ, int x, int y, int type, int value)
{
    SaveData *s = &game.s;
    int i;

    for (i = 0; i < 16; i++)
        if (s->ai_spot_x[civ][i] == x && s->ai_spot_y[civ][i] == y && s->ai_spot_type[civ][i] == type &&
            (int8_t)s->ai_spot_value[civ][i] <= value)
            return;
    for (i = 0; i < 16; i++) {
        if ((uint8_t)s->ai_spot_type[civ][i] != NONE8 && (int8_t)s->ai_spot_value[civ][i] >= value)
            continue;
        ai_shift_spots(civ, i);
        s->ai_spot_x[civ][i] = (int8_t)x;
        s->ai_spot_y[civ][i] = (int8_t)y;
        s->ai_spot_type[civ][i] = (int8_t)type;
        s->ai_spot_value[civ][i] = (uint8_t)value;
        return;
    }
}

/* f_0402_3f18: what settlers could do to (x, y): 1 irrigate, 2 mine, 0 nothing. */
int ai_terraform_kind(int x, int y)
{
    int t;

    if (map_improvements(x, y) & (IMP_CITY | IMP_IRRIGATION | IMP_MINE))
        return 0;
    t = map_terrain(x, y);
    if (terrain_improve[t].mine <= -3)
        return 2;
    if (terrain_improve[t].irrigate == -2 && water_near(x, y))
        return 1;
    return 0;
}

/* f_15da_399c: the sea square next to (x, y) (or (x, y) itself) from which a transport
 * can unload onto the most free land; -1 if none. */
static int ai_landing_dir(int civ, int x, int y)
{
    int best = -1, best_n = -1, i, k, sx, sy, lx, ly, n;

    for (i = 0; i <= 8; i++) {
        sx = around_dx[i] + x;
        sy = around_dy[i] + y;
        if (map_terrain(sx, sy) != TERRAIN_OCEAN)
            continue;
        if (i != 0 && map_unit_owner(sx, sy) != -1)
            continue;
        n = 0;
        for (k = 1; k <= 8; k++) {
            lx = around_dx[k] + sx;
            ly = around_dy[k] + sy;
            if (map_terrain(lx, ly) == TERRAIN_OCEAN || map_unit_owner(lx, ly) != -1)
                continue;
            n++;
            if ((DIP(civ, game.s.human_civ) & DIP_PEACE) && map_tile_owner(lx, ly) == game.s.human_civ)
                n--;
        }
        if (n > best_n) {
            best_n = n;
            best = i;
        }
    }
    return best;
}

/* f_0860_1059: like stack_best_defender(), but the search starts from the first unit of
 * the civilization on the same square, and any defence beats none. */
int stack_defender_at(int civ, int u)
{
    const Unit *p = unit_ptr(civ, u);
    int i;

    if (p->next == NONE8)
        return u;
    for (i = 0; i < MAX_UNITS; i++) {
        const Unit *o = unit_ptr(civ, i);
        if (o->type == NONE8 || o->x != p->x || o->y != p->y)
            continue;
        return stack_best_defender_from(civ, i, u, 0);
    }
    return u;
}

/* Squares to try next to a unit, as dx/dy pairs (index 1..8). */
#define NX(i) wrap_x(around_dx[i] + x)
#define NY(i) (around_dy[i] + y)

/* f_15da_3aa8 */
static int ai_barbarian_command(int civ, int u)
{
    Unit *p = unit_ptr(civ, u);
    int x, y, type, zoc, i, nx, ny, city, dist, best, best_dir, v, top, t, sx, sy, near;
    char msg[160];

    if (p->y < 2 || p->y >= 48) {
        delete_unit(civ, u);
        return 0;    /* (returns whatever delete_unit left in AX) */
    }
    zoc = in_enemy_zone(civ, p->x, p->y);
    best_dir = 0;
    x = p->x;
    y = p->y;
    type = STYPE(p);

    if (unit_type_word(STYPE(p), UTW_ROLE) == 5) {
        /* A ship full of raiders looks for a beach. */
        if (p->next == NONE8) {
            delete_unit(civ, u);
            return ' ';
        }
        city = nearest_city(x, y, &dist);
        for (i = 1; i <= 8; i++) {
            nx = NX(i);
            ny = NY(i);
            if (dist <= 8 && map_terrain(nx, ny) != TERRAIN_OCEAN && !(map_improvements(nx, ny) & IMP_CITY) &&
                map_unit_owner(nx, ny) == -1 && ny > 1 && ny < 48) {
                if ((game.s.visibility[nx][ny] & (1 << game.s.human_civ)) &&
                    (int8_t)CITY(city)->owner == game.s.human_civ &&
                    map_continent(nx, ny) == map_continent(CITY(city)->x, CITY(city)->y)) {
                    p->visible |= 1 << game.s.human_civ;
                    view_follow(game.s.human_civ, nx, ny);
                    near = nearest_city(nx, ny, &dist);
                    snprintf(msg, sizeof msg, "Barbarian raiding party\nlands near %s!\nCitizens are alarmed.\n",
                             game.s.city_names[CITY(near)->name]);
                    ui_message(msg, 3);
                }
                p->visible |= game.s.visibility[nx][ny];
                p->moves = 0;
                return 'u';
            }
            if (map_unit_owner(nx, ny) > 0)
                return i;
        }
        if (p->goto_x != NONE8)
            return 0;
        /* Sail for the richest city within reach. */
        best = 0;
        for (i = 0; i < MAX_CITIES; i++) {
            if (CITY(i)->status == NONE8)
                continue;
            v = (plunder(i) + 50) / map_distance(x, y, CITY(i)->x, CITY(i)->y);
            if (v > best) {
                best = v;
                city = i;
            }
        }
        if (best == 0) {
            delete_unit(civ, u);
            return ' ';
        }
        p->goto_x = CITY(city)->x;
        p->goto_y = CITY(city)->y;
        return 0;
    }

    {
        int tdx = -2, tdy = -2;
        static const City no_city;    /* city[-1] when there are no cities: unknown memory */
        const City *cp;

        city = nearest_city(x, y, &dist);
        cp = city >= 0 ? CITY(city) : &no_city;
        if (dist == 0 && (p->next == NONE8 || unit_type_word(STYPE(p), UTW_ROLE) == 2))
            return 'f';
        if (((u + game.s.turn) & 3) == 0) {
            if (map_continent(x, y) != map_continent(cp->x, cp->y) || dist > 8) {
                delete_unit(civ, u);
                return ' ';
            }
        }
        if (city != -1 && map_continent(x, y) == map_continent(cp->x, cp->y) &&
            (game.s.human_mask & (1 << cp->owner))) {
            tdx = sign(cp->x - x);
            tdy = sign(cp->y - y);
        }
        if (civ == (int8_t)cp->owner ||
            (game.s.city_count[(int8_t)cp->owner] < 2 && game.s.treasury[(int8_t)cp->owner] < 100)) {
            tdx = -2;
            tdy = -2;
        }
        if (tdx != -2) {
            p->goto_x = cp->x;
            p->goto_y = cp->y;
            p->unk8 = (uint8_t)goto_step(civ, u);
        }
        p->goto_x = NONE8;

        if (STYPE(p) == UNIT_DIPLOMAT) {
            /* The barbarian leader stays with his men, or runs. */
            t = map_terrain(p->x, p->y);
            if (p->next != NONE8 && t != TERRAIN_OCEAN && unit_ptr(civ, p->next)->type != UNIT_DIPLOMAT)
                return ' ';
            v = dist;                                     /* distance to the nearest city */
            if (t == TERRAIN_OCEAN) {
                if (v < 3) {
                    delete_unit(civ, u);
                    return ' ';
                }
            } else {
                near = nearest_unit(civ, u, p->x, p->y, &dist);
                if (dist <= 3 && unit_ptr(civ, near)->type != UNIT_DIPLOMAT) {
                    p->goto_x = unit_ptr(civ, near)->x;
                    p->goto_y = unit_ptr(civ, near)->y;
                    return 0;
                }
                if ((game.s.turn & 7) + 4 <= v) {
                    delete_unit(civ, u);
                    return ' ';
                }
            }
        }

        best = -999;
        for (i = 1; i < 9; i++) {
            int score;
            nx = wrap_x(p->x + around_dx[i]);
            ny = p->y + around_dy[i];
            t = map_terrain(nx, ny);
            if ((t == TERRAIN_OCEAN) != (unit_type_word(UTYPE(p), UTW_DOMAIN) == DOMAIN_SEA))
                continue;
            if (!map_in_bounds(nx, ny))
                continue;
            top = unit_at(nx, ny);
            if (p->type == UNIT_DIPLOMAT) {
                score = 0;
                if (top != -1)
                    score += civ == stack_owner ? 99 : -99;
                nearest_city(nx, ny, &dist);
                score += (int8_t)terrain_types[t].move_cost + rnd(4) + dist * 4;
            } else {
                if ((map_improvements(nx, ny) & IMP_CITY) && map_unit_layer_owner(nx, ny) != civ) {
                    if (map_terrain(x, y) == TERRAIN_OCEAN)
                        continue;
                    return i;
                }
                sx = rnd(6);
                sy = (map_improvements(nx, ny) & (IMP_IRRIGATION | IMP_MINE)) ? 6 : 0;
                score = (map_unit_layer_owner(nx, ny) == civ ? 0 : 4) + sy + sx;
                if (top != -1) {
                    if (civ == stack_owner) {
                        if (map_terrain(x, y) != TERRAIN_OCEAN)
                            continue;
                        score -= 20;
                    } else {
                        if (map_terrain(x, y) == TERRAIN_OCEAN)
                            continue;
                        if (unit_ptr(stack_owner, top)->type != UNIT_DIPLOMAT)
                            score += 99;
                    }
                }
                if (zoc && unit_type_word(STYPE(p), UTW_DOMAIN) == DOMAIN_LAND && top == -1 && in_enemy_zone(civ, nx, ny))
                    continue;
                if ((int8_t)p->unk8 == i)
                    score += 6;
                if (around_dx[i] == tdx)
                    score += 2;
                if (around_dy[i] == tdy)
                    score += 2;
            }
            if (score > best) {
                best = score;
                best_dir = i;
            }
        }
        if ((map_improvements(x, y) & IMP_CITY) && p->next == NONE8 && best < 99)
            return ' ';
        p->unk8 = (uint8_t)best_dir;
        (void)type;
        return best_dir;
    }
}

/* f_15da_0c76 */
int ai_unit_command(int civ, int u)
{
    SaveData *s = &game.s;
    Unit *p = unit_ptr(civ, u);
    int human = s->human_civ;
    int zoc, best_dir, x, y, type, city, city_dist, unit_dist, terrain, role, cont, pol, civ_bit;
    int i, k, d, best, target, nx, ny, top, v, n, mask = 0, owner, t;

    if (civ == 0)
        return ai_barbarian_command(civ, u);
    if (p->type == 15 && (int8_t)p->special <= 0)     /* bomber out of fuel */
        return 'h';

    if (p->type == 14 && p->goto_x == NONE8) {        /* fighter: intercept the human's bombers */
        int last = 0;
        if (unit_type_word(STYPE(p), UTW_MOVES) / 2 * 3 >= (int8_t)p->moves)
            return 'h';
        best = 999;
        target = -1;
        for (i = 0; i < MAX_UNITS; i++) {
            const Unit *b = unit_ptr(human, i);
            if (b->type != 15 || !((int8_t)b->visible & (1 << civ)))
                continue;
            last = map_distance(p->x, p->y, b->x, b->y);
            if (last < best) {
                best = last;
                target = i;
            }
        }
        if (target != -1 && last > 1) {
            const Unit *b = unit_ptr(human, target);
            if (unit_type_word(STYPE(p), UTW_MOVES) > best) {
                p->goto_x = b->x;
                p->goto_y = b->y;
            } else {
                int c = -1;
                best = 999;
                for (i = 0; i < MAX_CITIES; i++) {
                    if (CITY(i)->status == NONE8 || (int8_t)CITY(i)->owner != civ)
                        continue;
                    if (unit_type_word(STYPE(p), UTW_MOVES) < map_distance(p->x, p->y, CITY(i)->x, CITY(i)->y))
                        continue;
                    d = map_distance(b->x, b->y, CITY(i)->x, CITY(i)->y);
                    if (d < best) {
                        best = d;
                        c = i;
                    }
                }
                if (c == -1)
                    return ' ';
                p->goto_x = CITY(c)->x;
                p->goto_y = CITY(c)->y;
            }
        }
    }

    zoc = in_enemy_zone(civ, p->x, p->y);
    best_dir = 0;
    x = p->x;
    y = p->y;
    type = STYPE(p);
    city = nearest_city(x, y, &city_dist);
    p->type = NONE8;
    nearest_unit(civ, u, x, y, &unit_dist);
    p->type = (uint8_t)type;
    terrain = map_terrain(x, y);
    role = unit_type_word(STYPE(p), UTW_ROLE);
    cont = map_continent(x, y);
    pol = policy(civ, cont);
    civ_bit = 1 << civ;

    if (type == UNIT_NUCLEAR) {
        int score, c2, near_own;
        best = -1;
        for (i = 0; i < MAX_CITIES; i++) {
            const City *c = CITY(i);
            score = owner = (int8_t)c->owner;
            if (c->status == NONE8 || (c->buildings & BIT(B_SDI)))
                goto consider;
            {
                int mine = s->military_power[civ], theirs = s->military_power[owner] * 2;
                if (mine * 3 >= theirs && !(DIP(civ, owner) & 8)) {
                    if (s->units_active[owner][UNIT_NUCLEAR] != 0 || mine >= theirs)
                        goto consider;
                }
            }
            if ((DIP(civ, owner) & 0x82) != 0x80 || (int8_t)c->size <= 4)
                goto consider;
            score = 0;
            for (k = 0; k <= 8; k++) {
                int o = map_unit_owner(c->x + around_dx[k], c->y + around_dy[k]);
                if (o == -1)
                    continue;
                if ((DIP(civ, o) & 0x82) == 0x80)
                    score++;
                else if (o == civ)
                    score -= 2;
                else
                    score -= 99;
            }
            score += (int8_t)((int8_t)c->size / 2);
        consider:
            if (score <= best)
                continue;
            near_own = 0;
            for (c2 = 0; c2 < MAX_CITIES; c2++) {
                if (CITY(c2)->status == NONE8 || (int8_t)CITY(c2)->owner != civ)
                    continue;
                if (map_distance(c->x, c->y, CITY(c2)->x, CITY(c2)->y) <= 16) {
                    near_own = 1;
                    break;
                }
            }
            if (near_own) {
                best = score;
                city = i;
            }
        }
        if (best >= 10 && s->units_active[civ][UNIT_NUCLEAR] > 1) {
            p->goto_x = CITY(city)->x;
            p->goto_y = CITY(city)->y;
            for (i = 1; i <= 8; i++) {
                nx = p->goto_x + around_dx[i];
                ny = p->goto_y + around_dy[i];
                if (map_unit_owner(nx, ny) != -1)
                    continue;
                unit_remove_from_square(civ, u, x, y);
                p->x = (uint8_t)nx;
                p->y = (uint8_t)ny;
                unit_place(civ, u, nx, ny);
                if ((int8_t)CITY(city)->owner == human)
                    s->unk_e204[civ] = -2;
                return ((i - 1) ^ 4) + 1;
            }
        }
        return ' ';
    }

    if (type == UNIT_DIPLOMAT) {
        if (p->goto_x != NONE8)
            return 0;
        if ((int8_t)CITY(city)->owner != civ) {
            p->goto_x = CITY(city)->x;
            p->goto_y = CITY(city)->y;
            return 0;
        }
        /* At home: disbanded.  The original carries on with the dead unit (sic). */
        delete_unit(civ, u);
    }

    if (role == 0)
        ai_forget_spot(civ, 0, x, y, 0);

    /* Exploring the best land nearby; settlers found cities on good ground. */
    best = map_pixel(x + 80, y);
    if (best != 0 && role < 3) {
        best_dir = 0;
        for (i = 1; i <= 8; i++) {
            nx = NX(i);
            ny = NY(i);
            v = map_pixel(nx + 80, ny);
            if (v > best) {
                best = v;
                best_dir = i;
            }
            if (has_hut(map_terrain(nx, ny), nx, ny) && role != 0)
                return i;
        }
        if (s->turn == 0 && earth_map) {
            best = 15;
            best_dir = 0;
        }
        if (best >= 8 && 15 - city_dist <= best && !zoc) {
            if (role == 0)
                return best_dir == 0 ? 'b' : best_dir;
            ai_add_spot(civ, x, y, 0, 2);
        }
    }

    /* Settlers crowd the human's land when they are behind in science. */
    if (role == 0 && s->difficulty != 0 && human == (int8_t)CITY(city)->owner && !zoc && city_dist > 1 &&
        civ != (int8_t)CITY(city)->owner && s->advances_known[civ] < s->advances_known[human]) {
        v = map_pixel(x + 80, y + 50);
        if (v >= 9 && 14 - city_dist <= v)
            return 'b';
    }

    if (map_improvements(x, y) & IMP_CITY) {
        if (role == 2) {
            int saved, def, need;
            if (p->next == NONE8)
                return 'f';
            saved = STYPE(p);
            p->type = UNIT_DIPLOMAT;
            def = stack_defender_at(civ, u);
            p->type = (uint8_t)saved;
            if (def != -1 && unit_type_word(STYPE(unit_ptr(civ, def)), UTW_DEFENSE) < unit_type_word(saved, UTW_DEFENSE))
                return 'f';
            k = enemy_in_zone(civ, x, y) ? 3 : 4;
            if (CITY(city)->buildings & BIT(B_PALACE))
                k--;
            else if (pol == 0)
                k++;
            n = stack_count_role(civ, u, 2);
            need = (int8_t)CITY(city)->size / k + 1;
            if (need > n)
                ai_add_goal(civ, x, y, 2, 2);
            if (need >= n)
                return 'f';
            saved = p->status;
            p->status |= UNIT_FORTIFIED;
            if (stack_defender_at(civ, u) == u && need > stack_sum_of(civ, u, 4)) {
                p->status = (uint8_t)saved;
                return 'f';
            }
            p->status = (uint8_t)saved;
            if (((u + s->turn) & 7) == 0)
                p->status &= 0xF2;
        } else if (unit_dist > 0 && role != 0) {
            ai_add_goal(civ, x, y, 2, 4);
            return ' ';
        }
    }

    if (role == 5 || role == 3) {
        int woken = 0, j, steps = 0;
        n = 0;
        /* A broken ring (left by a ship leaving a shared square, see unit_unlink) makes
         * the original loop here for ever; the port gives up. */
        for (j = (int8_t)p->next; j != -1 && j != u && steps++ < 1000; j = (int8_t)unit_ptr(civ, j)->next) {
            Unit *q = unit_ptr(civ, j);
            if (!(q->status & UNIT_FORTIFIED)) {
                mask |= 1 << unit_type_word(STYPE(q), UTW_ROLE);
                if (unit_type_word(STYPE(q), UTW_DOMAIN) == DOMAIN_LAND)
                    n++;
            } else if (pol == 5) {
                /* The original reads city[l12] with l12 never set (stack garbage); the port
                 * takes the nearest city, the one the ship is in (see NOTES.md). */
                if ((int8_t)((int8_t)CITY(city)->size / 5) < woken++) {
                    q->status &= ~UNIT_FORTIFIED;
                    n++;
                }
            }
        }
        if (((city_dist != 0 ? 1 : 3) > n || mask == 1) && role == 5) {
            /* Too little aboard: to the coastal city with the most to carry. */
            int pick = 0;
            best = 999;
            for (i = 0; i < MAX_CITIES; i++) {
                const City *c = CITY(i);
                int cc;
                if (c->status == NONE8 || (int8_t)c->owner != civ || !(c->status & CITY_COASTAL))
                    continue;
                d = map_distance(x, y, c->x, c->y);
                cc = map_continent(c->x, c->y);
                if (CONT_WORD(s->unk_6c2c, civ, cc) < 16)
                    continue;
                top = unit_at(c->x, c->y);
                if (top != -1 && (d == 0) < stack_count_role(civ, top, 5))
                    d += 16;
                if (policy(civ, cc) != 5)
                    d += 16;
                else
                    d -= CONT_WORD(s->unk_8f9a, civ, cc) / 4;
                if (d < best) {
                    best = d;
                    p->goto_x = c->x;
                    p->goto_y = c->y;
                    pick = policy(civ, cc);
                }
            }
            k = pick == 5 ? 4 : 2;
            if (k * 3 >= best) {
                if (pick != 0)
                    ai_add_goal(civ, p->goto_x, p->goto_y, 0, k);
                if (pick != 2)
                    ai_add_goal(civ, p->goto_x, p->goto_y, 2, k);
                if (pick != 1)
                    ai_add_goal(civ, p->goto_x, p->goto_y, 1, k);
            }
            goto after_ships;
        }
        if (role == 5) {
            /* Somewhere to put the passengers ashore? */
            for (i = 1; i <= 8; i++) {
                int o, cc, ok;
                nx = NX(i);
                ny = NY(i);
                o = map_unit_owner(nx, ny);
                if (map_terrain(nx, ny) == TERRAIN_OCEAN || !map_in_bounds(nx, ny))
                    continue;
                if (o != -1 && o != civ)
                    continue;
                if (o == civ && stack_sum_of(civ, unit_at(nx, ny), 2) >= 2)
                    continue;
                if ((DIP(civ, human) & DIP_PEACE) && map_unit_layer_owner(nx, ny) == human &&
                    (map_improvements(nx, ny) & 15))
                    continue;
                cc = map_continent(nx, ny);
                ok = 0;
                if (CONT_WORD(s->unk_6c2c, civ, cc) >= 16 || policy(civ, cc) == 5) {
                    if (bit8(policy(civ, cc)) & mask & 6)
                        ok = 1;
                } else
                    ok = 1;
                if (p->goto_x != NONE8 && map_continent(p->goto_x, p->goto_y) == cc)
                    ok = 1;
                if (ok && s->unk_c10e[cc] >= 5 && ny > 1 && ny < 48) {
                    if (p->moves > 3) {
                        d = ai_landing_dir(civ, x, y);
                        if (d > 0)
                            return d;
                    }
                    if (policy(civ, cc) == 1)
                        ai_add_goal(civ, x, y, 3, 5);
                    p->moves = 0;
                    p->goto_x = NONE8;
                    return 'u';
                }
                if ((s->turn & 3) == 0)
                    p->goto_x = NONE8;
                break;
            }
            if (p->moves <= 3) {
                nx = (p->x + p->goto_x) >> 1;
                ny = (p->y + p->goto_y) >> 1;
                if (p->goto_x != NONE8 && map_terrain(nx, ny) == TERRAIN_OCEAN)
                    ai_add_goal(civ, nx, ny, 3, 3);
            }
        }
        if (zoc && role != 5)
            goto after_ships;
        if (p->goto_x == NONE8 && ((mask & 2) || !(mask & 1)) && p->type != UNIT_TRIREME) {
            /* Warships and empty transports: to the nearest city not at peace with us. */
            best = 999;
            for (i = s->turn & 7; i < MAX_CITIES; i += 8) {
                const City *c = CITY(i);
                owner = (int8_t)c->owner;
                if (c->status == NONE8 || owner == civ)
                    continue;
                if ((DIP(civ, owner) & 0x102) == 2)
                    continue;
                if (s->civ_rank[human] >= 7 && owner != human)
                    continue;
                d = map_distance(x, y, c->x, c->y);
                if (d < best) {
                    best = d;
                    p->goto_x = c->x;
                    p->goto_y = c->y;
                }
            }
        }
        if (p->goto_x == NONE8 && p->type != UNIT_TRIREME) {
            /* Else to an own city on another continent. */
            int here = -1;
            best = 999;
            if (map_terrain(x, y) != TERRAIN_OCEAN)
                here = cont;
            for (i = 0; i < MAX_CITIES; i++) {
                const City *c = CITY(i);
                int cc;
                if (c->status == NONE8 || (int8_t)c->owner != civ)
                    continue;
                d = map_distance(x, y, c->x, c->y);
                if (d < 8)
                    continue;
                cc = map_continent(c->x, c->y);
                if (cc == here || policy(civ, cc) == 5)
                    continue;
                if (!(bit8(policy(civ, cc)) & mask & 7))
                    d <<= 1;
                if (d < best) {
                    best = d;
                    p->goto_x = c->x;
                    p->goto_y = c->y;
                }
            }
        }
        if (p->goto_x == NONE8) {
            /* Else explore: some unclaimed land up to 23 squares away. */
            for (k = 2; k < 24; k++) {
                int r = rnd(9);
                nx = wrap_x(around_dx[r] * k + p->x);
                ny = around_dy[r] * k + p->y;
                if (ny <= 2 || ny >= 47 || map_terrain(nx, ny) == TERRAIN_OCEAN)
                    continue;
                if (CONT_WORD(s->unk_6b16, civ, map_continent(nx, ny)) != 0)
                    continue;
                p->goto_x = (uint8_t)nx;
                p->goto_y = (uint8_t)ny;
                break;
            }
        }
    }
after_ships:

    /* Slow units next to a human city at war pillage. */
    if (unit_type_word(STYPE(p), UTW_MOVES) < 2 && city_dist < 4 && (int8_t)CITY(city)->owner == human &&
        !(DIP(human, civ) & DIP_PEACE) && (map_improvements(x, y) & (IMP_IRRIGATION | IMP_MINE)) &&
        (int8_t)CITY(city)->owner != civ)
        return 'P';

    if (role == 0) {
        if (terrain == TERRAIN_OCEAN)
            goto wander;
        if (unit_dist >= 2)
            ai_add_goal(civ, x, y, 2, 2);
        if ((improve_worth(civ, terrain) != 0 || pol == 5) && city_dist > 0 && city_dist <= 2 &&
            (int8_t)CITY(city)->owner == civ &&
            ((int8_t)CITY(city)->size >= 3 || terrain != TERRAIN_HILLS || has_special_resource(x, y)) &&
            (s->rules_flags & 2)) {
            int imp;
            k = ai_terraform_kind(x, y);
            if (k == 1) {
                p->goto_x = NONE8;
                return 'i';
            }
            if (k == 2) {
                p->goto_x = NONE8;
                return 'm';
            }
            imp = map_improvements(x, y);
            if ((imp & (IMP_IRRIGATION | IMP_MINE)) && !(imp & IMP_ROAD) && terrain <= TERRAIN_GRASSLAND) {
                p->goto_x = NONE8;
                return 'r';
            }
            if ((imp & (IMP_ROAD | IMP_RAILROAD)) != (IMP_ROAD | IMP_RAILROAD) && civ_has_advance(civ, ADV_RAILROAD)) {
                v = tile_yield_gov(calc_city, cur_civ, x, y, YIELD_SHIELDS);
                if (((imp & IMP_ROAD) ? 1 : 2) <= v) {
                    p->goto_x = NONE8;
                    return 'r';
                }
            }
        }
        if (map_improvements(x, y) & IMP_POLLUTION)
            return 'p';
        if (p->goto_x != NONE8 &&
            (civ_types[s->civ_type[civ]].unk1 + 1) * s->unk_c10e[cont] > CONT_WORD(s->unk_6b16, civ, cont) << 4)
            return 0;
        if (city_dist <= 1 && (int8_t)CITY(city)->owner == civ && (s->rules_flags & 2)) {
            /* Improve a square next to the city. */
            for (i = 1; i <= 8; i++) {
                k = ((i + s->turn) & 7) + 1;
                nx = around_dx[k] + x;
                ny = around_dy[k] + y;
                top = unit_at(nx, ny);
                if (top != -1 && (civ != stack_owner || stack_count_type(civ, top, UNIT_SETTLERS) != 0))
                    continue;
                t = map_terrain(nx, ny);
                if (improve_worth(civ, t) == 0)
                    continue;
                if (!ai_terraform_kind(nx, ny))
                    continue;
                if ((int8_t)CITY(city)->size < 3 && map_terrain(nx, ny) == TERRAIN_HILLS &&
                    !has_special_resource(nx, ny))
                    continue;
                if (distance(nx - CITY(city)->x, ny - CITY(city)->y) <= 2) {
                    p->goto_x = (uint8_t)nx;
                    p->goto_y = (uint8_t)ny;
                    return 0;
                }
            }
        }
        if (map_improvements(x, y) & IMP_ROAD)
            goto wander;
        if (terrain == TERRAIN_RIVER && !civ_has_advance(civ, 19))     /* Bridge Building */
            goto wander;
        if ((int8_t)CITY(city)->owner != civ)
            goto wander;
        if (city_dist <= 2 && (terrain == TERRAIN_PLAINS || terrain == TERRAIN_GRASSLAND))
            return 'r';
        {
            /* Roads: join up neighbouring road squares. */
            int further = 0, roads = 0, bits = 0;
            for (i = 1; i <= 8; i++) {
                if (!(map_improvements(around_dx[i] + x, around_dy[i] + y) & IMP_ROAD))
                    continue;
                bits |= 1 << (i - 1);
                roads++;
                if (map_improvements(around_dx[i] * 2 + x, around_dy[i] * 2 + y) & IMP_ROAD)
                    further = 1;
            }
            bits &= bits >> 4;
            if (roads < 4 && (bits != 0 || (city_dist == 1 && terrain <= TERRAIN_GRASSLAND)))
                return 'r';
            if (roads == 1 && further && terrain_types[terrain].move_cost == 1)
                return 'r';
        }
    wander:
        if (p->goto_x != NONE8)
            return 0;
        k = rnd(20) + 1;
        if (!(block_flags((p->x >> 2) + around_dx[k], (p->y >> 2) + around_dy[k]) & 3)) {
            p->goto_x = (uint8_t)(around_dx[k] * 4 + p->x);
            p->goto_y = (uint8_t)(around_dy[k] * 4 + p->y);
            if (map_in_bounds(p->goto_x, p->goto_y) && terrain_types[map_terrain(p->goto_x, p->goto_y)].shields != 0 &&
                map_continent(p->goto_x, p->goto_y) == cont)
                return 0;
            p->goto_x = NONE8;
        }
    }

    if (p->goto_x != NONE8 && !(role == 3 && enemy_adjacent(civ, x, y)))
        return 0;

    if (role == 2 && terrain != TERRAIN_OCEAN) {
        if ((unit_dist > 1 && city_dist <= 3 && (int8_t)CITY(city)->owner == civ) || (zoc && unit_dist != 0))
            return 'f';
    }

    if (role == 0 && CONT_WORD(s->unk_6b16, civ, cont) > s->unk_c10e[cont] / 8) {
        if (city_dist == 0 && (int8_t)CITY(city)->size < 10)
            return 'b';                                  /* join the city */
        if ((int8_t)CITY(city)->owner == civ && (int8_t)CITY(city)->size < 10) {
            p->goto_x = CITY(city)->x;
            p->goto_y = CITY(city)->y;
            return 0;
        }
    }

    /* Score the eight neighbouring squares. */
    {
        int16_t score, odds;
        int lo, sq, enemy, best_enemy = 0, free;
        best = -999;
        best_dir = 0;
        free = 0;
        if (enemy_in_zone(civ, x, y))
            p->unk8 = NONE8;
        else
            free = 1;
        if (type == 1 && pol == 1)                      /* militia on an attack continent */
            role = 1;
        for (i = 1; i <= 8; i++) {
            nx = NX(i);
            ny = NY(i);
            if (!map_in_bounds(nx, ny))
                continue;
            lo = map_unit_layer_owner(nx, ny);
            sq = map_terrain(nx, ny);
            if (sq == TERRAIN_OCEAN && unit_type_word(STYPE(p), UTW_DOMAIN) == DOMAIN_LAND)
                continue;
            top = unit_at(nx, ny);
            if (top != -1 && unit_ptr(stack_owner, top)->type == UNIT_DIPLOMAT) {
                /* Look past diplomats for something to fight. */
                int j = top, steps = 0;
                do
                    j = (int8_t)unit_ptr(stack_owner, j)->next;
                while (j != -1 && j != top && unit_ptr(stack_owner, j)->type == UNIT_DIPLOMAT && ++steps < 1000);
                if (j != -1)
                    top = j;
            }
            if (zoc && unit_type_word(UTYPE(p), UTW_DOMAIN) == DOMAIN_LAND && top == -1 && in_enemy_zone(civ, nx, ny) &&
                p->type < UNIT_DIPLOMAT && terrain != TERRAIN_OCEAN)
                continue;
            if (terrain == TERRAIN_OCEAN && unit_type_word(STYPE(p), UTW_DOMAIN) == DOMAIN_LAND && top != -1 && lo != civ)
                continue;
            if (unit_type_word(STYPE(p), UTW_DOMAIN) == DOMAIN_SEA && sq != TERRAIN_OCEAN) {
                if (top == -1 || lo == civ || type == UNIT_SUBMARINE || terrain != TERRAIN_OCEAN)
                    continue;
            }
            if (top != -1 && lo == civ && stack_sum_of(civ, top, 2) >= 2)
                continue;
            if (role == 0) {
                score = 0;
                if (terrain == TERRAIN_OCEAN && enemy_in_zone(civ, nx, ny))
                    continue;
            } else {
                if (p->visible == 0 && terrain != TERRAIN_OCEAN) {
                    if (role == 1)
                        score = (int16_t)(rnd(3) - (int8_t)terrain_types[sq].move_cost * 2);
                    else
                        score = (int16_t)(rnd(3) - (int8_t)terrain_types[sq].defense);
                } else {
                    score = (int16_t)rnd(5);
                    if (top != -1 && lo == civ) {
                        if (role == 1) {
                            n = stack_sum_of(civ, top, 2);
                            score += (int16_t)(stack_sum_of(civ, top, 1) * 4 / (n + 1));
                        }
                        if (role == 0)
                            score += (int16_t)(((int8_t)terrain_types[sq].defense + stack_sum_of(civ, top, 1)) * 2);
                        if (role == 2) {
                            n = stack_sum_of(civ, top, 1);
                            score += (int16_t)(stack_sum_of(civ, top, 3) * 2 / (n + 1));
                        }
                    } else
                        score += (int16_t)((int8_t)terrain_types[sq].defense * 4);
                }
                if (unit_type_word(STYPE(p), UTW_DOMAIN) == DOMAIN_AIR)
                    score = (int16_t)rnd(3);
            }
            if (p->unk8 != NONE8) {
                d = abs((int8_t)p->unk8 - i);
                if (d > 4)
                    d = 8 - d;
                score -= (int16_t)(d * d * 2);
            }
            enemy = 0;
            if (top != -1) {
                if (lo == civ) {
                    score -= (int16_t)unit_type_word(STYPE(p), UTW_DEFENSE);
                } else {
                    enemy = 1;
                    if (role == 0)
                        continue;
                    if (DIP(civ, lo) & DIP_PEACE) {
                        /* At peace: perhaps buy the unit to stir things up against the human. */
                        if (role == 1 && pol == 1 && unit_type_word(STYPE(p), UTW_DOMAIN) == DOMAIN_LAND &&
                            (unit_ptr(lo, top)->status & UNIT_FORTIFIED) && (DIP(civ, human) & 3) == 1 &&
                            (DIP(lo, human) & 3) != 1 && CONT_WORD(s->unk_6b16, human, cont) != 0 &&
                            stack_sum_of(lo, top, 2) < 2 &&
                            (rnd(8) == 0 || (int8_t)CITY(city)->owner == human) &&
                            !(map_improvements(nx, ny) & IMP_CITY))
                            diplomat_bribe(lo, top, civ);
                        continue;
                    }
                    top = stack_best_defender(lo, top);
                    if (unit_type_word(STYPE(unit_ptr(lo, top)), UTW_DOMAIN) == DOMAIN_AIR && p->type != 14 &&
                        !(map_improvements(nx, ny) & IMP_CITY))
                        continue;
                    odds = (int16_t)unit_attack(civ, u, lo, top, 0);
                    odds = (int16_t)((stack_sum_of(lo, top, 0) + 1) * odds);
                    odds = (int16_t)(odds / unit_type_word(STYPE(p), UTW_COST));
                    if (map_improvements(nx, ny) & IMP_CITY)
                        odds = (int16_t)(odds * 3);
                    if (role == 1 && pol == 1)
                        odds = (int16_t)(odds * 3);
                    if (role == 1 && unit_type_word(STYPE(p), UTW_ATTACK) * 2 < stack_sum_of(civ, u, 3))
                        odds = (int16_t)(odds << 1);
                    if (odds >= (civ != 0 ? 6 : 12)) {
                        score += (int16_t)(odds * 4);
                    } else {
                        score -= 999;
                        if (role == 1 && pol == 1 && unit_type_word(STYPE(p), UTW_DOMAIN) == DOMAIN_LAND &&
                            (unit_ptr(lo, top)->status & (UNIT_FORTIFIED | UNIT_VETERAN)) &&
                            terrain_types[sq].defense >= 4 && stack_sum_of(lo, top, 2) < 2 && rnd(4) == 0 &&
                            !(map_improvements(nx, ny) & IMP_CITY))
                            diplomat_bribe(lo, top, civ);
                    }
                }
            } else {
                if ((map_improvements(nx, ny) & IMP_CITY) && lo != civ)
                    score = 999;
                if (has_hut(sq, nx, ny))
                    score += 20;
            }
            if (free) {
                /* Prefer heading for unexplored country. */
                int fx = around_dx[i] * 4 + x, fy = around_dy[i] * 4 + y, j, ax, ay;
                if (block_flags(fx / 4, fy / 4) == 0 && map_terrain(fx, fy) != TERRAIN_OCEAN && map_in_bounds(fx, fy))
                    score += 8;
                for (j = 1; j <= 8; j++) {
                    ax = wrap_x(around_dx[j] + fx);
                    ay = around_dy[j] + fy;
                    if (!map_in_bounds(ax, ay))
                        continue;
                    if (!(civ_bit & (int8_t)s->visibility[ax][ay]) &&
                        (map_terrain(ax, ay) != TERRAIN_OCEAN || unit_type_word(STYPE(p), UTW_DOMAIN) == DOMAIN_SEA))
                        score += 2;
                    if (unit_at(ax, ay) != -1)
                        score -= 2;
                    if (role == 0)
                        score += (int8_t)terrain_types[map_terrain(ax, ay)].food;
                }
            }
            if (enemy && p->moves < 3) {
                n = clamp(stack_sum_of(civ, u, 3), 1, 99);
                score = (int16_t)((int16_t)(stack_sum_of(civ, u, 1) * score) / n);
            }
            if (score > best) {
                best = score;
                best_dir = i;
                best_enemy = enemy;
            }
        }
        if (best_enemy && p->moves < 3)
            best_dir = 0;
        p->unk8 = (uint8_t)best_dir;
        return best_dir;
    }
}
