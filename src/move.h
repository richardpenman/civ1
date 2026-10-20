#ifndef MOVE_H
#define MOVE_H

/* unit_command() results */
#define MOVE_DONE     0x01
#define MOVE_MOVED    0x02   /* the unit changed square */
#define MOVE_END_UNIT 0x04   /* stop giving this unit orders now */
#define MOVE_RESCAN   0x08   /* ('w') look at this unit again later */
#define MOVE_JUMP     0x10   /* ('u') continue with *next_unit */
#define MOVE_GONE     0x20   /* the unit died */

/* The human's command for "a click made unit ui_activate_unit the active one" (L0c4f). */
#define CMD_ACTIVATE  0x200
extern int ui_activate_unit;

extern int last_dir, last_terrain, inciting, view_x, view_y, fast_view, midturn_loaded;
extern uint8_t sea_links[20 * 13];
extern int (*ai_unit_command_hook)(int civ, int unit);

int  unit_command(int civ, int unit, int key, int *next_unit);
void civ_move_units(int civ);
int  goto_step(int civ, int unit);
int  reachable(int x1, int y1, int x2, int y2, int sea, int limit);   /* f_1e4a_1112 */
int  water_near(int x, int y);                         /* f_0402_3f80 */
int  plunder(int city);                                /* f_1439_067f */
void goto_reset(void);
int  found_city(int civ, int unused, int x, int y, int size);
void city_capture(int civ, int city, int incited);
int  civil_war(int civ);
int  steal_advance(int civ, int from);
void spaceship_lost(int civ);
int  capital_distance(int civ, int x, int y);
void city_revolt(int city, int to);
void diplomat_bribe(int owner, int unit, int civ);
void diplomat_enters_city(int city, int civ, int unit);
int  ai_unit_command(int civ, int unit);               /* f_15da_0c76 */

#endif
