/* The advisors (overlay 14): City Status, Military (and Losses), Intelligence, Attitude,
 * Trade and Science.  Each is a page with the same heading (o_14_001c) that waits for a key. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gfx.h"
#include "font.h"
#include "assets.h"
#include "mapview.h"
#include "dialog.h"
#include "article.h"
#include "../game.h"
#include "../map.h"
#include "../util.h"
#include "../city.h"
#include "../civ.h"
#include "../view.h"
#include "../ui.h"
#include "../turn.h"
#include "../unit.h"

#define S (&game.s)

void building_icon(int b, int x, int y);
int  citizens(int city, int x, int y, int specialists, int maxw);
void shorten_in(int font, char *s, int maxw);
void button(int x1, int y1, int x2, int y2, const char *text, int c);

static uint8_t under[SCR_H][SCR_W];

static void text(int x, int y, int colour, const char *s)        /* f_0181_005e */
{
    draw_text(0, x, y, colour, s);
}

static void shadow(int x, int y, int colour, const char *s)      /* f_0181_0088 */
{
    draw_text(0, x, y + 1, 0, s);
    draw_text(0, x, y, colour, s);
}

static void centred(int x, int y, int colour, const char *s)     /* f_0181_00b5 */
{
    draw_text(0, x - text_width(0, s) / 2, y, colour, s);
}

/* o_05_1adf: the advisors of the government's era (GOVTna/m.PIC) go into SP299 (160, 140);
 * the original does this when a game starts or the government changes. */
static int portraits_gov = -1;

void advisor_portraits(void)
{
    int h = S->human_civ, g = S->government[h], x, y;
    char name[16];
    Picture p;

    snprintf(name, sizeof name, "GOVT%c%c.PIC", g == GOV_COMMUNISM ? '3' : '0' + g / 2,
             g != GOV_COMMUNISM && civ_has_advance(h, 20) ? 'M' : 'A');
    portraits_gov = g;
    if (load_picture(name, &p) != 0)
        return;
    for (y = 0; y < 60 && y < p.height; y++)
        for (x = 0; x < 160 && x < p.width; x++)
            sp299.pixels[(140 + y) * sp299.width + 160 + x] = p.pixels[y * p.width + x];
    pic_free(&p);
}

static void portrait(int sx)                     /* an advisor: SP299 (sx, 140) at (278, 2) */
{
    if (portraits_gov != S->government[S->human_civ])
        advisor_portraits();
    gfx_blit(&sp299, sx, 140, 40, 60, 278, 2, -1);
}

/* o_14_001c: the page and its heading */
static void heading(const char *title, int colour)
{
    static const char *const realm[3] = { "Empire", "Kingdom", "Republic" };
    int h = S->human_civ, g = S->government[h];
    char buf[96], year[24];

    gfx_fill(0, 0, 320, 200, colour);
    centred(160, 2, 15, title);
    snprintf(buf, sizeof buf, "%s of the %s", realm[g / 2 > 2 ? 2 : g / 2], S->civ_names[h]);
    centred(160, 10, 15, buf);
    snprintf(year, sizeof year, "%d %s", S->year < 0 ? -S->year : S->year, S->year < 0 ? "BC" : "AD");
    snprintf(buf, sizeof buf, "%s %s: %s", leader_titles[g], S->leader_names[h], year);
    centred(160, 18, 15, buf);
}

static void begin(const char *title, int colour)                 /* o_14_0000 */
{
    gfx_cursor(0);
    memcpy(under, screen, sizeof under);
    heading(title, colour);
}

static void end(void)                                             /* o_14_013b */
{
    gfx_wait_key();
    gfx_cursor(1);
    memcpy(screen, under, sizeof under);
    gfx_present();
}

static void more(const char *title, int colour)                  /* a new page of the same */
{
    gfx_wait_key();
    heading(title, colour);
}

/* o_14_1869 */
void advisor_city_status(void)
{
    int civ = S->human_civ, c, y = 32;
    char buf[96];

    begin("CITY STATUS", 8);
    for (c = 0; c < MAX_CITIES; c++) {
        const City *ct = &S->cities[c];
        if (ct->status == NONE8 || (int8_t)ct->owner != civ)
            continue;
        if (y >= 192) {
            more("CITY STATUS", 8);
            y = 32;
        }
        city_process(c, -1);
        text(8, y, 15, S->city_names[ct->name]);
        snprintf(buf, sizeof buf, "%d-%d{ %d| %d}", (int8_t)ct->size, calc.yield[0], calc.yield[1], calc.yield[2]);
        text(80, y, 15, buf);
        if (ct->producing < 0) {
            snprintf(buf, sizeof buf, "%s", building_types[-ct->producing].name);
            shorten_in(0, buf, 96);
            snprintf(buf + strlen(buf), sizeof buf - strlen(buf), "\x87 (%d/%d)", ct->shields,
                     building_types[-ct->producing].cost * 10);
        } else if (ct->producing < 64)
            snprintf(buf, sizeof buf, "%s\x87 (%d/%d)", S->unit_type_names[ct->producing], ct->shields,
                     S->unit_types[ct->producing].cost * 10);
        text(172, y, 15, buf);
        y += 8;
    }
    end();
}

