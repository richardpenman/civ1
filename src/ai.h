/* Computer players (original seg 15da, 14f6, 1c9d). */
#ifndef AI_H
#define AI_H

#include "game.h"

#define AI_GOALS 32

/* Per civ, a short list of map squares worth attention, best first (not saved). */
typedef struct {
    int8_t x[AI_GOALS], y[AI_GOALS];
    uint8_t type[AI_GOALS];    /* 1 attack, 2 defend, 3 naval, 4 ...; 0xFF free */
    int8_t prio[AI_GOALS];
} AiGoals;

extern AiGoals ai_goals[MAX_CIVS];
extern int ai_diplomat_civs;  /* g_2a1e: civs whose diplomats may go stealing */
extern int ai_at_war;         /* g_6e54 */

void ai_add_goal(int civ, int x, int y, int type, int prio);
void ai_clear_continent_goals(int civ);
void ai_rechoose_production(int civ, int continent);
void city_rechoose_production(int city);
void ai_forget_spot(int civ, int type, int x, int y, int range);
void ai_go_for_undefended(int city);
void ai_add_spot(int civ, int x, int y, int type, int value);
int  ai_terraform_kind(int x, int y);
void ai_strategy(int civ);                 /* f_15da_000c */
void ai_reset_goals(int civ);              /* ai_strategy2, f_15da_3452 */
void set_government(int civ, int gov);     /* f_14f6_04a9 */
extern uint8_t ai_block_flags[260];  /* far 2786:1498: per 4x4 block, set by ai_strategy */

#endif
