/* Small shared helpers (original seg 1dde and the MSC runtime). */
#ifndef UTIL_H
#define UTIL_H

void game_srand(unsigned seed);
unsigned long game_rand_state(void);
int  game_rand(void);                 /* 0..32767, the MSC 5.1 rand() sequence */
int  rnd(int n);                      /* 0..n-1 */
int  clamp(int v, int lo, int hi);
int  distance(int dx, int dy);        /* max + min/2 of |dx|, |dy| */
int  map_distance(int x1, int y1, int x2, int y2);  /* wraps east-west */
int  map_distance2(int dx, int dy);   /* 2*max + min, dx wrapped */

int  city_at(int x, int y);
int  nearest_city(int x, int y, int *dist);
int  nearest_unit(int civ, int exclude, int x, int y, int *dist);
int  civ_population(int civ);         /* in units of 10,000 people */
int  sign(int v);

#endif
