/* Spaceships (original overlay 18). */
#ifndef SPACESHIP_H
#define SPACESHIP_H

typedef struct {
    int population, support, energy, mass, fuel, flight_time, success;
} SpaceshipFigures;

int  spaceship_check(int civ, int frames, int offer_launch);  /* o_18_0249 */
int  spaceship_part_ai(int civ, int kind);                    /* o_18_0d3f */
void spaceship_part_human(int civ, int kind);                 /* o_18_0f55 */
void spaceship_launch(int civ);                               /* o_18_1595 */
void spaceship_view(void);                                    /* o_18_14f9 */
void spaceship_stars_step(void);                              /* o_18_0c39 */

#endif
