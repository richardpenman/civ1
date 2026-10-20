/* Cities: yields, growth, production (original seg 0d07 and overlays). */
#ifndef CITY_H
#define CITY_H

enum { YIELD_FOOD, YIELD_SHIELDS, YIELD_TRADE };
enum { SPEC_NONE, SPEC_TAXMAN, SPEC_SCIENTIST, SPEC_ENTERTAINER };

/* What city_process() works out for one city (DGROUP 0x705A.. and friends in the original). */
typedef struct {
    int yield[4];            /* g_705a: food, shields, trade (after routes); [3] = luxuries (g_7060) */
    int lux, happy, unhappy, science, tax, corruption;
    int support;             /* g_edd8: units the city supports */
    int shield_upkeep;       /* g_e216 */
    int settlers;            /* g_f2e6 */
    int settler_food;        /* food per settler: 1, or 2 beyond despotism */
    int away;                /* g_64c8: military units away (cause unhappiness in republics) */
    int specialists;         /* g_f7d8 */
    int free;                /* citizens not working a square */
    int enemy_near;          /* g_7068: the human has units on a city square */
    int capital_distance;    /* g_6ab0 */
    int pollution_factor;    /* g_c7a2 */
    int pollution_divisor;   /* g_6c18 */
    int box;                 /* food/shield box size per step: 10, or less for the AI */
} CityCalc;

extern CityCalc calc;
extern int ai_support_score, ai_unhappy_score, show_city_again, disorder_shown, research_bonus;

int  city_process(int city, long mode);
void city_screen(int civ, int city);          /* f_0ad5_03e5 */
extern int city_screen_dirty;
int  tile_yield(int city, int x, int y, int kind);
int  tile_yield_gov(int city, int gov_civ, int x, int y, int kind);
extern int calc_city;   /* g_64c2 */
void add_pollution(int x, int y);
void nuclear_fallout(int x, int y);
void city_unpack_fortified(int city);
void city_pack_fortified(int city);
void destroy_city(int city, int x, int y);
int  choose_production(int civ, int city);       /* f_0ad5_041c */
int  city_specialist(int i);                     /* f_0d07_6c89: of calc_city */
void city_set_specialist(int i, int type);       /* f_0d07_6c56 */
extern uint8_t city_taken[5][5];                 /* squares worked by other cities [dx+2][dy+2] */
int  caravan_arrive(int civ, int unit, int city);  /* f_1439_0940 */

/* Not yet ported (stubs in todo.c). */

void spaceship_part_human(int civ, int part);    /* o_18_0f55 */
int  spaceship_part_ai(int civ, int part);       /* o_18_0d3f */
void spaceship_launch(int civ);                  /* o_18_1595 */
extern int ai_flag_8f98;                         /* g_8f98 */

#endif
