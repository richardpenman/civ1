/* The SDL front end's side of ui.h: what the game logic asks of the player and the screen,
 * the human's orders (map, menus, advisors) and the city screen. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "gfx.h"
#include "font.h"
#include "assets.h"
#include "mapview.h"
#include "dialog.h"
#include "front.h"
#include "palace.h"
#include "article.h"
#include "sound.h"
#include "../game.h"
#include "../map.h"
#include "../unit.h"
#include "../util.h"
#include "../move.h"
#include "../city.h"
#include "../civ.h"
#include "../view.h"
#include "../turn.h"
#include "../save.h"
#include "../ai.h"
#include "../spaceship.h"
#include "../ui.h"

#define S (&game.s)

int message_lock = -1;
const char *save_dir = "save";

static int dirs_of_key(int k)
{
    switch (k) {
    case '8': return 1;
    case '9': return 2;
    case '6': return 3;
    case '3': return 4;
    case '2': return 5;
    case '1': return 6;
    case '4': return 7;
    case '7': return 8;
    }
    return 0;
}

/* ------------------------------------------------------------------------------------- */
/* The main screen. */

static int panel_civ = -1, panel_unit = 128;   /* panel_civ -2: emptied */

/* f_0237_0151 / 01cb: a civ's cities and diplomacy are being done (grey), done (white). */
void ui_turn_marker(int busy)
{
    gfx_fill(2, 192, 6, 6, busy ? 8 : 15);
    gfx_present();
}

void ui_unit_panel(int civ, int unit)
{
    if (unit < 0) {
        panel_civ = -2;
        panel(0, 97, 80, 103);
    } else {
        panel_civ = civ;
        panel_unit = unit;
        draw_unit_panel(civ, unit);
    }
    gfx_present();
}

void main_screen(void)
{
    draw_menu_bar();
    draw_view(S->human_civ, view_x, view_y, 0);
    draw_status_panel();
    if (panel_civ >= 0)
        draw_unit_panel(panel_civ, panel_unit);
    else if (panel_civ == -1 && S->units[S->human_civ][0].type != NONE8)
        draw_unit_panel(S->human_civ, 0);    /* (a new game: the panel shows the first unit) */
    else
        panel(0, 97, 80, 103);
    gfx_cycle_start();
    gfx_present();
}

void ui_redraw_square(int x, int y)
{
    if (fast_view == 1)
        return;
    draw_square_units(wrap_x(x), y);
    gfx_present();
}

void ui_redraw_terrain(int x, int y)
{
    if (fast_view == 1)
        return;
    draw_square(wrap_x(x), y);
    gfx_present();
}

void ui_redraw_around(int x, int y)
{
    int k;

    if (fast_view == 1)
        return;
    for (k = 0; k < 9; k++)
        if (map_in_bounds(wrap_x(x + around_dx[k]), y + around_dy[k]))
            draw_square_units(wrap_x(x + around_dx[k]), y + around_dy[k]);
    gfx_present();
}

/* f_0237_1b40: the map, the status panel and an empty unit panel. */
void ui_redraw_map(void)
{
    panel_civ = -2;
    main_screen();
    era_palette();
    gfx_cycle_start();
}

void ui_redraw_unit(int civ, int unit)
{
    Unit *p = &S->units[civ][unit];

    if (fast_view == 1 || p->type == NONE8)
        return;
    draw_square_units(p->x, p->y);
    gfx_present();
}

void ui_draw_view(int civ, int x, int y, int start)
{
    if (fast_view == 1)
        return;
    draw_menu_bar();
    draw_view(civ, x, y, start);
    gfx_present();
}

void ui_status_panel(void)
{
    if (fast_view == 1)
        return;
    draw_status_panel();
    gfx_present();
}

static int frame_style;            /* the border the engine picked (f_0860_25e1) */

void ui_frame_screen(int style)
{
    frame_style = style;
}

static int in_view_square(int x, int y)      /* f_0402_44b8 */
{
    int px = wrap_x(x - view_x) * 16 + 80, py = (y - view_y) * 16 + 8;

    return px >= 80 && px < 320 && py >= 8 && py <= 192;
}

/* f_0860_1d16: the unit slides to the next square a pixel a tick (17 steps), the square it
 * leaves showing what stays there. */
void ui_animate_move(int civ, int unit, int dir)
{
    static uint8_t back[SCR_H][SCR_W];
    Unit *p = &S->units[civ][unit];
    int x0, y0, i, x, y, r, nx;

    if (fast_view == 1)
        return;
    draw_square(p->x, p->y);
    if (!in_view_square(p->x, p->y) || !in_view_square(p->x + around_dx[dir], p->y + around_dy[dir])) {
        gfx_present();
        return;
    }
    nx = (int8_t)p->next;
    if (nx != -1 && (map_terrain(p->x, p->y) != TERRAIN_OCEAN ||
                     S->unit_types[S->units[civ][nx].type].domain == DOMAIN_SEA))
        draw_unit(civ, nx);
    memcpy(back, screen, sizeof back);
    x0 = wrap_x(p->x - view_x) * 16 + 80;
    y0 = (p->y - view_y) * 16 + 8;
    for (i = 0; i <= 16; i++) {
        x = around_dx[dir] * i + x0;
        y = around_dy[dir] * i + y0;
        sprite_draw(unit_sprite(civ, p->type), x + 1, y + 1);
        gfx_present();
        gfx_delay(1000 / 60);
        for (r = 0; r < 16; r++)
            if (y + r >= 0 && y + r < SCR_H && x >= 0 && x + 16 <= SCR_W)
                memcpy(&screen[y + r][x], &back[y + r][x], 16);
    }
    gfx_present();
}

/* f_1a19_000e's display: the attacker lunges at the defender, the loser blows up. */
void ui_combat(int civ, int unit, int dciv, int dunit, int attacker_won)
{
    static uint8_t back[SCR_H][SCR_W];
    Unit *a = &S->units[civ][unit], *d = &S->units[dciv][dunit];
    int ta = a->type, td = d->type, heavy, snd, x0, y0, sx, sy, i, x, y, lose;

    if (fast_view == 1)
        return;
    draw_square(a->x, a->y);
    /* the sound: heavy fighting if the winner hits or holds hard */
    if (attacker_won)
        heavy = S->unit_types[ta].attack > 4 || S->unit_types[ta].defense > 2;
    else
        heavy = S->unit_types[td].attack > 4 || S->unit_types[td].defense > 2;
    if (attacker_won && ta == 15)
        snd = 43;
    else if ((attacker_won && civ == S->human_civ) || (!attacker_won && dciv == S->human_civ))
        snd = heavy ? 40 : 38;
    else
        snd = heavy ? 41 : 39;
    ui_sound(snd);
    draw_unit(dciv, dunit);
    if (a->next != NONE8)
        draw_unit(civ, (int8_t)a->next);
    memcpy(back, screen, sizeof back);
    x0 = wrap_x(a->x - view_x) * 16 + 80;
    y0 = (a->y - view_y) * 16 + 8;
    sx = d->x > a->x ? 1 : d->x < a->x ? -1 : 0;
    sy = d->y > a->y ? 1 : d->y < a->y ? -1 : 0;
    if ((d->x == 0 && a->x == 79) || (d->x == 79 && a->x == 0))
        sx = -sx;
    for (i = 0; i <= 10; i++) {
        x = i * sx + x0;
        y = i * sy + y0;
        sprite_draw(unit_sprite(civ, ta), x + 1, y + 1);
        gfx_present();
        gfx_delay(2 * 1000 / 60);           /* (f_0181_0136(2): the timer runs at 60 Hz) */
        for (lose = 0; lose < 16; lose++)
            if (y + lose >= 0 && y + lose < SCR_H && x >= 0 && x + 16 <= SCR_W)
                memcpy(&screen[y + lose][x], &back[y + lose][x], 16);
    }
    draw_square_units(a->x, a->y);
    draw_square_units(wrap_x(d->x), d->y);
    memcpy(back, screen, sizeof back);
    lose = attacker_won ? 16 : 0;
    x = lose * sx + x0;
    y = lose * sy + y0;
    for (i = 0; i < 8; i++) {
        sprite_draw(icon_sprite(i + 32), x + 1, y + 1);
        gfx_present();
        gfx_delay(4 * 1000 / 60);
        for (lose = 0; lose < 16; lose++)
            if (y + lose >= 0 && y + lose < SCR_H && x >= 0 && x + 16 <= SCR_W)
                memcpy(&screen[y + lose][x], &back[y + lose][x], 16);
    }
    gfx_present();
}

/* f_01a7_046e: the sound driver plays sound n, if Sound is on in the options. */
void ui_sound(int n)
{
    if (S->unk_19c0 & 16)
        sound_play(n);
}

void ui_play_theme(int theme)
{
    ui_sound(theme);
}

void ui_flash_city(int x, int y)
{
    int i, px = wrap_x(x - view_x) * 16 + 80, py = (y - view_y) * 16 + 8;

    if (fast_view == 1 || px < 80 || px >= 320 || py < 8 || py > 192)
        return;
    screen_save();
    for (i = 0; i < 4; i++) {
        screen_restore();
        if (!(i & 1))
            gfx_rect(px, py, 15, 15, 15);
        gfx_delay(80);
    }
    screen_restore();
    gfx_present();
}

void ui_nuclear_explosion(int x, int y)
{
    int i, px = wrap_x(x - view_x) * 16 + 80, py = (y - view_y) * 16 + 8;

    screen_save();
    for (i = 0; i < 8; i++) {
        gfx_fill(px - 16 - i * 2, py - 16 - i * 2, 48 + i * 4, 48 + i * 4, i & 1 ? 14 : 12);
        gfx_delay(50);
    }
    screen_restore();
    gfx_present();
}

/* ------------------------------------------------------------------------------------- */
/* Messages. */

static void show(const char *text, const char *a, const char *b, int flags)
{
    char buf[4096];

    text_get(text, buf, sizeof buf, a, b);
    gfx_present();
    dialog(buf, -1, -1, 0, flags);
}

void ui_message(const char *text, int style)
{
    (void)style;
    show(text, NULL, NULL, 0);
}