/* o_14_061f */
static void military_losses(int civ)
{
    int t, k, any, n = 0, y = 28, x;
    char buf[16];

    begin("MILITARY LOSSES", 4);
    for (t = 0; t < 27; t++) {
        for (k = any = 0; k < MAX_CIVS; k++)
            if (S->units_lost[k][t])
                any = 1;
        if (!any)
            continue;
        gfx_hline(36, 319, y - 1, 8);
        sprite_draw(unit_sprite(civ, t), (n++ & 1) * 16 + 2, y - 4);
        text(36, y, 15, S->unit_type_names[t]);
        x = 104;
        if (S->units_lost[civ][t]) {
            snprintf(buf, sizeof buf, "%d", S->units_lost[civ][t]);
            text(104, y, 12, buf);
        }
        for (k = 1; k < MAX_CIVS; k++) {
            if (k == civ)
                continue;
            x += 28;
            if (S->units_lost[k][t]) {
                snprintf(buf, sizeof buf, "%d", S->units_lost[k][t]);
                text(x, y, civ_colour[k], buf);
            }
        }
        y += 7;
    }
    gfx_rect(100, 25, 18, y - 25, civ_colour[S->human_civ]);
    end();
}

/* o_14_03ad */
void advisor_military(void)
{
    int civ = S->human_civ, t, n = 0, y = 32;
    char buf[48];

    begin("MILITARY STATUS", 1);
    for (t = 0; t < 28; t++) {
        if (S->units_active[civ][t] <= 0 && S->units_in_production[civ][t] <= 0)
            continue;
        gfx_hline(36, 319, y - 2, 9);
        sprite_draw(unit_sprite(civ, t), (n++ & 1) * 16 + 2, y - 4);
        text(36, y, 15, S->unit_type_names[t]);
        snprintf(buf, sizeof buf, "(%d/%d/%d)", S->unit_types[t].attack, S->unit_types[t].defense,
                 S->unit_types[t].moves);
        text(112, y, 11, buf);
        if (S->units_active[civ][t] > 0) {
            snprintf(buf, sizeof buf, "%d active", S->units_active[civ][t]);
            text(168, y, 15, buf);
        }
        if (S->units_in_production[civ][t] > 0) {
            snprintf(buf, sizeof buf, "%d in production", S->units_in_production[civ][t]);
            text(232, y, 11, buf);
        }
        y += 9;
    }
    portrait(160);
    end();
    military_losses(civ);
}

/* o_14_115e: one civilization in detail */
static void intelligence_detail(int civ)
{
    const CivType *ct = &civ_types[S->civ_type[civ]];
    int y = 32, c, cap = -1, k;
    char buf[96];

    heading("INTELLIGENCE REPORT", 1);
    snprintf(buf, sizeof buf, "Subject: the %s", S->civ_names[civ]);
    shadow(16, 32, 15, buf);
    y += 12;
    snprintf(buf, sizeof buf, "Leader:\x8f %s %s", leader_titles[S->government[civ]], S->leader_names[civ]);
    text(16, y, 9, buf);
    y += 8;
    if (ct->aggression == 1)
        text(24, (y += 8) - 8, 7, "Aggressive");
    if (ct->aggression == -1)
        text(24, (y += 8) - 8, 7, "Friendly");
    if (ct->unk1 == 1)
        text(24, (y += 8) - 8, 7, "Expansionistic");
    if (ct->unk1 == -1)
        text(24, (y += 8) - 8, 7, "Perfectionist");
    if (ct->science_bias == 1)
        text(24, (y += 8) - 8, 7, "Civilized");
    if (ct->science_bias == -1)
        text(24, (y += 8) - 8, 7, "Militaristic");
    y += 4;
    for (c = 0; c < MAX_CITIES; c++)
        if ((int8_t)S->cities[c].owner == civ && S->cities[c].status != NONE8 &&
            (S->cities[c].buildings & BIT(B_PALACE)))
            cap = c;
    snprintf(buf, sizeof buf, "Capital:\x8f %s", cap < 0 ? "NONE" : S->city_names[S->cities[cap].name]);
    text(16, y, 9, buf);
    y += 8;
    snprintf(buf, sizeof buf, "Government:\x8f %s", government_names[S->government[civ]]);
    text(16, y, 9, buf);
    y += 8;
    snprintf(buf, sizeof buf, "Treasury:\x8f %d$", S->treasury[civ]);
    text(16, y, 9, buf);
    y += 8;
    if (S->difficulty <= 1) {
        snprintf(buf, sizeof buf, "Military:\x8f %d Units.", S->unit_count[civ]);
        text(16, y, 9, buf);
        y += 12;
    }
    text(16, y, 9, "Foreign Affairs:");
    y += 8;
    for (k = 1; k < MAX_CIVS; k++) {
        buf[0] = 0;
        if (S->diplomacy[civ][k] & 2)
            strcpy(buf, "At\x8f PEACE");
        if ((S->diplomacy[civ][k] & 3) == 1)
            strcpy(buf, "At\x8c WAR");
        if (S->diplomacy[k][civ] & 1) {
            snprintf(buf + strlen(buf), sizeof buf - strlen(buf), "\x87 with the %s.", S->civ_names[k]);
            text(24, y, 7, buf);
            y += 8;
        }
    }
    y += 4;
    text(16, y, 9, "Technologies:");
    y += 8;
    for (k = 71; k >= 0; k--)
        if (civ_has_advance(civ, k)) {
            text(24, y, 7, advance_types[k].name);
            y += 8;
            if (y >= 192)
                break;
        }
    gfx_wait_key();
}

