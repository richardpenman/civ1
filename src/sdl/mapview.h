#ifndef MAPVIEW_H
#define MAPVIEW_H

#include "../game.h"

extern int map_font, no_units_drawn, minimap_left, minimap_top;

void shadow_text(const char *s, int x, int y, int colour);
void frame3d(int x, int y, int w, int h, int light, int dark);
void panel(int x, int y, int w, int h);
void draw_square(int x, int y);
void draw_square_at(int x, int y, int px, int py, int labels);
void draw_square_units(int x, int y);
int  draw_unit(int civ, int u);
void draw_unit_at(int civ, int u, int px, int py, const Unit *p);
void draw_city_labels(void);
void draw_minimap(int civ, int x, int y);
void draw_view(int civ, int x, int y, int start);
void draw_menu_bar(void);
void draw_status_panel(void);
void draw_unit_panel(int civ, int u);

#endif