void ui_help_message(const char *key)
{
    char buf[4096];
    int f = text_section_raw(key, buf, sizeof buf);

    if (f >= 0 && !strcmp(text_file_name(f), "HELP.TXT")) {
        /* o_04_02d3: a Civilization Note, with Instant Advice and before 1 AD only */
        static uint8_t under[SCR_H][SCR_W];
        char lines[24][96];
        int n = 0, i, len;
        char *p = buf;
        if (!(S->unk_19c0 & 1) || S->year >= 0)
            return;
        text_substitute(buf, sizeof buf, ui_rplc[0][0] ? ui_rplc[0] : NULL, ui_rplc[1][0] ? ui_rplc[1] : NULL);
        while (*p && n < 24) {
            len = (int)strcspn(p, "^\n");
            snprintf(lines[n], sizeof lines[n], "%.*s", len, p);
            p += len;
            if (*p)
                p++;
            if (lines[n][0] || *p)
                n++;
        }
        while (n > 0 && !lines[n - 1][0])
            n--;
        gfx_cursor(0);
        memcpy(under, screen, sizeof under);
        gfx_fill(56, 16, 208, n * 6 + 10, 2);
        gfx_rect(56, 16, 208, n * 6 + 10, 10);
        draw_text_centered(1, 160, 19, 0, "--- Civilization Note ---");
        for (i = 0; i < n; i++)
            draw_text(1, 64, 25 + i * font_height(1), 15, lines[i]);
        gfx_wait_key();
        gfx_cursor(1);
        memcpy(screen, under, sizeof under);
        gfx_present();
        ui_rplc[0][0] = ui_rplc[1][0] = 0;
        return;
    }
    show(key, NULL, NULL, 0);
    ui_rplc[0][0] = ui_rplc[1][0] = 0;
}

static const char *const paper_names[4] = { "Weekly", "Today", "Times", "Tribune" };

void advisor_portraits(void);
static uint8_t paper_under[SCR_H][SCR_W];   /* (f_0ad5_038f's copy for the cabinet) */

void ui_newspaper(const News *n, const char *text)
{
    char buf[4096], name[128], key[8];
    const char *cn;
    int k;

    memcpy(paper_under, screen, sizeof paper_under);
    gfx_fill(0, 0, 320, 100, 15);
    gfx_hline(1, 318, 1, 0);
    gfx_fill(8, 3, 304, 20, 15);
    gfx_hline(0, 319, 35, 0);
    gfx_vline(1, 1, 35, 0);
    gfx_vline(318, 1, 35, 0);
    gfx_hline(0, 319, 97, 0);
    if (n->crowd)
        for (k = 0; k < 16; k++)
            sprite_draw(spr(&sp257, 176, 128, 20, 9), k * 20, n->crowd_y[k]);
    text_get(text, buf, sizeof buf, NULL, NULL);
    text_wrapped(buf, 3, 40, 16, 40, 0);                 /* (the original's font 4) */

    cn = n->city >= 0 && n->city < MAX_CITIES && S->cities[n->city].status != NONE8
         ? S->city_names[S->cities[n->city].name] : S->civ_adjectives[S->human_civ];
    snprintf(name, sizeof name, "%s%s %s", n->title >= 2 ? "The " : "", cn, paper_names[n->title & 3]);
    if (text_width(4, name) > 300)                       /* (font 5) */
        snprintf(name, sizeof name, "%s News", cn);
    if (text_width(4, name) < 200) {
        draw_text(4, 8, 11, 0, ",-.");
        draw_text(4, 268, 11, 0, ",-.");
    }
    draw_text_centered(4, 160, 11, 0, name);
    draw_text(2, 6, 3, 0, n->extra ? "EXTRA!" : "FLASH");   /* (font 3) */
    draw_text(2, 272, 3, 0, n->extra ? "EXTRA!" : "FLASH");
    draw_text(0, 272, 28, 0, "10 cents");
    snprintf(name, sizeof name, "January 1, %d %s", S->year < 0 ? -S->year : S->year, S->year < 0 ? "BC" : "AD");
    draw_text(0, 8, 28, 0, name);
    snprintf(key, sizeof key, "*NEWS%c", 'A' + n->headline % 14);
    text_get(key, buf, sizeof buf, NULL, NULL);
    strcpy(buf + strcspn(buf, "\n"), " ");                /* (f_1f65_01b3 ends each line in ' ') */
    draw_text_centered(1, 160, 3, 0, buf);               /* (font 2) */
    if (!n->crowd) {
        gfx_present();
        return;
    }
    k = dialog_font;
    dialog_font = 0;
    dialog("Press any key to continue.\n", 80, 128, 0, 0);
    dialog_font = k;
    memcpy(screen, paper_under, sizeof paper_under);
    gfx_present();
}

static void message_paper(const char *text)
{
    char buf[4096];

    text_get(text, buf, sizeof buf, NULL, NULL);
    dialog(buf, -1, 40, 0, 0);
}

/* o_19_166c: invaders march past the conquered city (over its city view). */
void ui_city_conquered(int city, int civ, const char *text)
{
    static uint8_t under[SCR_H][SCR_W], back[SCR_H][SCR_W];
    Picture p, hill;
    Sprite frame[10];
    int k, t, i, x;

    (void)city;
    (void)text;
    if (civ_has_advance(civ, 34)) {
        if (load_picture(civ_has_advance(civ, 58) ? "INVADERS.PIC" : "INVADER2.PIC", &p) != 0)
            return;
        for (k = 0; k < 10; k++)
            frame[k] = spr(&p, (k % 4) * 79, (k / 4) * 61 + 2, 78, 60);
    } else {
        if (load_picture("INVADER3.PIC", &p) != 0)
            return;
        for (k = 0; k < 10; k++)
            frame[k] = spr(&p, (k % 4) * 79 + 1, (k / 4) * 66 + 1, 78, 65);
    }
    memcpy(under, screen, sizeof under);
    if (load_picture("HILL.PIC", &hill) == 0) {
        if (hill.has_palette)
            gfx_set_palette((const uint8_t (*)[3])hill.palette);
        gfx_blit(&hill, 0, 0, 320, 200, 0, 0, -1);
        pic_free(&hill);
    }
    memcpy(back, screen, sizeof back);
    for (t = 0, k = 0; t < 660; t += 3, k++) {
        memcpy(screen, back, sizeof back);
        for (i = 7; i >= 0; i--) {
            x = t - i * 48;
            if (x < 320 && x > -48)
                sprite_draw(frame[k % 10], x, 132);
        }
        gfx_present();
        gfx_delay(30);
        if (gfx_poll_key())
            break;
    }
    pic_free(&p);
    gfx_set_palette(game_palette);
    memcpy(screen, under, sizeof under);
    gfx_present();
}

void ui_spaceship_news(int civ, const char *text)
{
    (void)civ;
    message_paper(text);
}

/* research_complete (f_0ad5_1569): with Animations on, the human's discovery is shown over
 * DISCOVR1.PIC (DISCOVR2 once Electricity is known), the text without a box at (96, 1 / 25),
 * the advance's icon centred on (174, 96 / 87) in ICONPG1.PAL's palette with DISCOVR's colours
 * 0..84.  (Without Animations, and for Future Tech, science.c prints a newspaper.) */
void ui_discovery(int civ, int advance, const char *text)
{
    static uint8_t under[SCR_H][SCR_W];
    uint8_t pal[256][3];
    Picture p, dpal, icons, ipal;
    char file[24];
    int modern = civ_has_advance(S->human_civ, 54), k = advance_icon(advance), font = dialog_font;

    (void)civ;
    snprintf(file, sizeof file, "DISCOVR%d.PIC", modern + 1);
    if (k < 0 || load_picture(file, &p) != 0)
        return;
    memcpy(under, screen, sizeof under);
    strcpy(strchr(file, '.'), ".PAL");
    memcpy(pal, p.palette, sizeof pal);
    if (load_picture(file, &dpal) == 0 && dpal.has_palette)
        memcpy(pal, dpal.palette, sizeof pal);
    gfx_set_palette((const uint8_t (*)[3])pal);
    gfx_blit(&p, 0, 0, 320, 200, 0, 0, -1);
    dialog_font = modern ? 0 : 5;                     /* (the original's font 6) */
    dialog(text, 96, modern ? 25 : 1, 0, DLG_DRAW);
    dialog_font = 0;
    snprintf(file, sizeof file, "ICONPG%d.PIC", 1 + k / 9);
    if (load_picture(file, &icons) == 0) {
        icon_centre(&icons, (k % 3) * 111 + 1, (k % 9 / 3) * 69 + 1, 110, 68, 174, modern ? 87 : 96);
        if (load_picture("ICONPG1.PAL", &ipal) == 0 && ipal.has_palette) {
            memcpy(ipal.palette, pal, 85 * 3);
            gfx_set_palette((const uint8_t (*)[3])ipal.palette);
        }
        pic_free(&ipal);
        pic_free(&icons);
    }
    gfx_present();
    gfx_wait_key();
    dialog_font = font;
    pic_free(&dpal);
    pic_free(&p);
    gfx_set_palette(game_palette);
    memcpy(screen, under, sizeof under);
    gfx_present();
}

/* f_0ad5_1d13 -> o_08_062a: the human's new advance in the Civilopedia. */
void ui_advance_screen(int advance, int style)
{
    article_show(0, advance, NULL, style);
}

/* o_05_1adf: the advisors of the new government (GOVTnx.PIC) into SP299 (160, 140). */
void ui_government_picture(void)
{
    advisor_portraits();
}

/* o_05_1b8b: under the newspaper that newspaper(-2) left drawn, the new cabinet: the four
 * advisors from SP299 (160 + 40k, 140) at (80k + 20, 118), their titles (without the ':')
 * below in font 2; a key or click, then the screen comes back (f_0ad5_038f / 03ba). */
void ui_new_government(int civ)
{
    static const char *const titles[4] = { "Defense Minister", "Domestic Advisor", "Foreign Minister",
                                           "Science Advisor" };
    int k;

    if (civ != S->human_civ || S->government[civ] == GOV_ANARCHY)
        return;
    gfx_fill(0, 100, 320, 100, 15);
    draw_text_centered(5, 160, 102, 0, "New Cabinet:");        /* (font 6) */
    for (k = 0; k < 4; k++) {
        gfx_blit(&sp299, k * 40 + 160, 140, 40, 60, k * 80 + 20, 118, -1);
        draw_text_centered(1, k * 80 + 40, (k & 1) ? 186 : 180, 0, titles[k]);
    }
    gfx_wait_key();
    memcpy(screen, paper_under, sizeof paper_under);
    gfx_present();
}