/* o_14_0d3d */
void advisor_intelligence(void)
{
    int h = S->human_civ, civ, k, n = 0, y, x, list[8], i, k2, detail = 0;
    char buf[96];

    begin("INTELLIGENCE REPORT", 1);
    for (civ = 1; civ < MAX_CIVS; civ++) {
        if (!(S->civs_alive & (1 << civ)))
            continue;
        y = n * 24 + 32;
        gfx_hline(4, 316, y - 2, 9);
        if (civ != h && !(S->diplomacy[h][civ] & 0x40)) {
            text(96, y, civ_colour[civ], "No embassy established.");
            n++;
            continue;
        }
        {
            int cap = -1;
            for (k = 0; k < MAX_CITIES; k++)
                if ((int8_t)S->cities[k].owner == civ && S->cities[k].status != NONE8 &&
                    (S->cities[k].buildings & BIT(B_PALACE)))
                    cap = k;
            (void)cap;
        }
        snprintf(buf, sizeof buf, "%s: %s", S->civ_names[civ], S->leader_names[civ]);
        shadow(8, y, 15, buf);
        snprintf(buf, sizeof buf, "%s, %d$", government_names[S->government[civ]], S->treasury[civ]);
        if (S->difficulty <= 1)
            snprintf(buf + strlen(buf), sizeof buf - strlen(buf), ", %d Units.", S->unit_count[civ]);
        text(160, y, civ_colour[civ], buf);
        y += 8;
        x = 16;
        for (k = 1; k < MAX_CIVS; k++) {
            int d = S->diplomacy[civ][k];
            if (x > 240) {
                x = 16;
                y += 8;
            }
            if (d & 2)
                snprintf(buf, sizeof buf, "%s%s", S->civ_names[k], (d & 4) ? ":\x8f ALLIED" : ":\x8f PEACE");
            else if ((d & 3) == 1)
                snprintf(buf, sizeof buf, "%s:\x8c WAR", S->civ_names[k]);
            else
                continue;
            text(x, y, 7, buf);
            x += 96;
        }
        list[n++] = civ;
        if (civ != h) {
            snprintf(buf, sizeof buf, "INFO%d", n);
            button(281, n * 24 + 20, 318, n * 24 + 29, buf, civ_colour[civ]);
        }
    }
    gfx_cursor(1);                           /* (L1073: the pointer shows while it waits) */
    gfx_present();
    for (;;) {
        k = gfx_wait_key();
        i = -1;
        if (k >= '1' && k <= '7')
            i = k - '1';
        else if (k == KEY_MOUSE && mouse_x > 281)
            i = (mouse_y - 28) / 24;
        mouse_x = 270;                       /* (f_0000_16a0: the pointer moves to the buttons) */
        if (i >= 0 && i < n && (k2 = list[i]) != h && (S->diplomacy[h][k2] & 0x40)) {
            intelligence_detail(k2);
            detail = 1;
        }
        break;
    }
    gfx_cursor(1);
    if (detail)
        redraw_map();
    else {
        memcpy(screen, under, sizeof under);
        gfx_present();
    }
}

static void percent(char *buf, size_t n, int a, int b)           /* o_14_1817 */
{
    snprintf(buf + strlen(buf), n - strlen(buf), "%d", a < 300 ? a * 100 / b : a * 10 / (b / 10));
}

/* o_14_15ee */
void advisor_attitude(void)
{
    int civ = S->human_civ, c, y = 32, x, b, people = 0, happy = 0, unhappy = 0, shift = 0, pop;
    char buf[160];

    begin("ATTITUDE SURVEY", 9);
    for (c = 0; c < MAX_CITIES; c++) {
        const City *ct = &S->cities[c];
        if (ct->status == NONE8 || (int8_t)ct->owner != civ)
            continue;
        if (y > 190) {
            more("ATTITUDE SURVEY", 9);
            y = 32;
        }
        city_process(c, -1);
        snprintf(buf, sizeof buf, "%s:", S->city_names[ct->name]);
        text(16, y, 15, buf);
        citizens(c, (shift & 7) + 80, y - 3, calc.specialists, 124);
        gfx_fill(212, y - 1, 90, 10, 11);
        for (b = 0, x = 212; b < 24; b++)
            if (ct->buildings & (1UL << b) & 0x2618) {
                building_icon(b + 1, x, y - 1);
                x += 18;
            }
        people += (int8_t)ct->size;
        happy += calc.happy;
        unhappy += calc.unhappy;
        shift += 4;
        y += 10;
    }
    if (!people)
        people++;
    pop = civ_population(civ);
    snprintf(buf, sizeof buf, "Population: ");
    if (pop >= 100)
        snprintf(buf + strlen(buf), sizeof buf - strlen(buf), "%d,%s", pop / 100, pop % 100 < 10 ? "0" : "");
    snprintf(buf + strlen(buf), sizeof buf - strlen(buf), "%d0,000 Happy:", pop % 100);
    percent(buf, sizeof buf, happy, people);
    strcat(buf, "% Content:");
    percent(buf, sizeof buf, people - happy - unhappy, people);
    strcat(buf, "% Unhappy:");
    percent(buf, sizeof buf, unhappy, people);
    strcat(buf, "%");
    text(16, y + 8, 15, buf);
    end();
}

