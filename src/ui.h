/* What the game logic asks of the user interface.  ui_headless.c implements these
 * without a screen (for tests and tools); the SDL front end replaces them. */
#ifndef UI_H
#define UI_H

void ui_redraw_square(int x, int y);
void ui_redraw_around(int x, int y);
void ui_message(const char *text, int style);       /* style = g_2f4e picture/frame */
void ui_help_message(const char *key);               /* "*KEY" section of HELP.TXT etc. */
/* A newspaper page (o_21_0000): paper of `city` (-1/-2: none), crowd heights, paper name
 * 0..3 ("Weekly", "Today", "Times", "Tribune"), EXTRA!/FLASH, headline *NEWSA + n. */
typedef struct {
    int city, crowd, title, extra, headline;
    unsigned char crowd_y[16];
} News;
void ui_newspaper(const News *news, const char *text);
void ui_advance_screen(int advance, int style);      /* the human discovered an advance (o_08_062a;
                                                     * style: its random border) */
void ui_government_picture(void);
void ui_intelligence_report(void);
void ui_city_work_square(int city, int square);
/* The city screen's HAPPY page, step by step in city_happiness() (mode 1). */
enum { HAPPY_ROW, HAPPY_LUX_ROW, HAPPY_BUILDING, HAPPY_NEXT, HAPPY_UNIT, HAPPY_AWAY, HAPPY_WONDERS_ROW };
void ui_city_happy(int city, int step, int arg);
/* Each unit as city_support() meets it (mode 1); home: the city supports it. */
void ui_city_unit(int city, int civ, int unit, int home);
void ui_redraw_terrain(int x, int y);
void ui_turn_marker(int busy);   /* the 6x6 square at (2,192): grey while a civ's turn starts */
void ui_unit_panel(int civ, int unit);                 /* f_0402_4010 (unit -1: empty) */                 /* f_1b05_03b1: a square without its units */
void ui_city_screen_begin(int city);
void ui_city_screen_resources(int city);
void ui_city_screen_end(int city);
int  ui_city_screen_again(int city);                  /* input: 1 if the city changed */
void ui_city_view(int city, int mode, const signed char *grid);  /* overlay 19; grid: city_view_layout() */
void ui_nuclear_explosion(int x, int y);
void ui_production_advice(int city, int military, int domestic);
int  ui_choose(const char *menu, int small_font, int x, int y, int advisor, int help);
                         /* index of the chosen line (0 = first option); at (x, y), -1 centred;
                          * advisor (-1 none, 3-6 the cabinet) speaks; help: "(HELP AVAILABLE)" */
int  ui_help_requested(void);
void ui_encyclopedia(int item);
void ui_status_panel(void);                          /* f_0237_107a: the drawing (status_report() is the logic) */
void ui_discovery(int civ, int advance, const char *text);   /* "wise men discover..." */
void ui_research_advice(int advance);
void ui_encyclopedia_advance(int advance);
void ui_redraw_map(void);                            /* f_0237_1b40 */
void ui_redraw_unit(int civ, int unit);              /* f_1b05_0e1b */
int  ui_menu(const char *menu, int disabled);        /* disabled: bit n greys out option n */
int  ui_ask(const char *text, int style);           /* a message with options; returns the choice */
void ui_combat(int civ, int unit, int dciv, int dunit, int attacker_won);
void ui_sound(int n);
void ui_animate_move(int civ, int unit, int dir);    /* f_0860_1d16 */
int  ui_name_city(int city);                         /* the human names a new city; 0 = cancelled */
void ui_city_founded(int city);
void ui_city_screen(int civ, int city);              /* f_0ad5_03e5 */
int  ui_ask_emissary(const char *text);              /* o_06_251d */
void ui_city_conquered(int city, int civ, const char *text);   /* o_19_166c, after the city view */
void ui_text_section(const char *key, const char *a, const char *b, char *out, int size);
void ui_intelligence_report_civ(int civ);            /* o_14_115e */
/* The player's order for the human's unit: a direction 1..8 or a command key for
 * unit_command(); the front end handles its own keys (menus, the view) before returning. */