void ui_research_advice(int advance)
{
    (void)advance;
}

void ui_production_advice(int city, int military, int domestic)
{
    char msg[256];
    const char *m = military == -999 ? "" : military >= 0 ? S->unit_type_names[military] : building_types[-military].name;
    const char *d = domestic == -999 ? "" : domestic >= 0 ? S->unit_type_names[domestic] : building_types[-domestic].name;

    if (!(S->unk_19c0 & 1))
        return;
    snprintf(msg, sizeof msg, "%s advisors:\nMilitary: %s\nDomestic: %s\n",
             S->city_names[S->cities[city].name], m[0] ? m : "-", d[0] ? d : "-");
    ui_message(msg, 0);
}

void ui_text_section(const char *key, const char *a, const char *b, char *out, int size)
{
    text_get(key, out, (size_t)size, a, b);
}

int ui_help_requested(void)
{
    return 0;
}

int ui_choose(const char *menu, int small_font, int x, int y, int advisor, int help)
{
    char buf[4096];

    (void)small_font;
    text_get(menu, buf, sizeof buf, NULL, NULL);
    gfx_present();
    dialog_advisor = advisor;
    dialog_help = help;
    return dialog(buf, x, y, 0, 0);
}

int ui_menu(const char *menu, int disabled)
{
    char buf[4096];

    text_get(menu, buf, sizeof buf, NULL, NULL);
    gfx_present();
    return dialog(buf, -1, -1, disabled, 0);
}

int ui_ask(const char *text, int style)
{
    (void)style;
    return ui_menu(text, 0);
}

int ui_ask_emissary(const char *text)
{
    return ui_menu(text, 0);
}

int ui_name_city(int city)
{
    char buf[CITY_NAME_LEN];
    char *name = S->city_names[S->cities[city].name];

    snprintf(buf, sizeof buf, "%.12s", name);
    if (city_name_prompt(buf, 12))
        snprintf(name, CITY_NAME_LEN, "%s", buf);
    return 1;
}

/* o_22_0e87: the founding (the settlers' walk, which a key skips; then the city view). */
void ui_city_founded(int city)
{
    static uint8_t under[SCR_H][SCR_W], back[SCR_H][SCR_W];
    Picture p, hill;
    Sprite frame[4];
    char title[64];
    int x, k, skipped = 0;

    if (!(S->unk_19c0 & 8))
        return;
    if (load_picture("SETTLERS.PIC", &p) == 0 && load_picture("HILL.PIC", &hill) == 0) {
        memcpy(under, screen, sizeof under);
        for (k = 0; k < 4; k++)
            frame[k] = spr(&p, 1, k * 16 + 1, 47, 15);
        if (hill.has_palette)
            gfx_set_palette((const uint8_t (*)[3])hill.palette);
        gfx_blit(&hill, 0, 0, 320, 200, 0, 0, -1);
        snprintf(title, sizeof title, "%s founded: %d %s", S->city_names[S->cities[city].name],
                 S->year < 0 ? -S->year : S->year, S->year < 0 ? "BC" : "AD");
        draw_text_centered(5, 160, 4, 0, title);      /* (the original's font 6) */
        memcpy(back, screen, sizeof back);
        for (x = 80; x < 120 && !skipped; x++) {
            memcpy(screen, back, sizeof back);
            for (k = 0; k < 4; k++)
                sprite_draw(frame[(k + x) & 3], k * 13 + x, ((k * 10) & 15) + 120);
            gfx_present();
            gfx_delay(10 * 1000 / 60);          /* (10 ticks of the 60 Hz timer a step) */
            skipped = gfx_poll_key() != 0;
        }
        pic_free(&hill);
        gfx_set_palette(game_palette);
        memcpy(screen, under, sizeof under);
    }
    pic_free(&p);
    if (!skipped)                       /* (a key during the walk skips the city view too) */
        city_view(city, -3);
}

void ui_custom_tribe(int civ)
{
    char buf[16] = "";

    /* (o_23_0173: the adjective is the name without a final 's') */
    snprintf(buf, sizeof buf, "%s", S->civ_names[civ]);
    name_prompt("Name of your Tribe...", buf, 11);
    snprintf(S->civ_names[civ], sizeof S->civ_names[civ], "%s", buf);
    if (buf[0] && buf[strlen(buf) - 1] == 's')
        buf[strlen(buf) - 1] = 0;
    snprintf(S->civ_adjectives[civ], sizeof S->civ_adjectives[civ], "%s", buf);
    if (!S->civ_names[civ][0])
        snprintf(S->civ_names[civ], sizeof S->civ_names[civ], "%s", civ_types[civ].plural);
    if (!S->civ_adjectives[civ][0])
        snprintf(S->civ_adjectives[civ], sizeof S->civ_adjectives[civ], "%s", civ_types[civ].adjective);
}

void ui_leader_name(int civ)
{
    char buf[14];

    snprintf(buf, sizeof buf, "%.13s", S->leader_names[civ]);
    draw_text_centered(6, 46, 92, 15, S->civ_names[civ]);     /* (o_05_0000) */
    gfx_fill(150, 0, 170, 200, 0);
    name_prompt("Your Name...", buf, 13);
    snprintf(S->leader_names[civ], sizeof S->leader_names[civ], "%s", buf);
}

void frame_border(int style);
static void text_block(const char *text, int x, int y, int colour);

/* o_05_0000's end: *INIT and the advances known, on a page with a border and the portrait */
void ui_game_intro(int civ)
{
    char msg[4096], *p;
    int k, n = 0;
    Picture diffs;

    text_get("*INIT", msg, sizeof msg, S->leader_names[civ], NULL);
    for (k = 0; k < 72; k++)
        if (civ_has_advance(civ, k)) {
            snprintf(msg + strlen(msg), sizeof msg - strlen(msg), "%s, ", advance_types[k].name);
            if (++n == 2)
                strcat(msg, "\n");
        }
    strcat(msg, "and Roads.\n");
    for (p = msg; *p; p++)
        if (*p == '^')
            *p = '\n';
    ui_sound(civ_types[S->civ_type[civ]].music);
    gfx_cursor(0);
    gfx_fill(0, 0, 320, 200, 15);
    frame_border(frame_style);
    if (load_picture("DIFFS.PIC", &diffs) == 0) {
        gfx_blit(&diffs, (S->difficulty & 1) ? 80 : 21, S->difficulty * 35 + 6, 53, 47, 134, 20, -1);
        pic_free(&diffs);
    }
    dialog_font = 0;
    text_block(msg, 88, 81, 0);
    gfx_cursor(1);
    gfx_wait_key();
}

/* o_02_1953: a line to (x1, y1) and a label box at (x2, y2) */
static void tutorial_label(const char *text, int x1, int y1, int x2, int y2)
{
    char buf[64];

    {
        /* (f_0181_000c: a line in white) */
        int dx = abs(x2 - x1), dy = abs(y2 + 6 - y1), sx = x1 < x2 ? 1 : -1, sy = y1 < y2 + 6 ? 1 : -1, e = dx - dy;
        int x = x1, y = y1, e2;
        for (;;) {
            gfx_pixel(x, y, 15);
            if (x == x2 && y == y2 + 6)
                break;
            e2 = 2 * e;
            if (e2 > -dy) {
                e -= dy;
                x += sx;
            }
            if (e2 < dx) {
                e += dx;
                y += sy;
            }
        }
    }
    snprintf(buf, sizeof buf, "%s\n", text);
    dialog(buf, x2, y2, 0, DLG_DRAW);
}

/* o_02_17d2: Chieftain's first screen, everything around the settlers named */
void ui_tutorial_start(void)
{
    int cx = view_x + 7, cy = view_y + 6, k, x, y, t;
    char name[32];

    gfx_cursor(0);
    for (k = 1; k <= 8; k++) {
        x = wrap_x(around_dx[k] + cx);
        y = around_dy[k] + cy;
        t = map_terrain(x, y);
        snprintf(name, sizeof name, "%s", terrain_types[t].name);
        if (has_special_resource(x, y))
            snprintf(name, sizeof name, "%s", terrain_types[t + TERRAIN_SPECIAL].name);
        if (has_hut(t, x, y))
            snprintf(name, sizeof name, "Village");
        tutorial_label(name, around_dx[k] * 16 + 200, around_dy[k] * 16 + 112, around_dx[k] * 64 + 170,
                       around_dy[k] * 48 + 100);
    }
    tutorial_label("Menu Bar", 160, 6, 200, 16);
    tutorial_label("Map Window", 48, 32, 88, 24);
    tutorial_label("Active Unit", 40, 128, 88, 170);
    gfx_wait_key();
    gfx_cursor(1);
}

/* o_07_176e: while a world is made, the story of the Earth (STORY.TXT) over the BIRTH
 * pictures; a key goes to the next page, Escape skips the rest. */
static int story_page = -1;

/* o_07_176e: while the world is made, STORY.TXT a line at a time (font 7, colour 3 then 11)
 * over BIRTH0-8.PIC, a picture at each '_' line; a key ends it. */
void ui_world_progress(void)
{
    static Picture pic;
    char path[512], line[160];
    FILE *f;
    int n = 0, len;

    if (story_page != -1)
        return;
    story_page = 0;
    snprintf(path, sizeof path, "%s/STORY.TXT", data_dir);
    if (!(f = fopen(path, "rb")))
        return;
    gfx_cursor(0);
    gfx_fill(0, 0, 320, 200, 0);
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        len = (int)strlen(line);
        if (len <= 3) {                          /* the next picture */
            char name[16];
            Picture pal;
            pic_free(&pic);
            snprintf(name, sizeof name, "BIRTH%d.PIC", n < 9 ? n : 8);
            if (load_picture(name, &pic) == 0) {
                snprintf(name, sizeof name, "BIRTH%d.PAL", n < 9 ? n : 8);
                if (load_picture(name, &pal) == 0 && pal.has_palette)
                    gfx_set_palette((const uint8_t (*)[3])pal.palette);
                else if (pic.has_palette)
                    gfx_set_palette((const uint8_t (*)[3])pic.palette);
                pic_free(&pal);
                gfx_blit(&pic, 0, 0, 320, 200, 0, 0, -1);
            }
            n++;
            continue;
        }
        if (pic.pixels)
            gfx_blit(&pic, 0, 160, 320, 8, 0, 160, -1);
        else
            gfx_fill(0, 160, 320, 8, 0);
        draw_text_centered(6, 160, 160, 3, line);
        gfx_present();
        gfx_delay(85);
        draw_text_centered(6, 160, 160, 11, line);
        gfx_present();
        gfx_delay(len > 19 ? 2250 : 1500);
        if (gfx_poll_wait())                 /* (the story waits on a key as it goes) */
            break;
    }
    fclose(f);
    pic_free(&pic);
    gfx_cursor(1);
    gfx_set_palette(game_palette);
}