/* o_14_07f1 */
void advisor_trade(void)
{
    int civ = S->human_civ, c, b, y = 40, income = 0, science = 0, upkeep[24], extra = 0, total = 0, f, n;
    char buf[96];

    extra = civ_has_advance(civ, 34) + civ_has_advance(civ, 37);    /* (the palace costs more) */
    begin("TRADE REPORT", 2);
    portrait(200);
    text(8, 32, 15, "City Trade");
    memset(upkeep, 0, sizeof upkeep);
    for (c = 0; c < MAX_CITIES; c++) {
        const City *ct = &S->cities[c];
        if (ct->status == NONE8 || (int8_t)ct->owner != civ)
            continue;
        if (y >= 180) {
            more("TRADE REPORT", 2);
            y = 40;
        }
        city_process(c, -1);
        snprintf(buf, sizeof buf, "%s", S->city_names[ct->name]);
        shorten_in(0, buf, 62);
        if (y < 180)
            shadow(16, y, 15, buf);
        if (ct->status & CITY_DISORDER)
            snprintf(buf, sizeof buf, " \x8c(Disorder)");
        else {
            snprintf(buf, sizeof buf, " %d\\/%d$/%d~", calc.lux, calc.tax, calc.science);
            income += calc.tax;
            science += calc.science;
        }
        if (y < 180) {
            text(80, y, 10, buf);
            y += 8;
        }
        for (b = 1; b < 24; b++)
            if (ct->buildings & (1UL << b)) {
                upkeep[b] += building_types[b + 1].upkeep;
                if (b == 1)
                    upkeep[b] += extra;
            }
    }
    snprintf(buf, sizeof buf, "Total Income: %d$", income);
    text(8, y + 4, 10, buf);
    if (civ_has_wonder(S->human_civ, 20))
        science += science / 2;
    if (science != 0 && y < 180) {
        f = S->year < 0 ? 1 : S->year < 1750 ? 2 : 3;
        snprintf(buf, sizeof buf, "Discoveries: %d turns",
                 f * S->advances_known[S->human_civ] * (S->difficulty * 2 + 6) / science);
        text(8, y + 12, 10, buf);
    }
    text(160, 32, 15, "Maintenance Costs");
    y = 40;
    for (b = 0; b < 24; b++) {
        if (!upkeep[b])
            continue;
        n = b == 1 ? upkeep[b] / (extra ? extra : 1) : upkeep[b] / building_types[b + 1].upkeep;
        snprintf(buf, sizeof buf, "%d %s, %d$", n, building_types[b + 1].name, upkeep[b]);
        if (y < 180) {
            text(160, y, 14, buf);
            y += 8;
        }
        total += upkeep[b];
    }
    snprintf(buf, sizeof buf, "Total Cost: %d$", total);
    text(160, y + 4, 14, buf);
    end();
}

/* o_14_014b */
void advisor_science(void)
{
    int civ = S->human_civ, k, need, step, w, x0, i, x, y, n;
    char buf[64];

    begin("SCIENCE REPORT", 1);
    k = S->difficulty * 2 + research_bonus + 6;
    if (k < 11 - S->advances_known[civ])
        k = 11 - S->advances_known[civ];
    need = S->advances_known[civ] * (S->year < 0 ? 1 : 2) * k;
    step = clamp(2432 / (need + 1), 1, 64);
    w = clamp(step * need / 8 + 8, 8, 304);
    x0 = 160 - w / 2;
    if (x0 > 40)
        portrait(280);
    gfx_fill(x0, 25, w, 16, 9);
    for (i = 0, x = 0; i < clamp(S->research_progress[civ], 0, need); i++, x += step)
        sprite_draw(icon_sprite(12), x / 8 + x0, 33);
    if (S->human_research != -1) {
        snprintf(buf, sizeof buf, "Researching %s", advance_types[S->human_research].name);
        draw_text(0, 161 - text_width(0, buf) / 2, 26, 0, buf);
        draw_text(0, 160 - text_width(0, buf) / 2, 26, 15, buf);
    }
    y = 42;
    for (k = 0, n = 0; k < 72; k++) {
        if (!civ_has_advance(civ, k))
            continue;
        text((n % 3) * 100 + 8, y, (S->first_discoverer[k] & 7) == civ ? 15 : 11, advance_types[k].name);
        n++;
        if (n % 3 == 0)
            y += 7;
        if (y > 192)
            break;
    }
    end();
}

/* o_14_115e on its own (after an embassy is set up) */
void advisor_intelligence_civ(int civ)
{
    memcpy(under, screen, sizeof under);
    intelligence_detail(civ);
    memcpy(screen, under, sizeof under);
    gfx_present();
}

