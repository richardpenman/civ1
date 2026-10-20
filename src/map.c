/* World map queries and updates (original seg 1b05).  The map lives in pixels of an
 * off-screen 320x200 page, in 80x50 blocks; see the layer table in game.h. */
#include "game.h"
#include "map.h"

/* The page is addressed linearly, as the graphics driver does: an x just off the left or
 * right edge (which the original produces near the map border) lands in the next row. */
int map_pixel(int x, int y)
{
    int k = y * MAPL_PAGE_W + x;

    return k >= 0 && k < MAPL_PAGE_W * MAPL_PAGE_H ? (&game.map.pixels[0][0])[k] : 0;
}

void map_put_pixel(int x, int y, int v)
{
    int k = y * MAPL_PAGE_W + x;

    if (k >= 0 && k < MAPL_PAGE_W * MAPL_PAGE_H)
        (&game.map.pixels[0][0])[k] = (uint8_t)v;
}

int map_in_bounds(int x, int y)
{
    return x >= 0 && x < MAP_W && y >= 0 && y < MAP_H;
}

/* f_1e4a_1191 */
int wrap_x(int x)
{
    if (x < 0)
        return x + MAP_W;
    if (x >= MAP_W)
        return x - MAP_W;
    return x;
}

int map_terrain(int x, int y)
{
    return terrain_by_colour[map_pixel(x, y) & 15];
}

int map_continent(int x, int y)
{
    return map_pixel(x, y + 50);
}

int map_improvements(int x, int y)
{
    return map_pixel(x, y + 100) | map_pixel(x, y + 150) << 4;
}

int map_known_improvements(int x, int y)
{
    return map_pixel(x + 80, y + 100) | map_pixel(x + 80, y + 150) << 4;
}

/* f_1b05_15f3: the human player now sees the square as it really is. */
void map_refresh_known(int x, int y)
{
    map_put_pixel(x + 80, y + 100, map_pixel(x, y + 100));
    map_put_pixel(x + 80, y + 150, map_pixel(x, y + 150));
}

/* f_1b05_1645.  Values below 16 go to the low nibble; others only touch the high one. */
void map_set_improvements(int bits, int x, int y)
{
    if (bits == 0) {
        map_put_pixel(x, y + 100, 0);
        map_put_pixel(x, y + 150, 0);
    } else if (bits < 16) {
        map_put_pixel(x, y + 100, map_pixel(x, y + 100) | bits);
    } else {
        map_put_pixel(x, y + 150, map_pixel(x, y + 150) | bits >> 4);
    }
    if (cur_player == game.s.human_civ)
        map_refresh_known(x, y);
}

/* f_1b05_16e0 */
void map_clear_improvements(int bits, int x, int y)
{
    if (bits & 0x0F)
        map_put_pixel(x, y + 100, map_pixel(x, y + 100) & ~bits);
    if (bits & 0xF0)
        map_put_pixel(x, y + 150, map_pixel(x, y + 150) & ~(bits >> 4));
    if (cur_player == game.s.human_civ)
        map_refresh_known(x, y);
}

/* f_1b05_14d2: owner of the units on a square, or -1. */
int map_unit_owner(int x, int y)
{
    int v = map_pixel(x + 160, y);

    return (v & 8) ? (v & 7) : -1;
}

/* f_1b05_135b */
int map_unit_layer_owner(int x, int y)
{
    return map_pixel(x + 160, y) & 7;
}

/* f_1b05_137e: keep the "units present" bit, change the recorded owner. */
void map_set_unit_layer_owner(int civ, int x, int y)
{
    map_put_pixel(x + 160, y, (map_pixel(x + 160, y) & 8) + civ);
}

int map_tile_owner(int x, int y)
{
    return map_pixel(x + 80, y);
}

/* Specials are not stored: one square in each 4x4 block is special, chosen by the seed. */
int has_special_resource(int x, int y)
{
    int v;

    if (y <= 1 || y >= 48)
        return 0;
    v = (x >> 2) * 13 + (y >> 2) * 11 + game.s.world_seed;
    return (x & 3) * 4 + (y & 3) == (v & 15);
}

/* f_1b05_1886: huts use the same hash with a 1-in-32 chance. */
int has_hut(int terrain, int x, int y)
{
    int v;

    if (terrain == TERRAIN_OCEAN || (game.s.visibility[x][y] & 1) || y <= 1 || y >= 48)
        return 0;
    v = (x >> 2) * 13 + (y >> 2) * 11 + game.s.world_seed + 8;
    if ((x & 3) * 4 + (y & 3) != (v & 31))
        return 0;
    return !(map_improvements(x, y) & IMP_CITY);
}

/* f_1b05_1503: mark a square as having units, keeping the recorded owner. */
void map_set_units_present(int x, int y)
{
    map_put_pixel(x + 160, y, (map_pixel(x + 160, y) & 7) + 8);
}

/* f_1b05_194a: size of the continent or ocean (x, y) belongs to. */
int body_area(int x, int y)
{
    int c = map_continent(x, y) & 63;

    return map_terrain(x, y) == TERRAIN_OCEAN ? game.s.sea_area[c] : game.s.land_area[c];
}