void ui_world_progress_end(void)
{
    story_page = -1;
}

/* ------------------------------------------------------------------------------------- */
/* Small screens. */

/* Text with '\t' going to the next column (every 52 pixels). */
static void draw_tabbed(int x, int y, int colour, const char *s)
{
    char part[80];
    int k, x0 = x;

    while (*s) {
        for (k = 0; *s && *s != '\t' && k < 79; s++)
            part[k++] = *s;
        part[k] = 0;
        x = draw_text(dialog_font, x, y, colour, part);
        if (*s == '\t') {
            s++;
            x = x0 + ((x - x0) / 52 + 1) * 52;
        }
    }
}

static void list_screen(const char *title, char lines[][80], int n)
{
    int top = 0, per = 20, i, k;

    for (;;) {
        gfx_fill(0, 0, 320, 200, 1);
        draw_text(map_font, 8, 2, 15, title);
        gfx_hline(0, 319, 10, 15);
        for (i = 0; i < per && top + i < n; i++)
            draw_tabbed(8, 14 + i * 9, 15, lines[top + i]);
        if (n > per)
            draw_text(map_font, 220, 192, 7, "MORE: UP/DOWN");
        k = gfx_wait_key();
        if ((k == '2' || k == KEY_DOWN || k == '3') && top + per < n)
            top += per;
        else if ((k == '8' || k == KEY_UP || k == '9') && top > 0)
            top -= per;
        else if (k != '2' && k != '8' && k != KEY_UP && k != KEY_DOWN)
            break;
    }
}

void advisor_city_status(void);
void advisor_attitude(void);
void demographics(void);

static void city_status_advisor(void)
{
    advisor_city_status();
}

void advisor_military(void);

static void military_advisor(void)
{
    advisor_military();
}

void advisor_intelligence_civ(int civ);

void ui_intelligence_report_civ(int civ)
{
    advisor_intelligence_civ(civ);
}

void ui_intelligence_report_for(int civ)
{
    ui_intelligence_report_civ(civ);
}

/* o_14_0d3d: the civilizations known; those with an embassy can be looked into (and then
 * the map is redrawn, as in the original). */
void advisor_intelligence(void);

void ui_intelligence_report(void)
{
    advisor_intelligence();
}

void advisor_trade(void);

static void trade_advisor(void)
{
    advisor_trade();
}

void advisor_science(void);

static void science_advisor(void)
{
    advisor_science();
}

void wonders_of_the_world(void);

static void wonders_screen(void)
{
    wonders_of_the_world();
}

void top_five_cities(void);

static void top_cities(void)
{
    top_five_cities();
}

void score_screen_original(void);

/* o_20_0c98 with its screen (which ends by redrawing the map). */
static void score_screen(void)
{
    score_screen_original();
}

void world_map_original(void);

static void world_map(void)
{
    world_map_original();
}

void civilopedia_original(int cat);

/* f_1c9d_07bd */
static void civilopedia(void)
{
    int f = dialog(" Complete\n Civilization Advances\n City Improvements\n Military Units\n Terrain Types\n"
                   " Miscellaneous\n", 182, 8, 0, DLG_ESCAPE);

    if (f >= 0)
        civilopedia_original(f - 1);
}

void ui_encyclopedia(int item)
{
    article_show(1, item, NULL, rnd(2));
}

void ui_encyclopedia_advance(int advance)
{
    article_show(0, advance, NULL, rnd(2));
}

/* ------------------------------------------------------------------------------------- */
/* The GAME menu. */

/* L358a / L3675 (civ_move_units): the tax or luxuries rate, the current one highlighted */
static void tax_rate_menu(int luxuries)
{
    char menu[1024];
    int h = S->human_civ, i, k, tax = S->tax_rate[h], sci = S->science_rate[h];

    if (!luxuries) {
        strcpy(menu, "Select new Tax rate:\n ");
        for (i = 0; i <= tax + sci; i++)
            snprintf(menu + strlen(menu), sizeof menu - strlen(menu), "%d%% Tax, (%d%% Science)\n ", i * 10,
                     (tax + sci - i) * 10);
        dialog_default = tax;
    } else {
        strcpy(menu, "Select new Luxuries rate:\n ");
        for (i = 0; i <= 10 - tax; i++)
            snprintf(menu + strlen(menu), sizeof menu - strlen(menu), "%d%% Luxuries, (%d%% Science)\n ",
                     i * 10, (10 - i - tax) * 10);
        dialog_default = 10 - tax - sci;
    }
    k = dialog(menu, 100, 80, 0, DLG_ESCAPE);
    if (k < 0)
        return;
    if (luxuries)
        S->science_rate[h] = (int16_t)(10 - tax - k);
    else {
        S->science_rate[h] = (int16_t)(sci + tax - k);
        S->tax_rate[h] = (int16_t)k;
    }
    status_report();
}

/* o_23_025b / o_23_0312: "Where in the heck is ..." -- a city whose name starts with what is
 * typed (any case); the view moves there if the human knows the square. */
static void find_city(void)
{
    static uint8_t under[SCR_H][SCR_W];
    char buf[24] = "";
    int n, c = -1, i, len;

    gfx_cursor(0);
    memcpy(under, screen, sizeof under);
    gfx_fill(64, 78, 224, 24, 15);
    gfx_rect(64, 78, 224, 24, 0);
    draw_text(dialog_font, 66, 80, 0, "Where in the heck is ... (city name)");
    text_field(66, 89, 16, buf);
    gfx_cursor(1);
    len = (int)strlen(buf);
    for (n = 0; n < NUM_CITY_NAMES && c < 0; n++) {
        if (strncasecmp(S->city_names[n], buf, (size_t)len) != 0)
            continue;
        for (i = 0; i < MAX_CITIES; i++)
            if (S->cities[i].status != NONE8 && S->cities[i].name == n) {
                c = i;
                break;
            }
    }
    if (c >= 0 && (S->visibility[S->cities[c].x][S->cities[c].y] & (1 << S->human_civ))) {
        view_center(S->human_civ, S->cities[c].x - 7, S->cities[c].y - 6);
        return;
    }
    gfx_fill(64, 78, 224, 10, 15);
    draw_text(dialog_font, 82, 80, 0, "Unknown city.");
    gfx_wait_key();
    memcpy(screen, under, sizeof under);
    gfx_present();
}

/* f_1c9d_00bb, option 3: the options, checked when on; each choice toggles one */
static void options_menu(void)
{
    int k;

    for (;;) {
        dialog_checks = S->unk_19c0;
        k = dialog("Options:\n Instant Advice\n AutoSave\n End of Turn\n Animations\n Sound\n Enemy Moves\n"
                   " Civilopedia Text\n Palace\n", 24, 16, 0, DLG_ESCAPE);
        if (k < 0)
            return;
        S->unk_19c0 ^= (int16_t)(1 << k);
        dialog_default = k;
    }
}

int save_slot(int slot)
{
    char sve[512], map[512];

    make_save_dir();
    snprintf(sve, sizeof sve, "%s/CIVIL%d.SVE", save_dir, slot);
    snprintf(map, sizeof map, "%s/CIVIL%d.MAP", save_dir, slot);
    return save_game(sve, map, &game);
}

/* f_0860_25e1: a border of SP299 pieces (one of two styles) round the screen */
void frame_border(int style)
{
    int k = style * 32 + 192, i;

    gfx_blit(&sp299, k, 120, 8, 8, 0, 0, -1);
    gfx_blit(&sp299, k + 8, 120, 8, 8, 312, 0, -1);
    gfx_blit(&sp299, k + 24, 120, 8, 8, 312, 192, -1);
    gfx_blit(&sp299, k + 16, 120, 8, 8, 0, 192, -1);
    for (i = 1; i < 24; i++) {
        gfx_blit(&sp299, k + 24, 128, 8, 8, 0, i * 8, -1);
        gfx_blit(&sp299, k + 8, 128, 8, 8, 312, i * 8, -1);
    }
    for (i = 1; i < 39; i++) {
        gfx_blit(&sp299, k, 128, 8, 8, i * 8, 0, -1);
        gfx_blit(&sp299, k + 16, 128, 8, 8, i * 8, 192, -1);
    }
}

/* f_1f65_008e: lines of text from (x, y) */
static void text_block(const char *text, int x, int y, int colour)
{
    char line[128];
    const char *p = text;
    int n;

    while (*p) {
        for (n = 0; *p && *p != '\n' && n < 127; p++)
            line[n++] = *p;
        line[n] = 0;
        if (*p)
            p++;
        draw_text(dialog_font, x, y, colour, line);
        y += font_height(dialog_font);
    }
}

/* o_11_05f6: which drive (only C: here); -1 if Escape */
static int save_drive(int loading)
{
    static int drive = 2;
    char text[256];
    int k;

    gfx_fill(0, 0, 320, 200, 15);
    if (!loading)
        frame_border(rnd(2));
    for (;;) {
        snprintf(text, sizeof text, "%s%c:\n\n    Press drive letter and\nReturn when disk is inserted.\n"
                 "    Press Escape to cancel.\n", loading ? "  Which drive contains your\n    saved game files?\n\n           "
                 : "  Which drive contains your\n     Save Game disk?\n\n           ", 'A' + drive);
        text_block(text, 80, 72, 0);
        k = gfx_wait_key();
        if (k >= 'a' && k <= 'f')
            drive = k - 'a';
        else if (k >= 'A' && k <= 'F')
            drive = k - 'A';
        if (k == 27 || k == KEY_QUIT)
            drive = -1;
        gfx_fill(80, 88, 160, 24, 15);
        if (k == 13 || k == 27 || k == KEY_QUIT)
            break;
    }
    gfx_fill(8, 8, 304, 184, 15);
    if (drive == -1) {
        drive = 2;
        return -1;
    }
    if (drive != 2) {
        snprintf(text, sizeof text, "No Disk in Drive %c.\n", 'A' + drive);
        dialog(text, 100, 80, 0, 0);
        return -1;
    }
    return drive;
}