/* o_12_0801 */
void wonders_of_the_world(void)
{
    int y = 999, w, c, owner;
    char buf[128];

    gfx_cursor(0);
    memcpy(under, screen, sizeof under);
    for (w = 1; w <= NUM_WONDERS; w++) {
        if (S->wonder_city[w] == -1)
            continue;
        if (y > 180) {
            if (y != 999)
                gfx_wait_key();
            gfx_fill(0, 0, 320, 200, 3);
            shadow(100, 12, 15, "The Wonders of the World");
            y = 32;
        }
        c = S->wonder_city[w];
        snprintf(buf, sizeof buf, "%s%s", w <= 7 ? "The " : "", building_types[FIRST_WONDER + w - 1].name);
        if (c == 128) {
            strcat(buf, " (Destroyed)");
            owner = 0;
        } else {
            owner = (int8_t)S->cities[c].owner;
            snprintf(buf + strlen(buf), sizeof buf - strlen(buf), "%s%s. (%s)", w <= 7 ? " of " : " in ",
                     S->city_names[S->cities[c].name], S->civ_adjectives[owner]);
        }
        gfx_rect(8, y, 303, 15, civ_colour[owner]);
        building_icon(FIRST_WONDER + w - 1, 16, y + 3);
        shorten_in(0, buf, 270);
        text(40, y + 5, 15, buf);
        y += 24;
    }
    gfx_wait_key();
    gfx_cursor(1);
    memcpy(screen, under, sizeof under);
    gfx_present();
}

/* o_12_195c: the human's place among the civilizations (lower is better if y < 0), and the
 * leader if there is an embassy with it. */
static void rank(int *v, int y, const char *unit)
{
    static const char *const th[4] = { "st", "nd", "rd", "th" };
    int h = S->human_civ, place = 1, best = -9999, who = h, c;
    char buf[80];

    for (c = 1; c < MAX_CIVS; c++) {
        if (!(S->civs_alive & (1 << c)))
            continue;
        if (y < 0)
            v[c] = -v[c];
        if (v[c] > v[h])
            place++;
        if (best < v[c]) {
            best = v[c];
            who = c;
        }
    }
    y = abs(y);
    snprintf(buf, sizeof buf, "%d%s", place, th[place > 4 ? 3 : place - 1]);
    gfx_hline(4, 316, y - 3, 9);
    shadow(192, y, 15, buf);
    if (who != h && (S->diplomacy[h][who] & 0x40)) {
        snprintf(buf, sizeof buf, "(%s: %d%s)", S->civ_names[who], abs(best), unit);
        text(318 - text_width(0, buf), y, civ_colour[who], buf);
    }
}

static void pop_text(char *buf, size_t n, int pop)                /* f_1dde_0335 */
{
    buf[0] = 0;
    if (pop >= 100)
        snprintf(buf, n, "%d,%s", pop / 100, pop % 100 < 10 ? "0" : "");
    snprintf(buf + strlen(buf), n - strlen(buf), "%d0,000", pop % 100);
}

