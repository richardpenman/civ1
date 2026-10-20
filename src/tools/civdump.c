/* civdump: load a .SVE/.MAP pair, print a summary and check the save round-trips. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../save.h"
#include "../map.h"



static void print_map(void)
{
    static const char sym[] = "dpgfhmtasjorOHGFCWTSOJFR";
    int x, y;

    for (y = 0; y < MAP_H; y++) {
        for (x = 0; x < MAP_W; x++) {
            int t = map_terrain(x, y);
            if (t < 0)
                putchar('?');
            else
                putchar(t == TERRAIN_OCEAN ? '.' : sym[t + (has_special_resource(x, y) ? TERRAIN_SPECIAL : 0)]);
        }
        putchar('\n');
    }
}

int main(int argc, char **argv)
{
    static uint8_t orig[SVE_SIZE], again[SVE_SIZE];
    SaveData *s = &game.s;
    FILE *f;
    int c, i, n = 0;

    if (argc < 3) {
        fprintf(stderr, "usage: civdump GAME.SVE GAME.MAP [-m]\n");
        return 2;
    }
    if (load_game(argv[1], argv[2], &game)) {
        fprintf(stderr, "cannot load %s / %s\n", argv[1], argv[2]);
        return 1;
    }
    printf("turn %d, year %d %s, difficulty %d, human civ %d (%s), alive mask %02x\n",
           s->turn, abs(s->year), s->year < 0 ? "BC" : "AD", s->difficulty,
           s->human_civ, s->civ_names[s->human_civ], s->civs_alive & 0xFF);
    for (c = 0; c < MAX_CIVS; c++)
        printf("  civ %d %-12.12s %-14.14s gov %-10s gold %5d advances %2d cities %2d units %3d score %d\n",
               c, s->civ_names[c], s->leader_names[c],
               s->government[c] >= 0 && s->government[c] < NUM_GOVERNMENTS ? government_names[s->government[c]] : "?",
               s->treasury[c], s->advances_known[c], s->city_count[c], s->unit_count[c], s->score[c]);
    for (i = 0; i < MAX_CITIES; i++) {
        const City *ct = &s->cities[i];
        if (ct->status == NONE8)
            continue;
        if (n++ < 12)
            printf("  city %3d %-13.13s owner %d at (%2d,%2d) size %2d producing %4d food %3d shields %3d\n",
                   i, s->city_names[ct->name], ct->owner, ct->x, ct->y, ct->size, ct->producing,
                   ct->food, ct->shields);
    }
    printf("  %d cities in use\n", n);

    f = fopen(argv[1], "rb");
    if (!f || fread(orig, 1, SVE_SIZE, f) != SVE_SIZE)
        return 1;
    fclose(f);
    save_encode(s, again);
    if (memcmp(orig, again, SVE_SIZE)) {
        for (i = 0; i < SVE_SIZE && orig[i] == again[i]; i++)
            ;
        printf("ROUND TRIP MISMATCH at offset 0x%04x\n", i);
        return 1;
    }
    printf("round trip OK\n");
    if (argc > 3 && !strcmp(argv[3], "-m"))
        print_map();
    return 0;
}
