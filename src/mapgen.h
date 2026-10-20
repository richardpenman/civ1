/* Making a new world (original overlay 7). */
#ifndef MAPGEN_H
#define MAPGEN_H

#include <stdint.h>

typedef struct {
    int land, temperature, climate, age;   /* 0..2 each: g_7e76, g_7e78, g_7e7a, g_7e7c */
} WorldParams;

extern WorldParams world;
extern int land_mass;            /* g_6780: never reset by the game (a fresh run starts at 0) */
extern uint8_t sea_links[260];   /* goto.c */

void generate_world(void);       /* o_07_0012 */
void build_land_links(void);     /* o_07_1142 */
void build_sea_links(void);      /* o_07_13df */

#endif
