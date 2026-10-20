/* Starting a new game (original overlay 5). */
#ifndef NEWGAME_H
#define NEWGAME_H

typedef struct {
    int difficulty;        /* 0 Chieftain .. 4 Emperor */
    int civ_count_choice;  /* menu line: 0 = 7 civilizations .. 4 = 3 */
    int tribe_choice;      /* menu line, -1 = random */
} NewGameChoices;

extern int no_sound_option;
void fresh_state(void);                       /* the variables before any game */
void new_game(const NewGameChoices *choices); /* o_05_0000 */

#endif