/* o_12_0d61 */
void demographics(void)
{
    int civ = S->human_civ, size[8], happy[8], gnp[8], food[8], prod[8], mfg[8], pollution[8], health[8],
        educ[8], land[8], v[8], c, x, y, k;
    char buf[96];

    gfx_cursor(0);
    memcpy(under, screen, sizeof under);
    gfx_fill(0, 0, 320, 200, 1);
    snprintf(buf, sizeof buf, "%s Demographics", S->civ_adjectives[civ]);
    centred(160, 4, 15, buf);
    for (k = 0; k < 8; k++) {
        size[k] = 1;
        happy[k] = gnp[k] = food[k] = prod[k] = mfg[k] = pollution[k] = health[k] = educ[k] = land[k] = 0;
    }
    for (x = 0; x < MAP_W; x++)
        for (y = 2; y < 48; y++)
            if (map_terrain(x, y) != TERRAIN_OCEAN)
                land[map_unit_layer_owner(x, y)]++;
    for (c = 0; c < MAX_CITIES; c++) {
        const City *ct = &S->cities[c];
        int o, sz, p;
        if (ct->owner == NONE8 || ct->status == NONE8)
            continue;
        city_process(c, -1);
        o = (int8_t)ct->owner;
        sz = (int8_t)ct->size;
        size[o] += sz;
        happy[o] += sz + calc.happy - calc.unhappy;
        gnp[o] += calc.lux * 2 + calc.tax;
        food[o] += calc.yield[0] - sz * 2;
        prod[o] += calc.yield[0] + calc.yield[1] + calc.yield[2];
        mfg[o] += calc.yield[1];
        p = calc.yield[1] / calc.pollution_divisor - 20 + sz * calc.pollution_factor / 4;
        if (p > 0)
            pollution[o] += p;
        if (ct->buildings & BIT(B_GRANARY))
            health[o] += sz;
        if (ct->buildings & BIT(B_AQUEDUCT))
            health[o] += sz;
        if (ct->buildings & BIT(B_LIBRARY))
            educ[o] += sz;
        if (ct->buildings & BIT(B_UNIVERSITY))
            educ[o] += sz;
    }
    y = 24;
    shadow(8, y, 15, "Approval Rating:");
    for (k = 1; k < 8; k++) {
        v[k] = happy[k] * 50 / size[k];
        if (k == civ) {
            snprintf(buf, sizeof buf, "%d%%", v[k]);
            text(104, y, 15, buf);
        }
    }
    rank(v, y, "%");
    y += 12;
    shadow(8, y, 15, "Population:");
    for (k = 1; k < 8; k++) {
        v[k] = civ_population(k);
        if (k == civ) {
            pop_text(buf, sizeof buf, v[k]);
            text(104, y, 15, buf);
        }
    }
    rank(v, y, "0000");
    y += 12;
    shadow(8, y, 15, "GNP:");
    for (k = 1; k < 8; k++) {
        v[k] = gnp[k];
        if (k == civ) {
            snprintf(buf, sizeof buf, "%d million $", v[k]);
            text(104, y, 15, buf);
        }
    }
    rank(v, y, "M$");
    y += 12;
    shadow(8, y, 15, "Mfg. Goods:");
    for (k = 1; k < 8; k++) {
        v[k] = mfg[k];
        if (k == civ) {
            snprintf(buf, sizeof buf, "%d Mtons", v[k]);
            text(104, y, 15, buf);
        }
    }
    rank(v, y, " Mtons");
    y += 12;
    shadow(8, y, 15, "Land Area:");
    for (k = 1; k < 8; k++) {
        v[k] = land[k];
        if (k == civ) {
            snprintf(buf, sizeof buf, "%d,000 sq.miles", v[k]);
            text(104, y, 15, buf);
        }
    }
    rank(v, y, ",000");
    y += 12;
    shadow(8, y, 15, "Literacy:");
    for (k = 1; k < 8; k++) {
        v[k] = (size[k] * 2 + educ[k]) * 2 / size[k];
        if (civ_has_advance(k, 0))
            v[k] <<= 1;
        if (civ_has_advance(k, 22))
            v[k] <<= 1;
        if (civ_has_advance(k, 30))
            v[k] <<= 1;
        if (civ_has_advance(k, 13))
            v[k] <<= 1;
        v[k] = clamp(v[k], 0, 100);
        if (k == civ) {
            snprintf(buf, sizeof buf, "%d%%", v[k]);
            text(104, y, 15, buf);
        }
    }
    rank(v, y, "%");
    y += 12;
    shadow(8, y, 15, "Disease:");
    for (k = 1; k < 8; k++) {
        v[k] = size[k] * 50 / (size[k] + health[k]);
        if (civ_has_advance(k, 10))
            v[k] /= 2;
        health[k] = 1500 / (v[k] + 20);
        if (k == civ) {
            snprintf(buf, sizeof buf, "%d%%", v[k]);
            text(104, y, 15, buf);
        }
    }
    rank(v, -y, "%");
    y += 12;
    shadow(8, y, 15, "Pollution:");
    for (k = 1; k < 8; k++) {
        v[k] = pollution[k];
        health[k] -= pollution[k] * 10 / size[k];
        if (k == civ) {
            snprintf(buf, sizeof buf, "%d0 tons/year", v[k]);
            text(104, y, 15, buf);
        }
    }
    rank(v, -y, "0 tons");
    y += 12;
    shadow(8, y, 15, "Life Expectancy:");
    for (k = 1; k < 8; k++) {
        v[k] = clamp(health[k], 20, 99);
        if (k == civ) {
            snprintf(buf, sizeof buf, "%d years", v[k]);
            text(104, y, 15, buf);
        }
    }
    rank(v, y, " yrs");
    y += 12;
    shadow(8, y, 15, "Family Size:");
    for (k = 1; k < 8; k++) {
        v[k] = food[k] * 40 / size[k] + 20;
        if (k == civ) {
            snprintf(buf, sizeof buf, "%d.%d children", v[k] / 10, v[k] % 10);
            text(104, y, 15, buf);
        }
        v[k] /= 10;
    }
    rank(v, y, "");
    y += 12;
    shadow(8, y, 15, "Military Service:");
    for (k = 1; k < 8; k++) {
        v[k] = S->unit_count[k] * 10 / size[k];
        if (k == civ) {
            snprintf(buf, sizeof buf, "%d years", v[k]);
            text(104, y, 15, buf);
        }
    }
    rank(v, y, " yrs");
    y += 12;
    shadow(8, y, 15, "Annual Income:");
    for (k = 1; k < 8; k++) {
        if (30000 / (S->advances_known[k] + 1) > gnp[k])
            v[k] = gnp[k] * S->advances_known[k] / size[k];
        else
            v[k] = gnp[k] / size[k] * S->advances_known[k];
        if (k == civ) {
            snprintf(buf, sizeof buf, "%d$ per capita", v[k]);
            text(104, y, 15, buf);
        }
    }
    rank(v, y, "$");
    y += 12;
    shadow(8, y, 15, "Productivity:");
    for (k = 1; k < 8; k++) {
        v[k] = prod[k] * 10 / size[k];
        if (k == civ) {
            snprintf(buf, sizeof buf, "%d", v[k]);
            text(104, y, 15, buf);
        }
    }
    rank(v, y, "");
    gfx_wait_key();
    gfx_cursor(1);
    memcpy(screen, under, sizeof under);
    gfx_present();
}

/* o_20_0c98's screen: the citizens (luxuries at 40%), wonders, spaceship and the rest. */
static int score_row = 8;                    /* g_50b8: rows close up when there are many */
static int sx, sy;                           /* g_c17c, g_c1dc */

static void score_citizen(int n)                                  /* o_20_1360 */
{
    sx += 8;
    if (sx > 292) {
        sy += score_row;
        sx = ((sy >> 1) & 4) + 16;
    }
    sprite_draw(spr(&sp257, n * 8, 128, 8, 16), sx, sy);
}

