/* Per-civilization queries. */
#ifndef CIV_H
#define CIV_H

#include "game.h"

int civ_has_advance(int civ, int advance);
int wonder_active_city(int wonder);           /* city with the wonder, -1 if none/obsolete */
int civ_has_wonder(int civ, int wonder);      /* owns it and it is not obsolete */

int  civ_restart(int civ);                     /* o_05_07c2: 1 = (re)placed, 0 = out */
void civ_check_destroyed(int civ, int by);    /* o_05_0e67 */
extern int civ_unk_e12c[MAX_CIVS], earth_map, quit_flag, game_won_conquest;

#endif