int  ui_human_command(int civ, int unit);
/* All units have moved: "End of Turn".  1 if the player woke units to move them now. */
int  ui_end_of_turn(int civ);
extern char ui_rplc[2][40];                 /* $RPLC1, $RPLC2 for the next help message */
void ui_draw_view(int civ, int x, int y, int dissolve_start);
void ui_new_government(int civ);
void ui_custom_tribe(int civ);              /* o_23_0173: names for a random tribe */
void ui_leader_name(int civ);               /* o_23_00d6: the player's name */
void ui_game_intro(int civ);                /* the *INIT text and the government picture */
void ui_frame_screen(int style);
/* The spaceship screen (overlay 18). */
#include "spaceship.h"
void ui_spaceship_screen(int civ);
void ui_spaceship_screen_end(void);
int  ui_spaceship_star(int i, int x, int y, int colour);   /* 1 if drawn (the place was dark) */
/* Show the figures; animate up to `frames` frames (-1: until a key) and return how many
 * were shown; `may_launch`: offer the LAUNCH button. */
int  ui_spaceship_figures(int civ, const SpaceshipFigures *f, int frames, int may_launch);
int  ui_launch_requested(void);
int  ui_spaceship_choose(const char *menu, int disabled_mask);
int  ui_choose_spaceship(int disabled_mask);
void ui_spaceship_news(int civ, const char *text);
/* Diplomacy (overlay 6).  Envoy dialogs return the line chosen (1 = accept), -1 for none. */
void ui_female_leader(int on);              /* "Empress"/"Queen" titles while she talks */
int  ui_receive_envoy(int civ, int embassy); /* 0 receive, 1 refuse, 2 intelligence report */
void ui_intelligence_report_for(int civ);
void ui_diplomacy_screen(int civ);
void ui_diplomacy_screen_end(void);
void ui_envoy_greeting(int civ, int variant);
void ui_envoy_says_greeting(int civ);
int  ui_envoy_says(int civ, const char *text, const char *subst1, const char *subst2);
/* `text` is a KING.TXT section key ("*PEACE") or the words themselves; `mood`: 0 calm,
 * 1 neutral, 2 angry, 4 ... (the leader's face). */
int  ui_envoy_asks(int civ, const char *text, const char *subst1, const char *subst2, int mood);
void ui_envoy_message(const char *key);
int  ui_envoy_threat(int civ, int disabled_mask);
void ui_world_progress(void);               /* o_07_176e: the story while the world is made */
void ui_world_progress_end(void);
void ui_tutorial_start(void);               /* f_0402_4010, o_02_17d2 */
void ui_civilization_lost(void);            /* o_02_1523 */
void ui_game_over(int spaceship_civ);       /* f_0237_08a2: replay, score, ... */
void ui_final_screens(void);                /* f_0237_090c */
void ui_autosave(int slot);                 /* o_11_0368 */
/* o_17_0000: the part of the palace to improve (palace_section_open) and its style 0..2;
 * -1 for none. */
int  ui_palace(int *style);
void ui_replay(void);                       /* o_09_0dde */
void ui_spaceship_arrives(int civ);         /* o_02_0bd0 */
void ui_play_theme(int theme);              /* f_01a7_046e */
void ui_wonder_view(int building, int style);   /* o_08_062a (style: its random border) */
void ui_flash_city(int x, int y);           /* f_0402_3e8c */
void ui_historian_begin(const char *title); /* o_12_09d6's screen */
void ui_historian_line(int civ, const char *line);
void ui_historian_end(void);
/* An envoy of `civ` proposes something (text section `section` with two substitutions);
 * returns the choice (1 = accept).  o_06_251d */
int  ui_envoy_proposal(int civ, const char *section, const char *subst1, const char *subst2);
     /* o_05_1adf + o_05_1b8b: picture, newspaper, cabinet */
extern int message_lock;                             /* g_38e4: -1 when messages may be shown */

#endif
