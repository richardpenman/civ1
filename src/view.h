#ifndef VIEW_H
#define VIEW_H

#include <stdint.h>

void view_center(int civ, int x, int y);   /* f_1b05_0004 */
void view_follow(int civ, int x, int y);   /* f_0860_1679 */
void redraw_map(void);                     /* f_0237_1b40 */
void frame_screen(void);                   /* f_0860_25e1 */
void newspaper(int city, const char *text);    /* o_21_0000 */
void city_view_layout(int city, int8_t *grid, int *river_col);
void city_view(int city, int mode);             /* o_19_0000 */
extern int units_hidden;                        /* g_ec1c */
void city_square_lookup(int x, int y);
void terrain_redraw(int x, int y);              /* f_1b05_03b1 */
void square_redraw(int x, int y);               /* f_1b05_11c6 */
void redraw_if_seen(int x, int y);              /* f_0402_3e8c */
void redraw_unit_square(int civ, int u);        /* f_0402_3ec3 */
void reveal_redraw(int x, int y, int civ);
void combat_display(int civ, int u, int dciv, int du, int won);

#endif
