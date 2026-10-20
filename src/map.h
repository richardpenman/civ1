#ifndef MAP_H_INCLUDED
#define MAP_H_INCLUDED

int  map_pixel(int x, int y);
void map_put_pixel(int x, int y, int v);
int  map_in_bounds(int x, int y);
int  wrap_x(int x);
int  map_terrain(int x, int y);
int  map_continent(int x, int y);
int  map_improvements(int x, int y);
int  map_known_improvements(int x, int y);
void map_refresh_known(int x, int y);
void map_set_improvements(int bits, int x, int y);
void map_clear_improvements(int bits, int x, int y);
int  map_unit_owner(int x, int y);
int  map_unit_layer_owner(int x, int y);
void map_set_unit_layer_owner(int civ, int x, int y);
int  map_tile_owner(int x, int y);
int  has_special_resource(int x, int y);
int  has_hut(int terrain, int x, int y);
void map_set_units_present(int x, int y);
int  body_area(int x, int y);

#endif
