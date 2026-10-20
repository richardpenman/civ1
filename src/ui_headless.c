#include <stdio.h>
#include "ui.h"

int ui_verbose;

void ui_redraw_square(int x, int y) { (void)x; (void)y; }
void ui_redraw_around(int x, int y) { (void)x; (void)y; }

static int next_answer(void);

/* (A message box is a dialog too: it uses up a scripted answer, as in the oracle.) */
void ui_message(const char *text, int style)
{
    (void)style;
    (void)next_answer();
    if (ui_verbose)
        printf("[message] %s\n", text);
}

void ui_help_message(const char *key)
{
    if (ui_verbose)
        printf("[help] %s\n", key);
}

void ui_newspaper(const News *news, const char *text)
{
    (void)news;
    if (ui_verbose)
        printf("[newspaper] %s\n", text);
}


int message_lock = -1;


void ui_advance_screen(int advance, int style) { (void)advance; (void)style; }
void ui_government_picture(void) { }
void ui_intelligence_report(void) { }

void ui_city_work_square(int city, int square) { (void)city; (void)square; }
void ui_city_happy(int city, int step, int arg) { (void)city; (void)step; (void)arg; }
void ui_city_unit(int city, int civ, int unit, int home) { (void)city; (void)civ; (void)unit; (void)home; }
void ui_redraw_terrain(int x, int y) { (void)x; (void)y; }
void ui_city_screen_begin(int city) { (void)city; }
void ui_city_screen_resources(int city) { (void)city; }
void ui_city_screen_end(int city) { (void)city; }
int  ui_city_screen_again(int city) { (void)city; return 0; }
void ui_city_view(int city, int mode, const signed char *grid) { (void)city; (void)mode; (void)grid; }
void ui_nuclear_explosion(int x, int y) { (void)x; (void)y; }

void ui_production_advice(int city, int military, int domestic) { (void)city; (void)military; (void)domestic; }
/* Dialogs are answered from headless_script (tests set it; the oracle answers alike), then
 * with headless_answer. */
int headless_answer;
int headless_script[16], headless_script_len, headless_script_pos;

static int next_answer(void)
{
    if (headless_script_pos < headless_script_len)
        return headless_script[headless_script_pos++];
    return headless_answer;
}
int  ui_choose(const char *menu, int small_font, int x, int y, int advisor, int help)
{ (void)menu; (void)small_font; (void)x; (void)y; (void)advisor; (void)help; return next_answer(); }
int  ui_help_requested(void) { return 0; }
void ui_encyclopedia(int item) { (void)item; }
void ui_status_panel(void) { }
void ui_discovery(int civ, int advance, const char *text) { (void)civ; (void)advance; ui_message(text, 0); }
void ui_research_advice(int advance) { (void)advance; }
void ui_encyclopedia_advance(int advance) { (void)advance; }
void ui_redraw_map(void) { }
void ui_redraw_unit(int civ, int unit) { (void)civ; (void)unit; }
int  ui_menu(const char *menu, int disabled) { (void)menu; (void)disabled; return next_answer(); }
int  ui_ask(const char *text, int style) { ui_message(text, style); return next_answer(); }
void ui_combat(int civ, int unit, int dciv, int dunit, int won) { (void)civ; (void)unit; (void)dciv; (void)dunit; (void)won; }
void ui_sound(int n) { (void)n; }
void ui_animate_move(int civ, int unit, int dir) { (void)civ; (void)unit; (void)dir; }
int  ui_name_city(int city) { (void)city; return 1; }
void ui_city_founded(int city) { (void)city; }
void ui_city_screen(int civ, int city) { (void)civ; (void)city; }
int  ui_ask_emissary(const char *text) { (void)text; return next_answer(); }
void ui_city_conquered(int city, int civ, const char *text) { (void)city; (void)civ; (void)text; }
void ui_text_section(const char *key, const char *a, const char *b, char *out, int size)
{
    (void)a; (void)b;
    snprintf(out, (size_t)size, "%s", key);
}
void ui_intelligence_report_civ(int civ) { (void)civ; }
void ui_draw_view(int civ, int x, int y, int start) { (void)civ; (void)x; (void)y; (void)start; }
void ui_new_government(int civ) { (void)civ; }
int ui_envoy_proposal(int civ, const char *section, const char *a, const char *b)
{
    (void)civ; (void)section; (void)a; (void)b;
    return 0;
}
void ui_play_theme(int theme) { (void)theme; }
void ui_wonder_view(int building, int style) { (void)building; (void)style; }
void ui_flash_city(int x, int y) { (void)x; (void)y; }
void ui_historian_begin(const char *title) { if (ui_verbose) printf("[history] %s\n", title); }
void ui_historian_line(int civ, const char *line) { (void)civ; if (ui_verbose) printf("[history] %s\n", line); }
void ui_historian_end(void) { }
void ui_tutorial_start(void) { }
void ui_civilization_lost(void) { }
void ui_game_over(int civ) { (void)civ; }
void ui_final_screens(void) { }
void ui_autosave(int slot) { (void)slot; }
int  ui_palace(int *style) { (void)style; return -1; }
void ui_replay(void) { }
void ui_spaceship_arrives(int civ) { (void)civ; }
void ui_world_progress(void) { }
void ui_world_progress_end(void) { }
void ui_custom_tribe(int civ) { (void)civ; }
void ui_leader_name(int civ) { (void)civ; }
void ui_game_intro(int civ) { (void)civ; }
void ui_frame_screen(int style) { (void)style; }
void ui_spaceship_screen(int civ) { (void)civ; }
void ui_spaceship_screen_end(void) { }
int  ui_spaceship_star(int i, int x, int y, int colour) { (void)i; (void)x; (void)y; (void)colour; return 1; }
int  ui_spaceship_figures(int civ, const SpaceshipFigures *f, int frames, int may_launch)
{
    (void)civ; (void)f; (void)may_launch;
    return frames > 0 ? frames : 1;
}
int  ui_launch_requested(void) { return 0; }
int  ui_spaceship_choose(const char *menu, int disabled) { (void)menu; (void)disabled; return 0; }
int  ui_choose_spaceship(int disabled) { (void)disabled; return -1; }
void ui_spaceship_news(int civ, const char *text) { (void)civ; (void)text; }
void ui_female_leader(int on) { (void)on; }
int  ui_receive_envoy(int civ, int embassy) { (void)civ; (void)embassy; return next_answer(); }
void ui_intelligence_report_for(int civ) { (void)civ; }
void ui_diplomacy_screen(int civ) { (void)civ; }
void ui_diplomacy_screen_end(void) { }
void ui_envoy_greeting(int civ, int variant) { (void)civ; (void)variant; }
void ui_envoy_says_greeting(int civ) { (void)civ; (void)next_answer(); }
int  ui_envoy_says(int civ, const char *t, const char *a, const char *b) { (void)civ; (void)t; (void)a; (void)b; return next_answer(); }
int  ui_envoy_asks(int civ, const char *t, const char *a, const char *b, int mood)
{
    (void)civ; (void)t; (void)a; (void)b; (void)mood;
    return next_answer();
}
void ui_envoy_message(const char *key) { (void)key; (void)next_answer(); }
int  ui_envoy_threat(int civ, int disabled) { (void)civ; (void)disabled; return next_answer(); }
int  ui_human_command(int civ, int unit) { (void)civ; (void)unit; return ' '; }
int  ui_end_of_turn(int civ) { (void)civ; return 0; }
void ui_unit_panel(int civ, int unit) { (void)civ; (void)unit; }
void ui_turn_marker(int busy) { (void)busy; }
