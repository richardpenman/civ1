/* The human player and a rival's leader talk (original overlay 6). */
#include <stdio.h>
#include <string.h>
#include "game.h"
#include "civ.h"
#include "map.h"
#include "unit.h"
#include "util.h"
#include "ai.h"
#include "diplomacy.h"
#include "science.h"
#include "move.h"
#include "meeting.h"
#include "ui.h"
#include "view.h"

#define DIP(a, b) game.s.diplomacy[a][b]
#define DIPB(a, b) (*(uint8_t *)&game.s.diplomacy[a][b])

int audience_open;               /* g_e724: the rival's envoy is on screen */

/* o_06_1874: the envoy's greeting (one of four). */
static void greeting(int civ)
{
    ui_envoy_greeting(civ, rnd(4));
}

/* o_06_16cd: the envoy asks for an audience.  0 (or the caller's message_lock) if the
 * human receives him, -1 if not. */
int diplomacy_audience(int civ, int x, int y)
{
    SaveData *s = &game.s;
    int r;

    if (audience_open)
        return message_lock;
    audience_open = 1;
    if (message_lock == 0) {
        if (x != -1)
            view_center(s->human_civ, x - 8, y - 2);
        for (;;) {
            r = ui_receive_envoy(civ, (DIP(s->human_civ, civ) & DIP_EMBASSY) != 0);
            message_lock = -r;
            if (message_lock != -2)
                break;
            ui_intelligence_report_for(civ);     /* o_14_115e */
        }
    }
    if (message_lock == -1)
        return -1;
    ui_diplomacy_screen(civ);                    /* o_06_1d2b and the palette */
    greeting(civ);
    ui_envoy_says_greeting(civ);                 /* o_06_251d */
    return message_lock;
}

/* o_06_16ac: the envoy leaves. */
void diplomacy_audience_end(void)
{
    ui_diplomacy_screen_end();                   /* o_06_233c */
    redraw_map();
    message_lock = 0;
    audience_open = 0;
}

/* o_06_1a99: a peace treaty; on Chieftain the rival's units near the human's cities go. */
static void sign_peace(int civ)
{
    SaveData *s = &game.s;
    int u, c, d;

    diplomacy_set(s->human_civ, civ, DIP_PEACE);
    s->unk_e204[civ] = (int16_t)(s->turn + 16);
    if (s->difficulty != 0)
        return;
    for (u = 0; u < MAX_UNITS; u++) {
        Unit *p = &s->units[civ][u];
        if (p->type == NONE8)
            continue;
        c = nearest_city(p->x, p->y, &d);
        if ((int8_t)s->cities[c].owner == s->human_civ && d <= 2)
            delete_unit(civ, u);
    }
}

/* o_06_1a3f */
static void treaty_text(int civ)
{
    ui_envoy_says(civ, "*TREATY", game.s.civ_adjectives[civ], game.s.civ_adjectives[game.s.human_civ]);
}

/* o_06_0000: the meeting.  `by_diplomat`: the human's diplomat came (1), else the units
 * met (0). */