void score_screen_original(void)
{
    int civ = S->human_civ, c, i, n, citizens, wonders, score, y, x, w, top, tax, ship, peace;
    char buf[128];

    gfx_cursor(0);
    memcpy(under, screen, sizeof under);
    for (;;) {
        heading("CIVILIZATION SCORE", 3);
        if (S->unk_4c88 & 0x100) {
            centred(160, 100, 15, "SCORING COMPLETED");
            end();
            return;
        }
        sx = 999;
        sy = 26;
        citizens = 0;
        tax = S->tax_rate[civ];
        S->tax_rate[civ] = (int16_t)(6 - S->science_rate[civ]);
        for (c = 0; c < MAX_CITIES; c++) {
            const City *ct = &S->cities[c];
            if (ct->status == NONE8 || (int8_t)ct->owner != civ)
                continue;
            city_process(c, -1);
            for (i = 0; i < calc.happy; i++)
                score_citizen(i & 1);
            n = (int8_t)ct->size - calc.happy - calc.unhappy - calc.specialists;
            for (i = 0; i < n; i++)
                score_citizen((i & 1) + 2);
            for (i = 0; i < calc.unhappy; i++)
                score_citizen((i & 1) + 4);
            for (i = 0; i < calc.specialists; i++)
                score_citizen(city_specialist(i) + 5);
            citizens += (int8_t)ct->size + calc.happy - calc.unhappy;
        }
        S->tax_rate[civ] = (int16_t)tax;
        snprintf(buf, sizeof buf, "%s Citizens (%d)", S->civ_adjectives[civ], citizens);
        shadow(16, 26, 15, buf);
        y = top = sy + 16;
        x = 999;
        wonders = 0;
        for (w = 1; w <= NUM_WONDERS; w++) {
            c = S->wonder_city[w];
            if (c == -1 || c == 128 || (int8_t)S->cities[c].owner != civ)
                continue;
            x += 152;
            if (x > 300) {
                y += clamp(score_row + 2, 8, 99);
                x = 16;
            }
            building_icon(FIRST_WONDER + w - 1, x, y - 2);
            text(x + 20, y, 11, building_types[FIRST_WONDER + w - 1].name);
            wonders += 20;
        }
        snprintf(buf, sizeof buf, "%s Achievements (%d)", S->civ_adjectives[civ], wonders);
        shadow(16, top, 15, buf);
        score = citizens + wonders;
        y += 10;
        if (S->unk_ee0c[civ] != 0 && S->unk_4c84 != 0) {
            ship = S->unk_4c84 * S->unk_ee0c[civ] / 2;
            if (ship < 0)
                ship = 0;
            snprintf(buf, sizeof buf, "SpaceShip: (%d", ship);
            if (S->unk_ed30[civ] == S->year)
                score += ship;
            else
                strcat(buf, " possible");
            strcat(buf, ")");
            shadow(16, y, 15, buf);
            y += 10;
        }
        buf[0] = 0;
        if (S->pollution_count) {
            snprintf(buf, sizeof buf, "Pollution: (%d)   ", -10 * S->pollution_count);
            score -= S->pollution_count * 10;
        }
        if (S->year > 0 && S->peace_turns) {
            peace = clamp(S->peace_turns * 3, 0, 100);
            snprintf(buf + strlen(buf), sizeof buf - strlen(buf), "Peace: (+%d) ", peace);
            score += peace;
        }
        if (S->future_tech_count) {
            snprintf(buf + strlen(buf), sizeof buf - strlen(buf), "Future Tech: (+%d)", S->future_tech_count * 5);
            score += S->future_tech_count * 5;
        }
        if (buf[0] && y < 184) {
            shadow(16, y, 15, buf);
            y += 10;
        }
        if (retired)
            score = (550 - S->turn) * 2 + S->unk_e214 * 100 + 400;
        if (score < 0)
            score = 0;
        S->score[0] = (int16_t)score;           /* (the original adds it up in score[0]) */
        snprintf(buf, sizeof buf, "Total Score: %d", score);
        if (y < 186) {
            shadow(16, y, 15, buf);
            break;
        }
        score_row--;                         /* (too long: again, closer) */
    }
    gfx_fill(35, 192, 250, 7, 8);
    if (score > 0)
        gfx_fill(35, 192, clamp(score / 4, 0, 250), 7, 11);
    gfx_wait_key();
    gfx_cursor(1);
    redraw_map();
}

/* o_12_0000(1): the whole world, a 4 x 4 picture of each known square (SP299), cities and
 * units in their owners' colours; centred on the capital and on the human's lands. */
