#include <stddef.h>
#include "game.h"
#include "map.h"
#include "unit.h"
#include "util.h"
#include "ai.h"
#include "diplomacy.h"
#include "ui.h"
#include "view.h"
#include "city.h"

/* f_1b05_144a: the first unit on a square (of whichever civ is there), or -1. */
int unit_at(int x, int y)
{
    int civ = map_unit_owner(x, y), u;

    if (civ == -1)
        return -1;
    for (u = 0; u < MAX_UNITS; u++) {
        const Unit *p = unit_ptr(civ, u);
        if (p->type != NONE8 && p->x == x && p->y == y) {
            stack_owner = civ;
            return u;
        }
    }
    map_put_pixel(x + 160, y, 0);     /* stale marker */
    return -1;
}

/* f_1a19_0b66: units on one square form a circular list through `next`. */
void unit_link(int civ, int u, int top)
{
    Unit *t = unit_ptr(civ, top);

    if (t->next == NONE8) {
        t->next = (uint8_t)u;
        unit_ptr(civ, u)->next = (uint8_t)top;
    } else {
        unit_ptr(civ, u)->next = t->next;
        t->next = (uint8_t)u;
    }
}

/* f_1a19_0bc9 */
void unit_unlink(int civ, int u)
{
    int p = (int8_t)unit_ptr(civ, u)->next, n;

    if (p == -1)
        return;
    if ((int8_t)unit_ptr(civ, p)->next == u) {
        unit_ptr(civ, p)->next = NONE8;
    } else {
        for (n = 0; ; ) {
            p = (int8_t)unit_ptr(civ, p)->next;
            n++;
            if (n >= 32 || p == -1 || (int8_t)unit_ptr(civ, p)->next == u)
                break;
        }
        if (n < 32 && p != -1)
            unit_ptr(civ, p)->next = unit_ptr(civ, u)->next;
    }
    unit_ptr(civ, u)->next = NONE8;
}

/* f_1b05_13bd */
void unit_place(int civ, int u, int x, int y)
{
    int top = unit_at(x, y);

    if (top != -1)
        unit_link(civ, u, top);
    map_put_pixel(x + 160, y, civ + 8);
}

/* f_1b05_1404 */
void unit_remove_from_square(int civ, int u, int x, int y)
{
    if (unit_ptr(civ, u)->next != NONE8)
        unit_unlink(civ, u);
    else
        map_put_pixel(x + 160, y, civ);
}

/* f_0860_141b: call fn for each unit of the stack starting at `u` (at most 10). */
void stack_foreach(int civ, int u, UnitFn fn)
{
    int first = u, next, n = 0;

    for (;;) {
        next = (int8_t)unit_ptr(civ, u)->next;
        fn(civ, u);
        u = next;
        if (next == -1 || u == first || ++n >= 10)
            break;
    }
}

static int count_role, count_result;

/* f_0860_1379 */
static void cb_count_role(int civ, int u)
{
    if (utype(unit_ptr(civ, u)->type)->role == count_role)
        count_result++;
}

/* f_0860_1350 */
int stack_count_role(int civ, int u, int role)
{
    count_role = role;
    count_result = 0;
    stack_foreach(civ, u, cb_count_role);
    return count_result;
}

/* f_0860_1472: wake sentries, except land units being carried at sea. */
void cb_wake(int civ, int u)
{
    Unit *p = unit_ptr(civ, u);

    if (map_terrain(p->x, p->y) != TERRAIN_OCEAN || utype(p->type)->domain != DOMAIN_LAND)
        p->status &= ~UNIT_SENTRY;
}

/* f_0860_15e0 / 1613 / 1646 */
void cb_delete_land(int civ, int u)
{
    if (utype(unit_ptr(civ, u)->type)->domain == DOMAIN_LAND)
        delete_unit(civ, u);
}

void cb_delete_nonland(int civ, int u)
{
    if (utype(unit_ptr(civ, u)->type)->domain != DOMAIN_LAND)
        delete_unit(civ, u);
}

void cb_delete_air(int civ, int u)
{
    if (utype(unit_ptr(civ, u)->type)->domain == DOMAIN_AIR)
        delete_unit(civ, u);
}