static int last_slot = -1;           /* g_f088 */

/* o_11_0368: save the game in slot 0-3 */
static void save_menu(void)
{
    static uint8_t under[SCR_H][SCR_W];
    char menu[1024], tmp[96];
    int i, k, ok;

    memcpy(under, screen, sizeof under);
    gfx_cursor(0);
    if (save_drive(0) >= 0) {
        strcpy(menu, "\x8cSelect Save File...\n");
        for (i = 0; i < 4; i++) {
            describe_slot(i, tmp, sizeof tmp);
            strncat(menu, tmp, sizeof menu - strlen(menu) - 2);
            strcat(menu, "\n");
        }
        dialog_default = clamp(last_slot, 0, 3);
        gfx_cursor(1);
        k = dialog(menu, 48, 33, 0, DLG_ESCAPE);
        gfx_cursor(0);
        if (k >= 0) {
            last_slot = k;
            ok = save_slot(k) == 0;
            if (!ok)
                gfx_fill(64, 127, 192, 34, 12);
            dialog(ok ? " Game has been saved.\n Press key to continue.\n"
                      : " Game NOT saved.\n Press key to continue.\n", 64, 127, 0, DLG_NOSEL);
        }
    }
    gfx_cursor(1);
    memcpy(screen, under, sizeof under);
    gfx_present();
}

/* o_11_0000(-1): the slot to load, or -1 */
int choose_load_slot(void)
{
    char menu[1536], tmp[96];
    int i, k, valid = 0;

    gfx_cursor(0);
    if (save_drive(1) < 0) {
        gfx_cursor(1);
        return -1;
    }
    strcpy(menu, "\x8cSelect Load File...\n");
    for (i = 0; i < 10; i++) {
        describe_slot(i, tmp, sizeof tmp);
        if (slot_exists(i))
            valid |= 1 << i;
        strncat(menu, tmp, sizeof menu - strlen(menu) - 2);
        strcat(menu, "\n");
    }
    gfx_cursor(1);
    k = dialog(menu, 48, 65, 0, DLG_ESCAPE);
    if (k < 0 || !(valid & (1 << k)))
        return -1;
    last_slot = k;
    return k;
}

void ui_autosave(int slot)
{
    save_slot(slot);
}

static void revolution(void)
{
    char msg[160];

    if (ui_ask("*REV", 0) != 1)
        return;
    S->government[S->human_civ] = GOV_ANARCHY;
    snprintf(msg, sizeof msg, "The %s are\nrevolting! Citizens\ndemand new govt.\n", S->civ_names[S->human_civ]);
    newspaper(-1, msg);
    redraw_map();
}

/* Returns 1 if the game should stop (quit). */
static int game_menu(void)
{
    /* f_1c9d_00bb */
    char text[160] = " Tax Rate\n Luxuries Rate\n FindCity\n Options\n Save Game\n REVOLUTION!\n \n Retire\n QUIT to DOS\n";
    int k;

    if (S->unk_4c88 & 0x100)
        strcat(text, " View Replay\n");
    screen_save();
    k = dialog(text, 16, 8, 0, DLG_ESCAPE | DLG_KEEP);
    if (k >= 6)
        k = k == 6 ? -1 : k - 1;          /* (the blank line) */
    if (k == 3)
        options_menu();                   /* (over the menu, which goes after) */
    screen_restore();
    gfx_present();

    switch (k) {
    case 0: tax_rate_menu(0); break;
    case 1: tax_rate_menu(1); break;
    case 2: find_city(); break;
    case 4: save_menu(); break;
    case 5: revolution(); break;
    case 6:                               /* Retire */
    case 7:                               /* QUIT to DOS */
        /* (L3872: both ask the same; "Retire" ends the game with its score) */
        if (dialog("Are you sure you\nwant to Quit?\n Keep Playing\n Yes, Quit\n", 100, 80, 0, DLG_ESCAPE) == 1) {
            quit_flag = k == 6 ? 2 : 1;
            return 1;
        }
        break;
    case 8:
        ui_replay();
        break;
    }
    return 0;
}

static void advisors_menu(void)
{
    /* f_1c9d_0623 */
    int k = dialog(" City Status (F1)\n Military Advisor (F2)\n Intelligence Advisor (F3)\n Attitude Advisor (F4)\n"
                   " Trade Advisor (F5)\n Science Advisor (F6)\n", 112, 8, 0, DLG_ESCAPE);

    switch (k) {
    case 0: city_status_advisor(); break;
    case 1: military_advisor(); break;
    case 2: ui_intelligence_report(); break;
    case 3: advisor_attitude(); break;
    case 4: trade_advisor(); break;
    case 5: science_advisor(); break;
    }
}

static void world_menu(void)
{
    /* f_1c9d_06f2: SpaceShips only once one has been launched */
    int k = dialog(" Wonders of the World (F7)\n Top 5 Cities (F8)\n Civilization Score (F9)\n"
                   " World Map (F10)\n Demographics\n SpaceShips\n", 144, 8, (S->unk_4c88 & ~0x1FF) ? 0 : 32,
                   DLG_ESCAPE);

    switch (k) {
    case 0: wonders_screen(); break;
    case 1: top_cities(); break;
    case 2: score_screen(); break;
    case 3: world_map(); break;
    case 4: demographics(); break;
    case 5: spaceship_view(); break;
    }
}

/* f_1c9d_01e6: the ORDERS menu, only what the unit can do; returns the order's key. */
static int orders_menu(int civ, int u)
{
    const Unit *p = &S->units[civ][u];
    int imp = map_improvements(p->x, p->y), t = map_terrain(p->x, p->y), n = 0, disabled = 0, k, v;
    char text[512], keys[32];

    text[0] = 0;
#define ADD(str, key) (strcat(text, str), keys[n++] = (key))
    ADD(" No Orders \x8fspace\n", ' ');
    if (p->type == UNIT_SETTLERS) {
        ADD((imp & IMP_CITY) ? " Add to City \x8f" "b\n" : " Found New City \x8f" "b\n", 'b');
        if (!(imp & IMP_ROAD))
            ADD(" Build Road \x8fr\n", 'r');
        else if (!(imp & IMP_RAILROAD) && civ_has_advance(civ, ADV_RAILROAD))
            ADD(" Build RailRoad \x8fr\n", 'r');
        if (!(imp & IMP_IRRIGATION)) {
            v = terrain_improve[t].irrigate;
            if (v == -2) {
                strcat(text, " Build Irrigation");
                if (!water_near(p->x, p->y))
                    disabled |= 1 << n;
            } else if (v >= 0) {
                strcat(text, " Change to ");
                strcat(text, terrain_types[terrain_by_colour[v]].name);
            }
            if (v != -1)
                ADD(" \x8fi\n", 'i');
        }
        if (!(imp & IMP_MINE)) {
            v = terrain_improve[t].mine;
            if (v <= -2)
                strcat(text, " Build Mines");
            else if (v >= 0) {
                strcat(text, " Change to ");
                strcat(text, terrain_types[terrain_by_colour[v]].name);
            }
            if (v != -1)
                ADD(" \x8fm\n", 'm');
        }
        if (imp & IMP_POLLUTION)
            ADD(" Clean up Pollution \x8fp\n", 'p');
    }
    if (p->type == UNIT_SETTLERS) {
        strcat(text, " Build Fortress \x8f" "f\n");
        if (!civ_has_advance(civ, 45))            /* (Construction) */
            disabled |= 1 << n;
    } else if (S->unit_types[p->type].domain == DOMAIN_LAND)
        strcat(text, " Fortify \x8f" "f\n");
    if (S->unit_types[p->type].domain == DOMAIN_LAND)
        keys[n++] = 'f';
    ADD(" Wait \x8fw\n Sentry \x8fs\n GoTo\n", 'w');
    keys[n++] = 's';
    keys[n++] = 'g';
    if ((imp & (IMP_IRRIGATION | IMP_MINE | IMP_ROAD | IMP_RAILROAD)) && p->type < UNIT_DIPLOMAT && p->type != 14)
        ADD(" Pillage \x8fP\n", 'P');
    if (imp & IMP_CITY)
        ADD(" Home City \x8fh\n", 'h');
    if ((S->unit_types[p->type].role == 5 || p->type == UNIT_CARRIER) && p->next != NONE8)
        ADD(" Unload \x8fu\n", 'u');
    ADD(" \n Disband Unit \x8f" "D\n", 0);
    keys[n++] = 'D';
#undef ADD
    k = dialog(text, 72, 8, disabled, DLG_ESCAPE);
    if (k < 0 || k >= n)
        return 0;
    return keys[k];
}

/* Keys every screen with the map understands; returns 1 if handled. */
static int common_key(int k, int *stop)
{
    *stop = 0;
    switch (k) {
    case 27: *stop = game_menu(); break;
    case KEY_F1: city_status_advisor(); break;
    case KEY_F1 + 1: military_advisor(); break;
    case KEY_F1 + 2: ui_intelligence_report(); break;
    case KEY_F1 + 3: advisor_attitude(); break;
    case KEY_F1 + 4: trade_advisor(); break;
    case KEY_F1 + 5: science_advisor(); break;
    case KEY_F1 + 6: wonders_screen(); break;
    case KEY_F1 + 7: top_cities(); break;
    case KEY_F1 + 8: score_screen(); break;
    case KEY_F10: world_map(); break;
    case KEY_QUIT:
        quit_flag = 1;
        *stop = 1;
        return 1;
    default:
        return 0;
    }
    main_screen();
    return 1;
}

