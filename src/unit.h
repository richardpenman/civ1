/* Units and unit stacks (original seg 0860, 1a19, 1b05). */
#ifndef UNIT_H
#define UNIT_H

#include "game.h"

typedef void (*UnitFn)(int civ, int unit);

static inline Unit *unit_ptr(int civ, int u) { return &game.s.units[civ][u]; }
static inline const UnitType *utype(int type) { return &game.s.unit_types[type]; }

/* unit_type_word() fields (words of the original record from obsolete_by on) */
enum { UTW_DOMAIN = 1, UTW_MOVES, UTW_FUEL, UTW_ATTACK, UTW_DEFENSE, UTW_COST, UTW_SIGHT,
       UTW_CAPACITY, UTW_ROLE };
int  unit_type_word(int type, int f);
extern int umax_shown;

int  unit_at(int x, int y);
void unit_link(int civ, int unit, int top);
void unit_unlink(int civ, int unit);
void unit_place(int civ, int unit, int x, int y);
void unit_remove_from_square(int civ, int unit, int x, int y);
void stack_foreach(int civ, int unit, UnitFn fn);
int  stack_count_role(int civ, int unit, int role);
int  enemy_adjacent(int civ, int x, int y);
int  create_unit(int civ, int type, int x, int y);
void delete_unit(int civ, int unit);
void unit_reveal(int x, int y, int civ, int unit, int display);
void cb_wake(int civ, int unit);
void cb_delete_land(int civ, int unit);
void cb_delete_nonland(int civ, int unit);
void cb_delete_air(int civ, int unit);
void cb_reset_orders(int civ, int unit);
void cb_unload(int civ, int unit);
void cb_reset_ships(int civ, int unit);
int  stack_sum_of(int civ, int unit, int kind);
int  stack_count_type(int civ, int unit, int type);
int  stack_room(int civ, int unit);
int  stack_best_defender(int civ, int unit);
int  stack_best_defender_from(int civ, int start, int best, int defence);
int  stack_defender_at(int civ, int unit);            /* f_0860_1059 */
int  in_enemy_zone(int civ, int x, int y);
int  enemy_in_zone(int civ, int x, int y);  /* f_0860_1720 */

#endif
