/* Entry points for the Python oracle tests (tests/), which load libciv.so with ctypes. */
#include <string.h>
#include "save.h"
#include "util.h"



int capi_load(const char *sve, const char *map)
{
    return load_game(sve, map, &game);
}

void capi_save_bytes(uint8_t *buf)
{
    save_encode(&game.s, buf);
}

void capi_load_bytes(const uint8_t *buf)
{
    save_decode(buf, SVE_SIZE, &game.s);
}

uint8_t *capi_map_pixels(void)
{
    return &game.map.pixels[0][0];
}

void capi_srand(unsigned seed)
{
    game_srand(seed);
}

unsigned long capi_rand_state(void)
{
    return game_rand_state();
}

int capi_year(void)
{
    return game.s.year;
}

int capi_save(const char *sve, const char *map)
{
    return save_game(sve, map, &game);
}