/* A click on the menu bar.  Returns an order for the unit, or 0. */
static int menu_bar_click(int civ, int u, int *stop)
{
    int x = mouse_x, r = 0;

    *stop = 0;
    switch (clamp(x / 60, 0, 4)) {           /* (f_1c9d_000e) */
    case 0:
        *stop = game_menu();
        break;
    case 1:
        if (u < MAX_UNITS)
            r = orders_menu(civ, u);
        break;
    case 2:
        advisors_menu();
        break;
    case 3:
        world_menu();
        break;
    default:
        civilopedia();
        break;
    }
    main_screen();
    return r;
}

/* The square under the mouse, if on the map view. */
static int clicked_square(int *x, int *y)
{
    if (mouse_x < 80 || mouse_y < 8)
        return 0;
    *x = wrap_x(view_x + (mouse_x - 80) / 16);
    *y = view_y + (mouse_y - 8) / 16;
    return map_in_bounds(*x, *y);
}

static int minimap_click(void)
{
    if (mouse_x >= 80 || mouse_y < 8 || mouse_y >= 58)
        return 0;
    view_center(S->human_civ, minimap_left + mouse_x - 7, minimap_top + mouse_y - 8 - 6);
    main_screen();
    return 1;
}

static int own_city_at(int x, int y)
{
    int c;

    if (!(map_improvements(x, y) & IMP_CITY))
        return -1;
    c = city_at(x, y);
    return c >= 0 && (int8_t)S->cities[c].owner == S->human_civ ? c : -1;
}

/* Pick a square for GoTo with the keys or the mouse; 0 if cancelled. */
static int pick_square(int *x, int *y)
{
    int cx = *x, cy = *y, k, d, px, py;

    for (;;) {
        main_screen();
        draw_text(map_font, 84, 10, 15, "GOTO: SELECT DESTINATION");
        px = wrap_x(cx - view_x) * 16 + 80;
        py = (cy - view_y) * 16 + 8;
        if (px >= 80 && px < 320 && py >= 8 && py <= 192)
            gfx_rect(px, py, 15, 15, 15);
        k = gfx_wait_key();
        if (k == 27 || k == KEY_QUIT)
            return 0;
        if (k == 13 || k == ' ') {
            *x = cx;
            *y = cy;
            return 1;
        }
        if (k == KEY_MOUSE) {
            if (clicked_square(x, y))
                return 1;
            continue;
        }
        d = dirs_of_key(k);
        if (d) {
            cx = wrap_x(cx + around_dx[d]);
            cy = clamp(cy + around_dy[d], 0, MAP_H - 1);
        }
    }
}

void ui_city_screen(int civ, int city);
void ui_replay(void);

/* f_0860_1f2a: the units on a square in a list; the one picked is woken (its orders cleared,
 * a sleeping unit's moves restored) and returned.  Escape or a click elsewhere returns the
 * stack's defender (f_0860_1059). */
static int stack_pick(int civ, int u)
{
    static uint8_t under[SCR_H][SCR_W];
    int list[12], n, top, i, j, k, chosen = stack_defender_at(civ, u), py;
    char buf[64];
    Unit *q;

    memcpy(under, screen, sizeof under);
    for (n = 0, j = u; n < 12; ) {
        list[n++] = j;
        j = (int8_t)S->units[civ][j].next;
        if (j == -1 || j == u)
            break;
    }
    top = 96 - n * 8;
    for (;;) {
        dialog_box_at(100, top, 120, n * 16 + 5, 3);
        for (i = 0, py = top + 5; i < n; i++, py += 16) {
            q = &S->units[civ][list[i]];
            draw_unit_at(civ, list[i], 106, py, q);
            snprintf(buf, sizeof buf, "%s%s", S->unit_type_names[q->type], (q->status & 0x20) ? " (V)" : "");
            draw_text(map_font, 128, py, 15, buf);
            snprintf(buf, sizeof buf, "%s", q->home_city == NONE8 ? "NONE" : S->city_names[S->cities[q->home_city].name]);
            draw_text(map_font, 128, py + 8, 14, buf);
        }
        /* (with a mouse, as here, the list takes a click; a key closes it: L20b6) */
        k = gfx_wait_key();
        i = -1;
        if (k == KEY_MOUSE && mouse_button == 1 && mouse_x > 100 && mouse_x < 220 && mouse_y - top - 5 >= 0)
            i = (mouse_y - top - 5) / 16;
        if (i < 0 || i >= n)
            break;
        chosen = list[i];
        q = &S->units[civ][chosen];
        if (q->status & 0xC2)
            q->moves = (uint8_t)(S->unit_types[q->type].moves * 3);
        q->status &= 0x30;
        q->goto_x = NONE8;
        if (n <= 1)
            break;
    }
    memcpy(screen, under, sizeof under);
    gfx_present();
    return chosen;
}

/* The player's order for unit u (f_0402_000a's input part). */
int ui_human_command(int civ, int u)
{
    Unit *p = &S->units[civ][u];
    int k, on = 0, stop, x, y, d, c;
    unsigned next = 0, now;

    panel_civ = civ;
    panel_unit = u;
    main_screen();
    for (;;) {
        now = gfx_ticks();
        if (now >= next) {
            on = !on;
            next = now + 167;                 /* (f_0860_0aa8: 10 ticks of 60 Hz) */
            if (on)
                draw_unit(civ, u);
            else
                draw_square(p->x, p->y);
            gfx_present();
        }
        k = gfx_poll_wait();
        if (!k) {
            gfx_delay(10);
            continue;
        }
        draw_square_units(p->x, p->y);
        draw_unit(civ, u);
        d = dirs_of_key(k);
        if (d)
            return d;
        if (common_key(k, &stop)) {
            if (stop)
                return ' ';
            next = 0;                         /* (the unit shows again at once) */
            on = 0;
            continue;
        }
        switch (k) {
        case ' ': case 'b': case 'f': case 's': case 'w': case 'r': case 'i': case 'm': case 'p':
        case 'D': case 'P': case 'u': case 'h':
            return k;
        case 'g':
            x = p->x;
            y = p->y;
            if (pick_square(&x, &y) && !(x == p->x && y == p->y)) {
                p->goto_x = (uint8_t)x;
                p->goto_y = (uint8_t)y;
                return 0;
            }
            main_screen();
            continue;
        case 'c':
            view_center(civ, p->x - 7, p->y - 6);
            main_screen();
            continue;
        case 13:
            c = own_city_at(p->x, p->y);
            if (c >= 0) {
                city_screen(civ, c);
                main_screen();
            }
            continue;
        case KEY_MOUSE:
            if (mouse_y < 8) {
                k = menu_bar_click(civ, u, &stop);
                if (stop)
                    return ' ';
                if (k == 'g') {
                    x = p->x;
                    y = p->y;
                    if (pick_square(&x, &y) && !(x == p->x && y == p->y)) {
                        p->goto_x = (uint8_t)x;
                        p->goto_y = (uint8_t)y;
                        return 0;
                    }
                    main_screen();
                    continue;
                }
                if (k)
                    return k;
                continue;
            }
            if (minimap_click())
                continue;
            if (!clicked_square(&x, &y))
                continue;
            if (mouse_button == 2) {
                /* the right button: a square next to the unit is a one-step GoTo; any
                 * other square tells about its terrain */
                if (distance(x - p->x, y - p->y) == 1 || (abs(x - p->x) == 79 && abs(y - p->y) <= 1)) {
                    p->goto_x = (uint8_t)x;
                    p->goto_y = (uint8_t)y;
                    p->unk8 = NONE8;
                    return 0;
                }
                if (S->visibility[x][y] & (1 << S->human_civ)) {
                    article_show(3, map_terrain(x, y), NULL, rnd(2));
                    main_screen();
                }
                continue;
            }
            /* the left button: own city -> its screen; own unit -> wake and activate one of
             * the units there; elsewhere the view centres on the square */
            if ((map_improvements(x, y) & IMP_CITY) && (c = city_at(x, y)) >= 0 &&
                (int8_t)S->cities[c].owner == S->human_civ) {
                city_screen(civ, c);
                main_screen();
                continue;
            }
            d = unit_at(x, y);
            if (d >= 0 && stack_owner == civ) {
                d = stack_pick(civ, d);
                if (!(S->units[civ][d].status & 0x09)) {
                    ui_activate_unit = d;
                    return CMD_ACTIVATE;
                }
                main_screen();
                continue;
            }
            view_center(civ, x - 7, y - 6);
            main_screen();
            continue;
        }
    }
}

/* "End of Turn": the map stays up until the player presses Enter. */
int ui_end_of_turn(int civ)
{
    int k, stop, x, y, c, u, on = 0, woke;
    unsigned next = 0, now;

    panel_civ = civ;
    panel_unit = 128;
    main_screen();
    for (;;) {
        now = gfx_ticks();
        if (now >= next) {
            on = !on;
            next = now + 400;
            gfx_fill(4, 124, 72, 8, 7);
            draw_text(map_font, 4, 124, on ? 0 : 7, "End of Turn");
            gfx_present();
        }
        k = gfx_poll_wait();
        if (!k) {
            gfx_delay(10);
            continue;
        }
        if (k == 13 || k == ' ')
            return 0;
        if (common_key(k, &stop)) {
            if (stop)
                return 0;
            continue;
        }
        if (k != KEY_MOUSE)
            continue;
        if (mouse_y < 8) {
            menu_bar_click(civ, 128, &stop);
            if (stop)
                return 0;
            continue;
        }
        if (minimap_click() || !clicked_square(&x, &y))
            continue;
        c = own_city_at(x, y);
        if (c >= 0 && mouse_button != 2) {
            city_screen(civ, c);
            main_screen();
            continue;
        }
        /* wake the human's units there that still have moves */
        woke = 0;
        for (u = 0; u < MAX_UNITS; u++) {
            Unit *p = &S->units[civ][u];
            if (p->type == NONE8 || p->x != x || p->y != y || p->moves == 0)
                continue;
            p->status &= (uint8_t)~(UNIT_SENTRY | UNIT_FORTIFIED | UNIT_FORTIFYING);
            woke = 1;
        }
        if (woke)
            return 1;
        view_center(civ, x - 7, y - 6);
        main_screen();
    }
}

/* o_08_062a: the Civilopedia article of a wonder just built elsewhere. */
void ui_wonder_view(int building, int style)
{
    article_show(1, building, NULL, style);
}

/* o_17_0000's screen: the palace, the sections that can grow numbered (1-7 castle, a-c
 * gardens); the player clicks one or presses its key, then picks a style. */
