/* The game turn (original seg 0237). */
#ifndef TURN_H
#define TURN_H

int  game_turn(void);                 /* game_turn_loop */
int  civ_score(int civ);              /* o_20_0c98 without its screen */
void status_report(void);
int  palace_section_open(int section);
void palace_improve(void);              /* o_17_0000 */             /* f_0237_107a */
extern int load_marker;               /* g_3434 */
extern int pop_milestone;             /* g_1c24 */
extern int retired;
extern int status_peace;              /* (the peace bar is drawn with this) */                   /* g_c7a4 */

#endif