void world_map_original(void)
{
    int h = S->human_civ, x, y, dx, dy, top = 999, bottom = 0, u, c, px, py, o, t;

    gfx_cursor(0);
    memcpy(under, screen, sizeof under);
    gfx_fill(0, 0, 320, 200, 0);
    dx = 40 - S->capital_x[h];
    if (show_all) {
        dy = 0;
        dx = 0;
    } else {
        for (u = 0; u < MAX_UNITS; u++) {
            if (S->units[h][u].type == NONE8)
                continue;
            if (top > S->units[h][u].y)
                top = S->units[h][u].y;
            if (bottom < S->units[h][u].y)
                bottom = S->units[h][u].y;
        }
        for (c = 0; c < MAX_CITIES; c++) {
            if ((int8_t)S->cities[c].owner != h)
                continue;
            if (top > S->cities[c].y)
                top = S->cities[c].y;
            if (bottom < S->cities[c].y)
                bottom = S->cities[c].y;
        }
        for (x = 0; x < MAP_W; x++)
            if ((S->visibility[x][0] | S->visibility[x][49]) & (1 << h)) {
                top = 0;
                bottom = 49;
            }
        dy = 25 - (bottom + top) / 2;
    }
    for (y = 0; y < MAP_H; y++)
        for (x = 0; x < MAP_W; x++) {
            if (!show_all && (!(S->visibility[x][y] & (1 << h)) || !map_in_bounds(0, y + dy)))
                continue;
            px = wrap_x(x + dx) * 4;
            py = (y + dy) * 4;
            t = map_terrain(x, y);
            gfx_blit(&sp299, t * 4 + 160, ((x + y) & 1) * 4 + 111, 4, 4, px, py, -1);
            if (map_improvements(x, y) & IMP_CITY) {
                c = city_at(x, y);
                if (c >= 0 && (S->cities[c].visible_size != 0 || show_all || (int8_t)S->cities[c].owner == h))
                    gfx_fill(px, py, 4, 4, civ_colour[map_unit_layer_owner(x, y)]);
                continue;
            }
            o = map_unit_owner(x, y);
            if (o == -1)
                continue;
            if (o != h && !show_all) {
                u = unit_at(x, y);
                if (u < 0 || !(S->units[o][u].visible & (1 << h)))
                    continue;
            }
            gfx_fill(px + 1, py + 1, 3, 3, 0);
            gfx_fill(px, py, 3, 3, civ_colour[o]);
        }
    gfx_wait_key();
    gfx_cursor(1);
    memcpy(screen, under, sizeof under);
    gfx_present();
}

/* o_08_0066: the Civilopedia's topics of a category (-1 all) in alphabetical columns, from
 * entry `first` on; returns -1 for MORE, -2 to leave, else shows the article picked. */
static const char *const concepts[21] = {
    "Veteran Units", "Fortify", "Irrigation", "Mining", "Roads", "RailRoads", "Anarchy", "Despotism",
    "Monarchy", "Communism", "Republic", "Democracy", "Fortress", "Pillage", "Disband", "Pollution",
    "Sentry", "Luxuries", "Taxes", "Science", "Trade Routes",
};

static int pedia_more;                           /* g_678a */

static int lower_cmp(const char *a, const char *b)                 /* (MSC stricmp) */
{
    int x, y;

    for (;; a++, b++) {
        x = (unsigned char)*a;
        y = (unsigned char)*b;
        if (x >= 'A' && x <= 'Z')
            x += 32;
        if (y >= 'A' && y <= 'Z')
            y += 32;
        if (x != y || !x)
            return x - y;
    }
}

static int pedia_page(int first, int cat)
{
    static int item[200], kind[200];
    char last[32] = "a", best[32], buf[32];
    const char *name;
    int w = cat > 0 ? 150 : 100, x = 10, y = 16, n = 0, i, k, bi, bk, col, row, idx;

    gfx_fill(0, 0, 320, 200, 14);
    gfx_fill(60, 2, 200, 9, 15);
    centred(161, 4, 0, "ENCYCLOPAEDIA of CIVILIZATION");
    centred(160, 3, 10, "ENCYCLOPAEDIA of CIVILIZATION");
    text(286, 4, 12, "EXIT");
    if (cat == -1)
        text(8, 4, 12, "MORE");
    gfx_fill(2, 14, 316, 184, 15);
    for (;;) {
        strcpy(best, "zzz");
        bi = bk = -1;
        for (k = 0; k < 5; k++) {
            int count = k == 0 ? 67 : k == 1 ? 46 : k == 2 ? 28 : k == 3 ? 12 : 21;
            if (cat != -1 && cat != k)
                continue;
            for (i = k == 1 ? 1 : 0; i < count; i++) {
                name = k == 0 ? advance_types[i].name : k == 1 ? building_types[i].name :
                       k == 2 ? S->unit_type_names[i] : k == 3 ? terrain_types[i].name : concepts[i];
                if (lower_cmp(name, last) > 0 && lower_cmp(name, best) < 0) {
                    bi = i;
                    bk = k;
                    snprintf(best, sizeof best, "%s", name);
                }
            }
        }
        if (bk == -1)
            break;
        if (n >= first) {
            snprintf(buf, sizeof buf, "%s", best);
            shorten_in(0, buf, w);
            text(x, y, 0, buf);
            item[n - first] = bi;
            kind[n - first] = bk;
            y += 7;
            if (y > 192) {
                x += w;
                y = 16;
            }
        }
        n++;
        strcpy(last, best);
        if (x >= 300)
            break;
    }
    pedia_more = x >= 300;
    gfx_present();
    k = gfx_wait_key();
    if (k == KEY_MOUSE && mouse_button == 1) {
        col = (mouse_x - 10) / w;
        row = mouse_y >= 16 ? (mouse_y - 16) / 7 : -1;
    } else {
        col = 2;
        row = -1;
    }
    idx = col * 26 + row;
    if (row >= 0) {
        if (n - first <= idx)
            return 0;
        article_show(kind[idx], item[idx], kind[idx] == 4 ? concepts[item[idx]] : NULL, rnd(2));
        return idx;
    }
    return col == 0 ? -1 : -2;
}

/* o_08_0000 */
void civilopedia_original(int cat)
{
    int first = 0, r;

    do {
        r = pedia_page(first, cat);
        if (r == -1)
            first = pedia_more ? first + 78 : 0;
    } while (r != -2);
    redraw_map();
}