int ui_palace(int *style)
{
    static uint8_t under[SCR_H][SCR_W];
    char buf[8];
    int i, k, sec = -1;

    memcpy(under, screen, sizeof under);
    palace_draw(0);
    draw_text_centered(dialog_font, 160, 3, 15, "Your palace grows. Select new section.");
    for (i = 1; i <= 7; i++)
        if (palace_section_open(i)) {
            snprintf(buf, sizeof buf, "%d", i);
            shadow_text(buf, i * 48 - 36, 144, 14);
        }
    for (i = 0; i < 3; i++)
        if (palace_section_open(9 + i)) {
            snprintf(buf, sizeof buf, "%c", 'a' + i);
            shadow_text(buf, i * 120 + 40, 160, 14);
        }
    while (sec < 0) {
        k = gfx_wait_key();
        if (k == KEY_QUIT || k == 27)
            break;
        if (k >= '1' && k <= '7')
            sec = k - '0';
        else if (k >= 'a' && k <= 'c')
            sec = k - 'a' + 9;
        else if (k == KEY_MOUSE)
            sec = mouse_y >= 154 ? mouse_x / 107 + 9 : (mouse_x + 12) / 48 + 1;
        if (sec > 0 && !(sec <= 7 ? S->unk_e83e[sec] < 4 : S->unk_e83e[sec] < 3))
            sec = -1;
    }
    *style = 0;
    if (sec > 0 && sec < 9) {
        int st = dialog("Select style:\n Classical\n Medieval\n Oriental\n", -1, 60, 0, 0);
        *style = st < 0 ? 0 : st;
    }
    gfx_set_palette(game_palette);
    memcpy(screen, under, sizeof under);
    gfx_present();
    return sec;
}

/* ------------------------------------------------------------------------------------- */
/* Diplomacy. */

static int dip_civ = -1, female;

void ui_female_leader(int on)
{
    female = on;
}

static void set_names(int civ)
{
    snprintf(text_us, sizeof text_us, "%s", S->civ_names[civ]);
    snprintf(text_them, sizeof text_them, "%s", S->civ_names[S->human_civ]);
}

int ui_receive_envoy(int civ, int embassy)
{
    char msg[256];

    snprintf(msg, sizeof msg, "%s envoy arrives\n Receive Envoy\n Refuse to receive\n%s",
             S->civ_adjectives[civ], embassy ? " Intelligence Report\n" : "");
    return ui_menu(msg, 0);
}

static uint8_t before_audience[SCR_H][SCR_W];
static int portrait_shown;

/* o_06_1d2b: the leader's portrait (KINGnn.PIC, nn from the civilization type). */
void ui_diplomacy_screen(int civ)
{
    static const signed char portrait[16] = { 3, 10, 7, 12, 1, 4, 13, 2, 3, 8, 3, 11, 9, 6, 0, 5 };
    char name[32];
    Picture p, pal;
    int t = S->civ_type[civ] & 15;

    dip_civ = civ;
    set_names(civ);
    snprintf(name, sizeof name, "KING%02d.PIC", portrait[t]);
    if (load_picture(name, &p) != 0)
        return;
    memcpy(before_audience, screen, sizeof before_audience);
    snprintf(name, sizeof name, "KING%02d.PAL", portrait[t]);
    if (load_picture(name, &pal) == 0 && pal.has_palette)
        gfx_set_palette((const uint8_t (*)[3])pal.palette);
    else if (p.has_palette)
        gfx_set_palette((const uint8_t (*)[3])p.palette);
    pic_free(&pal);
    gfx_blit(&p, 0, 0, 320, 200, 0, 0, -1);
    pic_free(&p);
    portrait_shown = 1;
    gfx_present();
}

void ui_diplomacy_screen_end(void)
{
    if (portrait_shown) {
        memcpy(screen, before_audience, sizeof before_audience);
        gfx_set_palette(game_palette);
        gfx_present();
        portrait_shown = 0;
    }
    dip_civ = -1;
    text_us[0] = text_them[0] = 0;
}

void ui_envoy_greeting(int civ, int variant)
{
    (void)civ;
    (void)variant;
}

void ui_envoy_says_greeting(int civ)
{
    char msg[256];

    snprintf(msg, sizeof msg, "%s %s\nof the %s:\n'Greetings.'\n", leader_titles[S->government[civ]],
             S->leader_names[civ], S->civ_names[civ]);
    dialog(msg, portrait_shown ? 4 : -1, portrait_shown ? 140 : -1, 0, DLG_DARK);
}

static int envoy(int civ, const char *text, const char *a, const char *b)
{
    char buf[4096], body[3800];

    set_names(civ);
    text_get(text, body, sizeof body, a, b);
    snprintf(buf, sizeof buf, "%s %s:\n%s", leader_titles[S->government[civ]], S->leader_names[civ], body);
    return dialog(buf, portrait_shown ? 4 : -1, portrait_shown ? 120 : -1, 0, DLG_DARK);
}

int ui_envoy_says(int civ, const char *text, const char *subst1, const char *subst2)
{
    return envoy(civ, text, subst1, subst2);
}

int ui_envoy_asks(int civ, const char *text, const char *subst1, const char *subst2, int mood)
{
    (void)mood;
    return envoy(civ, text, subst1, subst2);
}

void ui_envoy_message(const char *key)
{
    envoy(dip_civ >= 0 ? dip_civ : 0, key, NULL, NULL);
}

int ui_envoy_threat(int civ, int disabled_mask)
{
    (void)civ;
    return ui_menu("Your reply:\n Declare war\n Make peace\n", disabled_mask);
}

int ui_envoy_proposal(int civ, const char *section, const char *subst1, const char *subst2)
{
    return envoy(civ, section, subst1, subst2);
}

/* ------------------------------------------------------------------------------------- */
/* Spaceships. */

static int launch_wanted;

/* o_18_0000: the spaceship screen: the ship's parts (DOCKER.PIC) among stars. */
static const short ship_part_rect[10][4] = {
    { 0, 16, 16, 16 }, { 16, 16, 16, 16 }, { 32, 16, 16, 16 }, { 256, 48, 32, 16 }, { 0, 80, 16, 16 },
    { 192, 96, 32, 32 }, { 224, 96, 32, 32 }, { 288, 96, 32, 32 }, { 0, 96, 32, 32 }, { 256, 96, 32, 32 },
};
static Picture docker;
static int ship_civ;

static void draw_ship(int civ)
{
    int x, y, v;

    gfx_fill(0, 8, 240, 192, 0);
    for (x = 0; x < 15; x++)
        for (y = 0; y < 12; y++) {
            v = (int8_t)S->unk_far_2786_0740_bytes[civ * 180 + x * 12 + y];
            if (v < 0 || v > 9)
                continue;
            sprite_draw(spr(&docker, ship_part_rect[v][0], ship_part_rect[v][1], ship_part_rect[v][2],
                            ship_part_rect[v][3]), (x << 4) - 16, (y << 4) + 8);
        }
}

void ui_spaceship_screen(int civ)
{
    char title[64];

    screen_save();
    if (!docker.pixels)
        load_picture("DOCKER.PIC", &docker);
    ship_civ = civ;
    gfx_fill(0, 0, 320, 200, 0);
    gfx_fill(0, 0, 320, 8, civ_colour[civ & 7]);
    snprintf(title, sizeof title, "%s SpaceShip", S->civ_adjectives[civ]);
    draw_text_centered(map_font, 160, 1, 0, title);
    draw_ship(civ);
    gfx_present();
}

void ui_spaceship_screen_end(void)
{
    screen_restore();
    gfx_present();
}

int ui_spaceship_star(int i, int x, int y, int colour)
{
    (void)i;
    if (x >= 240 || gfx_get(x, y) != 0)
        return 0;
    gfx_pixel(x, y, colour);
    return 1;
}

int ui_spaceship_figures(int civ, const SpaceshipFigures *f, int frames, int may_launch)
{
    char buf[64];
    int y = 16, n = 0, k;

    gfx_fill(240, 8, 80, 192, civ_colour[civ & 7]);
    gfx_vline(0, 8, 199, civ_colour2[civ & 7]);
    draw_text(map_font, 242, y, 0, "POPULATION:");
    snprintf(buf, sizeof buf, "%d0,000", f->population);
    draw_text(map_font, 242, y += 8, 0, buf);
    snprintf(buf, sizeof buf, "SUPPORT: %d%%", f->support);
    draw_text(map_font, 242, y += 8, f->support >= 100 ? 0 : 12, buf);
    snprintf(buf, sizeof buf, "ENERGY: %d%%", f->energy);
    draw_text(map_font, 242, y += 8, f->energy >= 100 ? 0 : 12, buf);
    draw_text(map_font, 242, y += 16, 0, "MASS:");
    snprintf(buf, sizeof buf, "%d,%d00 TONS", f->mass / 10, f->mass % 10);
    draw_text(map_font, 242, y += 8, 0, buf);
    snprintf(buf, sizeof buf, "FUEL: %d%%", f->fuel);
    draw_text(map_font, 242, y += 8, f->fuel >= 100 ? 0 : 12, buf);
    draw_text(map_font, 242, y += 8, 0, "FLIGHT TIME:");
    snprintf(buf, sizeof buf, "%d.%d YEARS", f->flight_time / 10, f->flight_time % 10);
    draw_text(map_font, 242, y += 8, 0, buf);
    draw_text(map_font, 242, y += 16, 0, "PROB. OF SUCCESS");
    snprintf(buf, sizeof buf, "--- %d%% ---", f->success);
    draw_text(map_font, 242, y += 8, 0, buf);
    if (may_launch) {
        gfx_rect(242, 188, 76, 9, 0);
        draw_text(map_font, 256, 190, 0, "LAUNCH");
    }
    /* The engine moves the stars (they take random numbers) for the frames shown here. */
    launch_wanted = 0;
    gfx_present();
    for (;;) {
        n++;
        if (frames > 0 && n >= frames)
            break;
        k = gfx_poll_wait();
        if (!k) {
            gfx_delay(60);
            continue;
        }
        if (may_launch && (k == 'l' || (k == KEY_MOUSE && mouse_x > 242 && mouse_y >= 188)))
            launch_wanted = 1;
        if (frames < 0 || k == KEY_QUIT)
            break;
    }
    return n;
}