/* f_0860_1891: does another civ have units next to (x, y)? */
int enemy_adjacent(int civ, int x, int y)
{
    int i, o;

    for (i = 1; i < 9; i++) {
        o = map_unit_owner(wrap_x(x + around_dx[i]), y + around_dy[i]);
        if (o != -1 && o != civ)
            return 1;
    }
    return 0;
}

int umax_shown;          /* g_e680: the unit limit message was shown this turn */

/* f_0860_0cc7 */
int create_unit(int civ, int type, int x, int y)
{
    int u, home;
    Unit *p;

    for (u = 0; u < MAX_UNITS - 1; u++)
        if (unit_ptr(civ, u)->type == NONE8)
            break;
    if (u == MAX_UNITS - 1) {
        if (civ == game.s.human_civ && !umax_shown) {
            ui_help_message("*UMAX");
            umax_shown = 1;
        }
        return -1;
    }
    p = unit_ptr(civ, u);
    p->x = NONE8;
    p->next = NONE8;
    map_set_unit_layer_owner(civ, x, y);
    unit_place(civ, u, x, y);
    game.s.visibility[x][y] |= 1 << civ;
    p->status = 0;
    p->x = (uint8_t)x;
    p->y = (uint8_t)y;
    p->type = (uint8_t)type;
    p->visible = (uint8_t)(1 << civ);
    p->goto_x = NONE8;
    p->unk8 = NONE8;
    p->special = (uint8_t)utype(type)->fuel;
    home = nearest_city(x, y, NULL);
    p->home_city = (uint8_t)home;
    if (home == -1 || (int8_t)game.s.cities[home].owner != civ)
        p->home_city = NONE8;
    if (p->special)
        p->special--;
    game.s.units_active[civ][type]++;
    if (!quiet) {
        if (show_all || civ == game.s.human_civ || (p->visible & (1 << game.s.human_civ)))
            square_redraw(x, y);
        unit_reveal(x, y, civ, u, 1);
    }
    if (civ == game.s.human_civ && game.s.turn != 0 && !quiet) {
        if (game.s.unit_count[civ] == 0)
            ui_help_message("*FIRSTUNIT1");
        if (game.s.unit_count[civ] == 1)
            ui_help_message("*FIRSTUNIT2");
    }
    return u;
}

/* f_0860_0ee0 */
void delete_unit(int civ, int u)
{
    Unit *p = unit_ptr(civ, u);

    /* The type is used as a signed index: deleting a unit twice (which happens) counts
     * against entry -1, the previous civilization's last type. */
    if (count_losses)
        ((int16_t *)game.s.units_lost)[civ * NUM_UNIT_TYPES + (int8_t)p->type]++;
    if (unit_type_word((int8_t)p->type, UTW_CAPACITY) && p->next != NONE8 && map_terrain(p->x, p->y) == TERRAIN_OCEAN)
        stack_foreach(civ, u, cb_delete_land);
    if (p->type == UNIT_CARRIER && p->next != NONE8 && map_terrain(p->x, p->y) == TERRAIN_OCEAN)
        stack_foreach(civ, u, cb_delete_air);
    ((int16_t *)game.s.units_active)[civ * NUM_UNIT_TYPES + (int8_t)p->type]--;
    p->type = NONE8;
    p->moves = 0;
    unit_remove_from_square(civ, u, p->x, p->y);
    if (!quiet && replay_suppress == -1 &&
        (show_all || civ == game.s.human_civ || (p->visible & (1 << game.s.human_civ))))
        square_redraw(p->x, p->y);
}

static void minimap_update(int x, int y)
{
    square_redraw(x, y);
    map_put_pixel(x + 240, y, map_pixel(x, y) == 1 ? 1 : 2);
}

/* f_0860_01dc: what a unit at (x, y) sees, and who sees it.  Also records AI goals and
 * makes first contact between civilizations whose land units meet. */
