#ifndef DIPLOMACY_H
#define DIPLOMACY_H

void civ_contact(int civ, int other, int x, int y);
int  ai_wants_war(int civ, int other);
int  exchange_advances(int civ, int other);
void diplomacy_set(int civ, int other, int flag);
void diplomacy_clear(int civ, int other, int flag);
int  civ_nuclear_count(int civ);
void ai_diplomacy(int civ);                    /* f_14f6_000c */

#endif
