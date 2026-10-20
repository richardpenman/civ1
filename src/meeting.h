/* The human player and a rival's leader talk (original overlay 6). */
#ifndef MEETING_H
#define MEETING_H

extern int audience_open;                         /* g_e724 */
void diplomacy_meeting(int civ, int x, int y, int by_diplomat);  /* o_06_0000 */
int  diplomacy_audience(int civ, int x, int y);   /* o_06_16cd */
void diplomacy_audience_end(void);                /* o_06_16ac */

#endif