void unit_reveal(int x, int y, int civ, int u, int display)
{
    Unit *me = unit_ptr(civ, u);
    const UnitType *mt = utype(me->type);
    int here = map_terrain(x, y), imp_here, imp, i, sx, sy, top, owner, c, prio;
    int human = game.s.human_civ;

    if (civ == human) {
        for (i = 0; i < 25; i++) {
            sx = wrap_x(x + around_dx[i]);
            sy = y + around_dy[i];
            if (i >= 9 && (!(mt->sight & 2) || (mt->domain == DOMAIN_SEA && map_terrain(sx, sy) != TERRAIN_OCEAN)))
                continue;
            if (!map_in_bounds(sx, sy))
                continue;
            game.s.visibility[sx][sy] |= 1 << civ;
            map_refresh_known(sx, sy);
        }
    }

    imp_here = map_improvements(x, y);
    for (i = 1; i < 9; i++) {
        sx = wrap_x(x + around_dx[i]);
        sy = y + around_dy[i];
        if (!map_in_bounds(sx, sy))
            continue;
        if (civ != 0)
            game.s.visibility[sx][sy] |= 1 << civ;
        imp = map_improvements(sx, sy);
        if ((imp & IMP_CITY) && map_unit_layer_owner(sx, sy) != civ) {
            owner = map_unit_layer_owner(sx, sy);
            c = city_at(sx, sy);
            city_unpack_fortified(c);
            if (civ == human)
                game.s.cities[c].visible_size = game.s.cities[c].size;
            if (mt->domain == DOMAIN_LAND) {
                me->visible |= 1 << owner;
                if (!(game.s.diplomacy[civ][owner] & DIP_PEACE)) {
                    ai_add_goal(owner, sx, sy, 1, 4);
                    ai_add_goal(owner, sx, sy, 2, 2);
                }
            }
        }
        top = unit_at(sx, sy);
        if (top != -1 && civ != stack_owner) {
            owner = stack_owner;
            if (!(imp_here & IMP_CITY))
                me->visible |= 1 << owner;
            stack_foreach(owner, top, cb_wake);
            if (map_terrain(sx, sy) == TERRAIN_OCEAN || here != TERRAIN_OCEAN)
                unit_ptr(owner, top)->goto_x = NONE8;
            if (map_terrain(sx, sy) != TERRAIN_OCEAN || here == TERRAIN_OCEAN || mt->domain == DOMAIN_AIR)
                me->goto_x = NONE8;
            if (!(imp & IMP_CITY))
                unit_ptr(owner, top)->visible |= 1 << civ;
            if (mt->domain == DOMAIN_LAND && here != TERRAIN_OCEAN && map_terrain(sx, sy) != TERRAIN_OCEAN &&
                utype(unit_ptr(owner, top)->type)->domain != DOMAIN_AIR) {
                /* From here the original reads stack_owner again, which the meeting's map
                 * redraws (in civ_contact) may have changed: see view.c. */
                civ_contact(civ, owner, x, y);
                if (!(game.s.diplomacy[civ][stack_owner] & DIP_PEACE)) {
                    const Unit *t = unit_ptr(stack_owner, top);
                    prio = 1;
                    if (stack_owner != 0 && t->type != UNIT_DIPLOMAT &&
                        (!(t->status & UNIT_FORTIFIED) || utype(t->type)->role != 2))
                        prio = 3;
                    ai_add_goal(civ, sx, sy, 1, prio);
                }
                if (me->type != UNIT_DIPLOMAT)
                    ai_add_goal(stack_owner, x, y, 2, civ == 0 ? 1 : 2);
            }
            if (map_terrain(sx, sy) == TERRAIN_OCEAN && stack_owner != 0 &&
                !(game.s.diplomacy[civ][stack_owner] & DIP_PEACE))
                ai_add_goal(civ, sx, sy, 3, 2);
        }
        if (display && (civ == human || show_all))
            minimap_update(sx, sy);
    }

    for (i = 9; i < 25; i++) {
        sx = wrap_x(x + around_dx[i]);
        sy = y + around_dy[i];
        if (!map_in_bounds(sx, sy))
            continue;
        top = unit_at(sx, sy);
        owner = stack_owner;
        imp = map_improvements(sx, sy);
        if ((mt->sight & 2) && (mt->domain != DOMAIN_SEA || map_terrain(sx, sy) == TERRAIN_OCEAN)) {
            if (civ != 0)
                game.s.visibility[sx][sy] |= 1 << civ;
            if (top != -1 && civ != owner && unit_ptr(owner, top)->type != UNIT_SUBMARINE) {
                if (!(imp & IMP_CITY))
                    unit_ptr(owner, top)->visible |= 1 << civ;
                if (map_terrain(sx, sy) == TERRAIN_OCEAN && owner != 0 && !(game.s.diplomacy[civ][owner] & DIP_PEACE))
                    ai_add_goal(civ, sx, sy, 3, 2);
            }
            if (display && (civ == human || show_all))
                minimap_update(sx, sy);
        }
        if (top != -1 && civ != owner && me->type != UNIT_SUBMARINE) {
            Unit *t = unit_ptr(owner, top);
            const UnitType *tt = utype(t->type);
            if ((tt->sight & 2) && (tt->domain != DOMAIN_SEA || here == TERRAIN_OCEAN)) {
                t->status &= ~UNIT_SENTRY;
                t->goto_x = NONE8;
                if (!(imp_here & IMP_CITY))
                    me->visible |= 1 << owner;
                if (map_terrain(sx, sy) == TERRAIN_OCEAN && owner != 0 && !(game.s.diplomacy[civ][owner] & DIP_PEACE))
                    ai_add_goal(stack_owner, x, y, 3, 2);
            } else if (owner == human && (map_improvements(sx, sy) & IMP_CITY) &&
                       (game.s.visibility[x][y] & (1 << human))) {
                me->visible |= 1 << owner;
                square_redraw(x, y);
            }
        }
        if ((imp & IMP_CITY) && map_unit_layer_owner(sx, sy) == human && (game.s.visibility[x][y] & (1 << human))) {
            me->visible |= 1 << human;
            square_redraw(x, y);
        }
    }
    if (civ == human && display)
        reveal_redraw(x, y, civ);
}