void diplomacy_meeting(int civ, int x, int y, int by_diplomat)
{
    SaveData *s = &game.s;
    int human = s->human_civ, last, hostile = 0, initial_hostile, fear = 0, demand = 0, mood = 1, gang;
    int c, d, o, a, want, give, best, ai_n, human_n, enemy, peace_wish, betrayed, cash, ch, f, k, n, list[8];
    char buf[160], amount[16];

    map_refresh_known(x, y);
    square_redraw(x, y);
    last = s->unk_e204[civ];
    ui_female_leader(s->civ_type[civ] == 14);
    if (!by_diplomat)
        s->unk_e204[civ] = s->turn;
    audience_open = 0;
    message_lock = by_diplomat;

    /* What each side has on the continents they share. */
    for (c = 1; c < 15; c++) {
        int thr = s->city_count[civ] > 1 ? 1 : 0;
        int16_t *my_cities = &s->unk_6b16[civ][0], *their_cities = &s->unk_6b16[human][0];
        int16_t *my_def = &s->unk_6c2c[civ][0], *their_def = &s->unk_6c2c[human][0];
        int16_t *my_att = &s->unk_8f9a[civ][0], *their_att = &s->unk_8f9a[human][0];
        if (my_cities[c] > thr && their_att[c] > my_def[c]) {
            fear += (int16_t)(their_att[c] * (s->difficulty == 0 ? 4 : 2)) / (my_def[c] + 1);
            continue;
        }
        if (their_def[c] != 0 && my_def[c] != 0)
            hostile = 1;
        if (their_cities[c] != 0) {
            if (my_cities[c] > 1) {
                demand += my_att[c] - their_att[c];
                continue;
            }
            demand += my_att[c];
        } else if (my_cities[c] != 0)
            demand += (my_def[c] - their_att[c]) / 2;
        else
            demand += (my_att[c] - their_att[c]) / 4;
    }
    gang = s->civ_rank[human] == 7 && s->city_count[human] > 4 && s->city_count[civ] > 1 &&
           s->units_active[human][UNIT_NUCLEAR] == 0 && s->turn > 200;
    if ((gang || ((DIP(civ, human) & 8) && s->military_power[civ] * 3 > s->military_power[human])) &&
        civ_nuclear_count(human) == 0) {
        hostile = 1;
        demand = clamp(demand, s->civ_rank[civ] * 10, 9999);
        fear = 0;
    }
    if (s->units_active[civ][UNIT_NUCLEAR] != 0)
        DIPB(civ, human) |= 0x80;
    if (s->units_active[civ][UNIT_NUCLEAR] != 0 && civ_nuclear_count(human) == 0) {
        hostile = 1;
        demand = clamp(demand, 100, 9999);
    }
    if (s->units_active[human][UNIT_NUCLEAR] != 0) {
        if (civ_nuclear_count(civ) == 0)
            fear = 4;
        else
            demand = (int16_t)(s->units_active[civ][UNIT_NUCLEAR] * (demand / 2)) / s->units_active[human][UNIT_NUCLEAR];
    }
    if (fear) {
        hostile = 0;
        mood = 0;
    }
    if (civ_has_wonder(human, WONDER_GREAT_WALL) || civ_has_wonder(human, WONDER_UNITED_NATIONS)) {
        gang = 0;
        hostile = 0;
        if (last == -2)
            last = -1;
    }
    if (DIP(civ, human) & 8)
        demand <<= 1;
    else if (last == -2)
        demand /= 4;
    demand = clamp((int16_t)(demand * (s->difficulty + 1)) / 32, 0, 20) * 50;
    if (demand > s->treasury[human] && demand <= s->treasury[human] * 2 && s->treasury[human] >= 50)
        demand = s->treasury[human] / 50 * 50;
    if (s->units_active[civ][UNIT_NUCLEAR] != 0) {
        if (!(DIP(civ, human) & 0x80) && s->treasury[human] < demand)
            demand = s->treasury[human] / 50 * 50;
        DIPB(civ, human) |= 0x80;
    }
    if (demand == 0 || s->military_power[civ] * 3 < s->military_power[human])
        hostile = 0;
    if (hostile)
        mood = 2;
    if (!hostile && (DIP(civ, human) & 4))
        mood = 0;
    initial_hostile = hostile;

    /* Wars with others make a civilization peaceable. */
    peace_wish = (DIP(civ, human) & 8) ? -2 : 0;
    enemy = -1;
    for (o = 1; o < MAX_CIVS; o++) {
        if (o == human || (DIP(civ, o) & 3) != 1)
            continue;
        if (s->military_power[civ] < s->military_power[o] * 4)
            peace_wish++;
        if (s->military_power[o] > s->military_power[civ])
            peace_wish++;
        if (DIP(human, o) & DIP_PEACE)
            enemy = o;
    }
    peace_wish -= civ_types[s->civ_type[civ]].aggression;
    if (s->military_power[human] < s->military_power[civ])
        peace_wish--;
    if ((!(DIP(civ, human) & DIP_PEACE) ? 1 : 0) < peace_wish && !gang) {
        hostile = 0;
        if (rnd(2))
            demand = 0;
    }
    if (s->government[civ] >= GOV_REPUBLIC && hostile && !(DIP(civ, human) & 8)) {
        if (s->military_power[human] * 3 > s->military_power[civ] * 2 && !gang)
            hostile = 0;
        else
            set_government(civ, GOV_MONARCHY);  /* a senate would not allow the war */
    }
    betrayed = 0;
    f = (uint8_t)DIP(civ, human);
    if ((f & DIP_PEACE) && hostile && (s->military_power[civ] < s->military_power[human] || (f & 8))) {
        hostile = 0;
        betrayed = 1;
        mood = 0;
    }

    /* An exchange of knowledge. */
    for (;;) {
        want = give = -1;
        best = 0;
        ai_n = human_n = 0;
        for (a = 0; a < NUM_ADVANCES; a++) {
            if (civ_has_advance(human, a))
                human_n++;
            if (civ_has_advance(civ, a))
                ai_n++;
            if (!civ_has_advance(human, a) && civ_has_advance(civ, a))
                give = a;
            if (civ_has_advance(human, a) && !civ_has_advance(civ, a) && advance_value(civ, a) >= best) {
                want = a;
                best = advance_value(civ, a);
            }
        }
        if (((DIP(civ, human) & 0x20) || hostile) && ai_n > 6)
            break;
        if (human_n < ai_n || give == -1 || diplomacy_audience(civ, x, y) == -1)
            break;
        if (ui_envoy_asks(civ, "*EXCHANGE", advance_types[give].name, "", 1) != 1)
            break;
        steal_advance(civ, human);
        steal_advance(human, civ);
        hostile = 0;
    }

    if (!hostile && !by_diplomat && (DIP(civ, human) & DIP_PEACE) && fear > 2)
        goto leave;
    if (!(s->rules_flags & 1)) {
        hostile = 1;
        mood = 0;
        fear = 0;
        betrayed = 0;
    }
    if (enemy != -1 && diplomacy_audience(civ, x, y) != -1 && mood != 0) {
        /* Break with our enemy! */
        if (ui_envoy_asks(civ, "*CANCEL", hostile ? "demand" : "request", s->civ_names[enemy], hostile ? 2 : 1) == 1) {
            hostile = 0;
            if (rnd(2))
                demand = 0;
            diplomacy_clear(human, enemy, DIP_PEACE);
            DIPB(enemy, human) |= 8;
        }
    }
    if (demand != 0 && mood == 2 && s->treasury[human] > demand && diplomacy_audience(civ, x, y) != -1) {
        snprintf(amount, sizeof amount, "%d", demand);
        if (ui_envoy_asks(civ, "*TRIBUTE", "", amount, 2) == 1) {
            s->treasury[human] -= (int16_t)demand;
            s->treasury[civ] += (int16_t)demand;
            hostile = 0;
        }
        demand = 999;
    }
    if (mood != 0 && want != -1 && demand != 999 && diplomacy_audience(civ, x, y) != -1) {
        if (ui_envoy_asks(civ, "*TAKECIV", advance_types[want].name, "", 2) == 1) {
            give_advance(civ, want, human);
            hostile = 0;
        }
        demand = 999;
    }
    if (hostile && (DIP(civ, human) & DIP_PEACE) && demand > 50) {
        if (diplomacy_audience(civ, x, y) != -1)
            ui_envoy_asks(civ, "*PROVOKE", "", "", 2);
        else
            ui_envoy_message("*PROVOKE");
        diplomacy_clear(human, civ, DIP_PEACE);
    } else if (hostile && demand == 999) {
        if (diplomacy_audience(civ, x, y) != -1) {
            ui_envoy_asks(civ, "*REJECT", "", "", 2);
            diplomacy_clear(human, civ, DIP_PEACE);
        }
    } else if (hostile && !((DIP(civ, human) & DIP_CONTACT) && !by_diplomat)) {
        if (diplomacy_audience(civ, x, y) != -1)
            ui_envoy_asks(civ, "*RID", "", "", 2);
        else
            ui_envoy_message("*RID");
        diplomacy_clear(human, civ, DIP_PEACE);
    }

    if (!hostile && !((DIP(civ, human) & DIP_PEACE) && !by_diplomat) && diplomacy_audience(civ, x, y) != -1) {
        /* Peace? */
        if (ui_envoy_asks(civ, "*PEACE", "", "", 0) == 1) {
            treaty_text(civ);
            sign_peace(civ);
            s->unk_e204[civ] = (int16_t)(s->turn + 16);
        } else if (s->government[human] >= GOV_REPUBLIC) {
            ui_envoy_asks(civ, "You are overruled by the Senate.\nTreaty signed.\n", "", "", 0);
            sign_peace(civ);
        } else {
            if (DIP(civ, human) & DIP_PEACE)
                diplomacy_clear(human, civ, DIP_PEACE);
            if (fear >= 4 && give != -1 && !(DIP(civ, human) & 10) &&
                ui_envoy_asks(civ, "*GIVECIV", advance_types[give].name, "", 0) == 1) {
                give_advance(human, give, civ);
                treaty_text(civ);
                sign_peace(civ);
                last = s->turn;
            }
            if (!(DIP(civ, human) & 10)) {
                cash = clamp(fear * 2 - 4, 0, s->treasury[civ] / 50) * 50;
                snprintf(amount, sizeof amount, "%d", cash);
                if (cash != 0 && ui_envoy_asks(civ, "*GIVECASH", "", amount, 0) == 1) {
                    treaty_text(civ);
                    sign_peace(civ);
                    s->treasury[human] += (int16_t)cash;
                    s->treasury[civ] -= (int16_t)cash;
                    last = s->turn;
                }
            }
            if (fear != 0 && s->city_count[civ] <= 1 && !(DIP(civ, human) & 10)) {
                snprintf(amount, sizeof amount, "%d", s->treasury[civ]);
                if (ui_envoy_asks(civ, "*GROVEL", "", amount, 0) == 1) {
                    treaty_text(civ);
                    sign_peace(civ);
                    s->treasury[human] += s->treasury[civ];
                    s->treasury[civ] = 0;
                    for (a = 0; a < NUM_ADVANCES; a++)
                        if (!civ_has_advance(human, a) && civ_has_advance(civ, a))
                            give_advance(human, a, civ);
                    last = s->turn;
                }
            }
            if (!(DIP(civ, human) & DIP_PEACE))
                ui_envoy_asks(civ, "*MOBILIZE", "", "", 2);
        }
    }

    ui_female_leader(0);
    if (!hostile && (DIP(civ, human) & DIP_PEACE) && diplomacy_audience(civ, x, y) != -1) {
        if (mood != 0 && demand != 999)
            ui_envoy_asks(civ, "*ATTITUDE", "", "", 1);
        else {
            snprintf(buf, sizeof buf, "We welcome the friendship of the\n%s people and their most\nwise and munificent leader:\n%s %s.\n",
                     s->civ_adjectives[human], leader_titles[s->government[human]], s->leader_names[human]);
            ui_envoy_asks(civ, buf, "", "", 0);
        }
    }
    if ((DIP(civ, human) & DIP_PEACE) && diplomacy_audience(civ, x, y) != -1) {
        for (;;) {
            int disabled = 0;
            if (s->turn - last < 16 || s->government[human] >= GOV_REPUBLIC)
                disabled = 4;
            if (((1 << civ) | (1 << human)) == s->civs_alive)
                disabled |= 2;
            ch = ui_envoy_threat(civ, disabled);     /* *THREAT */
            if (ch == 2) {
                cash = clamp(fear, 0, s->treasury[civ] / 50) * 50;
                if (cash != 0) {
                    snprintf(amount, sizeof amount, "%d", cash);
                    ui_envoy_asks(civ, "*GIVEMORE", "", amount, 0);
                    s->treasury[human] += (int16_t)cash;
                    s->treasury[civ] -= (int16_t)cash;
                    s->unk_e204[civ] = s->turn;
                } else if (initial_hostile) {
                    diplomacy_clear(human, civ, DIP_PEACE);
                    ui_envoy_asks(civ, "*PROVOKE", "", "", 2);
                } else
                    ui_envoy_asks(civ, "We ignore your feeble threats.\n", "", "", 1);
            }
            if (ch != 1)
                break;
            /* Attack someone for us. */
            n = 0;
            strcpy(buf, "You must attack the evil:\n ");
            for (o = 1; o < MAX_CIVS; o++) {
                if (o == human || o == civ || !(s->civs_alive & (1 << o)))
                    continue;
                list[n++] = o;
                strncat(buf, s->civ_names[o], sizeof buf - strlen(buf) - 1);
                strncat(buf, "\n ", sizeof buf - strlen(buf) - 1);
            }
            k = ui_envoy_asks(civ, buf, "", "", 1);
            if (k == -1)
                break;
            o = list[k];
            if (!(DIP(civ, o) & DIP_CONTACT)) {
                ui_envoy_asks(civ, "*NOCONTACT", s->civ_names[o], "", 4);
                break;
            }
            if (!(DIP(civ, o) & DIP_PEACE)) {
                ui_envoy_asks(civ, "*ATWAR", s->civ_names[o], "", 4);
                continue;
            }
            cash = clamp((int16_t)(s->military_power[o] / 2 * (s->treasury[civ] / 100)) / 50, 2, 9999) * 50;
            snprintf(amount, sizeof amount, "%d", cash);
            if (ui_envoy_asks(civ, "*MERCENARY", s->civ_names[o], amount, 4) != 1)
                break;
            if (s->treasury[human] >= cash) {
                s->treasury[civ] += (int16_t)cash;
                s->treasury[human] -= (int16_t)cash;
                /* (only words: no war follows, sic) */
                snprintf(buf, sizeof buf, "%s declare war\non the %s.\n", s->civ_names[civ], s->civ_names[o]);
                ui_envoy_asks(civ, buf, "", "", 4);
            } else
                ui_envoy_asks(civ, "*UNFORTUNATE", "", "", 4);
            break;
        }
    }
    if (betrayed)
        DIP(civ, human) |= 0x100;
    DIPB(civ, human) &= ~0x20;
    DIPB(human, civ) &= ~0x20;
    if (message_lock != -1 && s->city_count[civ] > 4 && s->civ_rank[human] + 1 < s->civ_rank[civ] &&
        !(DIP(human, civ) & DIP_PEACE))
        ui_help_message("*SCHISM");
leave:
    if (message_lock != -1)
        diplomacy_audience_end();
    ui_female_leader(0);
    audience_open = 0;
    message_lock = -1;
    (void)d;
}