int ui_launch_requested(void)
{
    int r = launch_wanted;
    launch_wanted = 0;
    return r;
}

int ui_spaceship_choose(const char *menu, int disabled_mask)
{
    return ui_menu(menu, disabled_mask);
}

int ui_choose_spaceship(int disabled_mask)
{
    char menu[512];
    int c;

    snprintf(menu, sizeof menu, "View which spaceship?\n");
    for (c = 1; c < MAX_CIVS; c++) {
        strncat(menu, " ", sizeof menu - strlen(menu) - 1);
        strncat(menu, S->civ_names[c], sizeof menu - strlen(menu) - 1);
        strncat(menu, "\n", sizeof menu - strlen(menu) - 1);
    }
    return dialog(menu, -1, -1, disabled_mask, DLG_ESCAPE);
}

void ui_spaceship_arrives(int civ)
{
    char msg[128];

    snprintf(msg, sizeof msg, "The %s spaceship\narrives at Alpha Centauri!\n", S->civ_adjectives[civ]);
    ui_message(msg, 0);
}

/* ------------------------------------------------------------------------------------- */
/* History and the end. */

static char hist_lines[16][80];
static int hist_n;
static char hist_title[80];

void ui_historian_begin(const char *title)
{
    snprintf(hist_title, sizeof hist_title, "%s", title);
    hist_n = 0;
}

void ui_historian_line(int civ, const char *line)
{
    (void)civ;
    if (hist_n < 16)
        snprintf(hist_lines[hist_n++], 80, "%s", line);
}

void ui_historian_end(void)
{
    char buf[2048];
    int i;

    snprintf(buf, sizeof buf, "%s\n", hist_title);
    for (i = 0; i < hist_n; i++) {
        strncat(buf, hist_lines[i], sizeof buf - strlen(buf) - 1);
        strncat(buf, "\n", sizeof buf - strlen(buf) - 1);
    }
    dialog(buf, -1, -1, 0, DLG_DARK);
}

void ui_civilization_lost(void)
{
    ui_message("Your civilization\nhas been destroyed.\n", 0);
}

static int year_of_turn(int turn)
{
    int t, y = -4000;

    for (t = 0; t < turn; t++) {
        if (y < 1000)
            y += 20;
        else if (y < 1500)
            y += 10;
        else if (y < 1750)
            y += 5;
        else if (y < 1850)
            y += 2;
        else
            y++;
        if (y == 0)
            y = 1;
        if (y == 21)
            y = 20;
    }
    return y;
}

static const char *civ_name_of(int civ)
{
    if (civ == 0 && !S->civ_names[0][0])
        return "Barbarians";
    return civ >= 0 && civ < MAX_CIVS ? S->civ_names[civ] : "?";
}

static int year_of_turn(int turn);

static void line(int x1, int y1, int x2, int y2, int c)
{
    int dx = abs(x2 - x1), dy = -abs(y2 - y1), sx = x1 < x2 ? 1 : -1, sy = y1 < y2 ? 1 : -1, e = dx + dy, e2;

    for (;;) {
        gfx_pixel(x1, y1, c);
        if (x1 == x2 && y1 == y2)
            break;
        e2 = 2 * e;
        if (e2 >= dy) {
            e += dy;
            x1 += sx;
        }
        if (e2 <= dx) {
            e += dx;
            y1 += sy;
        }
    }
}

/* o_12_0567: the civilizations' power, sampled every 4 turns by civ_ranking. */
static void power_graph(void)
{
    char buf[32];
    int t, c, k, top = 50, n = S->turn / 4, px, py, x, y, v;

    gfx_fill(0, 0, 320, 200, 8);
    gfx_rect(4, 9, 300, 183, 0);
    for (t = 0; t <= S->turn && t <= 600; t += 50) {
        gfx_vline(t / 2 + 4, 9, 192, 0);
        if (t % 100 == 0) {
            y = year_of_turn(t);
            snprintf(buf, sizeof buf, "%d%s", y < 0 ? -y : y, y < 0 ? "BC" : "AD");
            draw_text(map_font, t / 2 + 4, 194, 15, buf);
        }
    }
    for (c = 1; c < MAX_CIVS; c++) {
        if (!(S->civs_alive & (1 << c)) && c > S->unk_e214 + 1)
            continue;
        draw_text(map_font, 8, c * 8 + 4, civ_colour[c], S->civ_adjectives[c]);
        for (k = 0; k < n && k < 150; k++)
            if (S->unk_far_2786_0000[k * 8 + c] > top)
                top = S->unk_far_2786_0000[k * 8 + c];
    }
    shadow_text("CIVILIZATION POWER Graph", 100, 2, 15);
    for (c = 1; c < MAX_CIVS; c++) {
        if (!(S->civs_alive & (1 << c)) && c > S->unk_e214 + 1)
            continue;
        px = 0;
        py = 0;
        for (k = 0; k < n && k < 150; k++) {
            x = k * 2;
            v = 180 * S->unk_far_2786_0000[k * 8 + c] / top;
            line(px + 4, 192 - py, x + 4, 192 - v, civ_colour[c]);
            px = x;
            py = v;
        }
    }
    gfx_wait_key();
}

/* o_09_0000: the replay, as a chronicle of the log. */
void ui_replay(void)
{
    static char lines[1200][80];
    const uint8_t *r = S->replay;
    int p = 0, n = 0, kind, turn, y, a[4], na, k;
    static const int nargs[16] = { 0, 4, 1, 1, 0, 2, 2, 0, 2, 4, 2, 3, 4, 2, 0, 0 };
    char when[16];

    while (p + 2 <= S->replay_len && n < 1200) {
        kind = r[p] >> 4;
        turn = (r[p] & 15) << 8 | r[p + 1];
        na = nargs[kind];
        if (p + 2 + na > S->replay_len)
            break;
        for (k = 0; k < na; k++)
            a[k] = r[p + 2 + k];
        p += 2 + na;
        y = year_of_turn(turn);
        snprintf(when, sizeof when, "%d%s", y < 0 ? -y : y, y < 0 ? "BC" : "AD");
        switch (kind) {
        case 1:
            if (a[0] == 0xFF)
                snprintf(lines[n++], 80, "%s: %s destroyed", when, S->city_names[a[1]]);
            else
                snprintf(lines[n++], 80, "%s: %s found %s", when, civ_name_of(a[0]), S->city_names[a[1]]);
            break;
        case 2:
            snprintf(lines[n++], 80, "%s: %s declare war on the %s", when, civ_name_of(a[0] >> 4), civ_name_of(a[0] & 15));
            break;
        case 3:
            snprintf(lines[n++], 80, "%s: %s make peace with the %s", when, civ_name_of(a[0] >> 4), civ_name_of(a[0] & 15));
            break;
        case 5:
            snprintf(lines[n++], 80, "%s: %s discover %s", when, civ_name_of(a[0]),
                     a[1] < NUM_ADVANCES ? advance_types[a[1]].name : "?");
            break;
        case 6:
            snprintf(lines[n++], 80, "%s: %s build the first %s", when, civ_name_of(a[0]),
                     a[1] < NUM_UNIT_TYPES ? S->unit_type_names[a[1]] : "?");
            break;
        case 8:
            snprintf(lines[n++], 80, "%s: %s adopt %s", when, civ_name_of(a[0]),
                     a[1] < 6 ? government_names[a[1]] : "?");
            break;
        case 9:
            snprintf(lines[n++], 80, "%s: %s capture %s", when, civ_name_of(a[0]), S->city_names[a[1]]);
            break;
        case 10:
            snprintf(lines[n++], 80, "%s: %s complete %s", when, civ_name_of(a[0]),
                     a[1] >= 1 && a[1] <= NUM_WONDERS ? building_types[FIRST_WONDER + a[1] - 1].name : "?");
            break;
        case 11:
            snprintf(lines[n++], 80, "%s: %d cities, %d0,000 citizens", when, a[0], a[1] * 256 + a[2]);
            break;
        case 13:
            snprintf(lines[n++], 80, "%s: the %s are destroyed by the %s", when, civ_name_of(a[0]), civ_name_of(a[1]));
            break;
        default:
            break;
        }
    }
    power_graph();
    list_screen("REPLAY", lines, n);
    main_screen();
}

/* f_0237_08a2: the replay, the score, the Hall of Fame. */
void ui_game_over(int spaceship_civ)
{
    static char lines[12][80];
    char path[512];
    FILE *f;
    int n = 0, score = civ_score(S->human_civ);

    (void)spaceship_civ;
    ui_replay();
    snprintf(lines[n++], 80, "%s %s of the %s", leader_titles[S->government[S->human_civ]],
             S->leader_names[S->human_civ], S->civ_names[S->human_civ]);
    snprintf(lines[n++], 80, "Year: %d %s", S->year < 0 ? -S->year : S->year, S->year < 0 ? "BC" : "AD");
    snprintf(lines[n++], 80, "Civilization score: %d", score);
    snprintf(lines[n++], 80, "Cities: %d   Advances: %d", S->city_count[S->human_civ], S->advances_known[S->human_civ]);
    list_screen("FINAL SCORE", lines, n);
    if (!(S->unk_4c88 & 0x100)) {
        make_save_dir();
        snprintf(path, sizeof path, "%s/FAME.TXT", save_dir);
        if ((f = fopen(path, "a")) != NULL) {
            fprintf(f, "%d\t%s\t%s\t%d %s\t%s\n", score, S->leader_names[S->human_civ], S->civ_names[S->human_civ],
                    S->year < 0 ? -S->year : S->year, S->year < 0 ? "BC" : "AD",
                    (const char *[]){ "Chieftain", "Warlord", "Prince", "King", "Emperor" }[S->difficulty % 5]);
            fclose(f);
        }
    }
}

/* f_0237_090c: the closing picture. */
void ui_final_screens(void)
{
    Picture p;

    if (load_picture("ADSCREEN.PIC", &p) == 0) {
        if (p.has_palette)
            gfx_set_palette((const uint8_t (*)[3])p.palette);
        gfx_blit(&p, 0, 0, 320, 200, 0, 0, -1);
        gfx_wait_key();
        pic_free(&p);
        gfx_set_palette(game_palette);
    }
}