/* --- more stack helpers (seg 0860) --------------------------------------------------- */

static int stack_sum_kind, stack_sum_before, stack_sum;

/* f_0860_1250 */
static void cb_sum(int civ, int u)
{
    const UnitType *t = utype(unit_ptr(civ, u)->type);

    switch (stack_sum_kind) {
    case 0: stack_sum += t->cost; break;
    case 1: stack_sum += t->defense; break;
    case 2: stack_sum++; break;
    case 3: stack_sum += t->attack; break;
    case 4:
        if (u < stack_sum_before && t->role == 2)
            stack_sum++;
        break;
    }
}

/* A unit-type field as the original reads it, also for type -1 or 255 (a unit already
 * deleted): word `f` counted from obsolete_by, out of range taken from the EXE's data. */
int unit_type_word(int type, int f)
{
    const UnitType *t;

    if (type < 0 || type >= NUM_UNIT_TYPES)
        return dgroup_word(0x112a + type * 34 + 12 + 2 * f);
    t = &game.s.unit_types[type];
    switch (f) {
    case UTW_DOMAIN:   return t->domain;
    case UTW_MOVES:    return t->moves;
    case UTW_FUEL:     return t->fuel;
    case UTW_ATTACK:   return t->attack;
    case UTW_DEFENSE:  return t->defense;
    case UTW_COST:     return t->cost;
    case UTW_CAPACITY: return t->capacity;
    case UTW_ROLE:     return t->role;
    default:           return 0;
    }
}

/* f_0860_1221: over the stack at `u`: 0 cost, 1 defence, 2 count, 3 attack,
 * 4 defenders with a lower index than `u`. */
int stack_sum_of(int civ, int u, int kind)
{
    stack_sum = 0;
    stack_sum_kind = kind;
    stack_sum_before = u;
    stack_foreach(civ, u, cb_sum);
    return stack_sum;
}

static int count_type;

/* f_0860_132a */
static void cb_count_type(int civ, int u)
{
    if ((int8_t)unit_ptr(civ, u)->type == count_type)
        count_result++;
}

/* f_0860_1301 */
int stack_count_type(int civ, int u, int type)
{
    count_type = type;
    count_result = 0;
    stack_foreach(civ, u, cb_count_type);
    return count_result;
}

/* f_0860_13c8 */
static void cb_room(int civ, int u)
{
    const UnitType *t = utype(unit_ptr(civ, u)->type);

    if (t->domain == DOMAIN_LAND)
        count_result--;
    if (t->role == 5)
        count_result += t->capacity;
}

/* f_0860_13a5: spare transport capacity in the stack. */
int stack_room(int civ, int u)
{
    count_result = 0;
    stack_foreach(civ, u, cb_room);
    return count_result;
}

static int best_defender, best_defence;

