#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "turn.h"

Game game;
const char *data_dir = "Dos/Civ1";

/* The original game's files: $CIV_ORIG, else ~/Dos/Civ1 (--data overrides both). */
__attribute__((constructor)) static void default_data_dir(void)
{
    static char path[512];
    const char *env = getenv("CIV_ORIG"), *home = getenv("HOME");

    if (env && *env)
        data_dir = env;
    else if (home && *home) {
        snprintf(path, sizeof path, "%s/Dos/Civ1", home);
        data_dir = path;
    }
}
#include "ai.h"
#include "city.h"
#include "civ.h"
#include "move.h"

int cur_player;
int stack_owner;
int quiet;
int count_losses;
int show_all;
int replay_suppress = -1;
int cur_civ;

void not_ported(const char *what)
{
    fprintf(stderr, "not ported yet: %s\n", what);
    abort();
}

/* The original's non-saved globals start out as in the EXE (mostly zero) on load. */
void reset_transient_state(void)
{
    cur_player = 0;
    stack_owner = 0;
    quiet = 0;
    count_losses = 0;
    show_all = 0;
    replay_suppress = -1;
    cur_civ = 0;
    ai_diplomat_civs = 0;
    ai_at_war = 0;
    ai_flag_8f98 = 0;
    ai_support_score = 0;
    ai_unhappy_score = 0;
    show_city_again = 0;
    disorder_shown = 0;
    research_bonus = 0;
    memset(civ_unk_e12c, 0, sizeof civ_unk_e12c);
    quit_flag = 0;
    game_won_conquest = 0;
    goto_reset();
    inciting = 0;
    last_dir = 0;
    last_terrain = 0;
    view_x = view_y = 0;
    memset(ai_goals, 0, sizeof ai_goals);
    memset(ai_block_flags, 0, sizeof ai_block_flags);
    load_marker = -3;            /* as the original's loader leaves it */
    pop_milestone = 1;
    retired = 0;
    city_screen_dirty = 0;
}