/* f_0860_1139 */
static void cb_defender(int civ, int u)
{
    const Unit *p = unit_ptr(civ, u);
    const UnitType *t = utype(p->type);
    int terrain = map_terrain(p->x, p->y), v;

    if (terrain == TERRAIN_OCEAN && t->domain != DOMAIN_SEA)
        return;
    if (t->domain == DOMAIN_LAND)
        v = ((p->status & UNIT_FORTIFIED) ? 3 : 2) * t->defense * terrain_types[terrain].defense << 3;
    else
        v = t->defense << 4;
    if (p->status & UNIT_VETERAN)
        v += v / 2;
    if (v > best_defence) {
        best_defence = v;
        best_defender = u;
    }
}

/* f_0860_10f2: the unit that defends the stack at `u`. */
int stack_best_defender(int civ, int u)
{
    if (unit_ptr(civ, u)->next == NONE8)
        return u;
    return stack_best_defender_from(civ, u, u, -1);
}

/* The search over the stack at `start` with the given starting answer (f_0860_1059 starts
 * from another unit of the stack, with defence 0). */
int stack_best_defender_from(int civ, int start, int best, int defence)
{
    best_defender = best;
    best_defence = defence;
    stack_foreach(civ, start, cb_defender);
    return best_defender;
}

/* f_0860_1563: forget orders (used on units next to a city that just lost its defender). */
void cb_reset_orders(int civ, int u)
{
    Unit *p = unit_ptr(civ, u);

    if (p->status & (UNIT_ROAD | UNIT_IRRIGATE | UNIT_MINE))
        p->special = 0;
    if (p->status & (UNIT_SENTRY | UNIT_ROAD | UNIT_FORTIFIED | UNIT_IRRIGATE | UNIT_MINE))
        p->moves = (uint8_t)(utype(p->type)->moves * 3);
    p->status &= UNIT_GOTO | UNIT_VETERAN;
    p->goto_x = NONE8;
    if (civ == game.s.human_civ)
        ui_redraw_unit(civ, u);
}

/* f_0860_14c6: units carried off by a ship wake up fresh. */
void cb_unload(int civ, int u)
{
    Unit *p = unit_ptr(civ, u);

    if (p->status & UNIT_SENTRY) {
        p->status &= ~UNIT_SENTRY;
        p->moves = (uint8_t)(utype(p->type)->moves * 3);
        if (utype(p->type)->fuel)
            p->special = (uint8_t)(utype(p->type)->fuel - 1);
    }
    p->unk8 = NONE8;
    p->goto_x = NONE8;
}

/* f_0860_1530 */
void cb_reset_ships(int civ, int u)
{
    if (utype(unit_ptr(civ, u)->type)->domain == DOMAIN_SEA)
        cb_reset_orders(civ, u);
}

/* f_0860_1720: is an enemy next to (x, y) on the same kind of terrain?  The human
 * gets to see such enemies (and their cities). */
int enemy_in_zone(int civ, int x, int y)
{
    int sea = map_terrain(x, y) == TERRAIN_OCEAN, i, sx, sy, o, u, c, human = game.s.human_civ;

    for (i = 1; i < 9; i++) {
        sx = wrap_x(x + around_dx[i]);
        sy = y + around_dy[i];
        if ((map_terrain(sx, sy) == TERRAIN_OCEAN) != sea)
            continue;
        o = map_unit_owner(sx, sy);
        if (o == -1 || o == civ)
            continue;
        if (civ == human) {
            u = unit_at(sx, sy);
            if (!(unit_ptr(o, u)->visible & (1 << human))) {
                unit_ptr(o, u)->visible |= 1 << human;
                game.s.visibility[sx][sy] |= 1 << human;
                square_redraw(sx, sy);
            }
            if (map_improvements(sx, sy) & IMP_CITY) {
                c = city_at(sx, sy);
                game.s.cities[c].visible_size = game.s.cities[c].size;
                map_refresh_known(sx, sy);
                game.s.visibility[sx][sy] |= 1 << human;
                square_redraw(sx, sy);
            }
        }
        return 1;
    }
    return 0;
}

/* f_0860_16f5: zone of control (cities are exempt). */
int in_enemy_zone(int civ, int x, int y)
{
    if (map_improvements(x, y) & IMP_CITY)
        return 0;
    return enemy_in_zone(civ, x, y);
}
