#include <stdio.h>
#include <string.h>
#include "game.h"
#include "civ.h"
#include "city.h"
#include "replay.h"
#include "science.h"
#include "ui.h"
#include "meeting.h"
#include "view.h"
#include "util.h"
#include "ai.h"

#define DIP(a, b) game.s.diplomacy[a][b]

static void append(char *buf, size_t size, const char *s)
{
    size_t n = strlen(buf);

    snprintf(buf + n, size - n, "%s", s);
}

/* f_0ad5_1d13 */
void give_advance(int civ, int adv, int from)
{
    SaveData *s = &game.s;
    int human = s->human_civ, i, n, gl, owner;
    char msg[512];

    if (civ_has_advance(civ, adv))
        return;
    if (adv != ADVANCE_FUTURE_TECH) {
        s->advances[civ][adv >> 4] |= (uint16_t)(1 << (adv % 16));
        s->advance_source[civ][adv] = (uint8_t)from;
    }
    if (s->turn == 0)
        return;
    s->advances_known[civ]++;

    if (civ == human || (DIP(human, civ) & DIP_EMBASSY)) {
        msg[0] = 0;
        append(msg, sizeof msg, s->civ_names[civ]);
        append(msg, sizeof msg, civ == from || from == 0 ? " develop\n" : " acquire\n");
        append(msg, sizeof msg, advance_types[adv].name);
        if (civ != from && from != 0) {
            append(msg, sizeof msg, " from\nthe ");
            append(msg, sizeof msg, from == -2 ? "Great Library" : s->civ_names[from]);
        }
        append(msg, sizeof msg, "!\n ");
        if (civ != human) {
            for (i = 0; i < 24; i++)
                if (building_types[i].prereq == adv) {
                    append(msg, sizeof msg, building_types[i].name);
                    append(msg, sizeof msg, "\n ");
                }
            for (i = 0; i < NUM_UNIT_TYPES; i++)
                if (s->unit_types[i].prereq == adv) {
                    append(msg, sizeof msg, s->unit_type_names[i]);
                    append(msg, sizeof msg, "\n ");
                }
        }
        if ((civ != human || from == -2) && message_lock == -1)
            ui_message(msg, civ == human ? 6 : 0);
    } else if (!(s->first_discoverer[adv] & 7) && civ_has_advance(civ, ADV_NAVIGATION) &&
               !civ_has_advance(human, adv) && rnd(3) == 0) {
        /* A rival with ships brags about its discovery through an emissary. */
        diplomacy_audience(civ, -1, 0);
    }

    if (civ == human) {
        if (adv != ADVANCE_FUTURE_TECH)
            ui_advance_screen(adv, rnd(2));       /* (o_08_062a's border: f_0860_25e1) */
        if (adv == ADV_INVENTION) {
            ui_government_picture();
            redraw_map();
        }
        if (message_lock == -1) {
            redraw_map();
            if (adv == ADV_MONARCHY)
                ui_help_message("*MONARCHY");
        }
    } else if (s->difficulty == 0 && adv == ADV_WRITING && !(DIP(human, civ) & DIP_EMBASSY) && message_lock == -1) {
        /* On Chieftain, a rival that learns Writing opens an embassy with you. */
        DIP(human, civ) |= DIP_EMBASSY;
        snprintf(msg, sizeof msg, "%s agree to\nan exchange of ambassadors.\n", s->civ_names[civ]);
        ui_message(msg, 1);
        ui_intelligence_report();
    }

    if (adv == ADV_GUNPOWDER || adv == ADV_COMBUSTION) {
        for (i = 0; i < MAX_CITIES; i++)
            if (s->cities[i].status != NONE8 && (int8_t)s->cities[i].owner == civ)
                s->cities[i].buildings &= ~2u;             /* Barracks */
        if (civ == human) {
            snprintf(msg, sizeof msg, "Development of %s\nmakes existing Barracks\nobsolete.\n", advance_types[adv].name);
            ui_message(msg, 3);
        }
    }

    if (!(s->first_discoverer[adv] & 7)) {
        s->first_discoverer[adv] |= civ;
        replay_log(5, 2, civ, adv, 0, 0);
        for (i = 1; i <= NUM_WONDERS; i++) {
            int c = s->wonder_city[i];
            if (wonder_obsolete[i] != adv || c < 0 || c >= MAX_CITIES || (int8_t)s->cities[c].owner != human)
                continue;
            snprintf(msg, sizeof msg, "%s cancels\nthe effect of\n%s.\n", advance_types[adv].name,
                     building_types[FIRST_WONDER + i - 1].name);
            newspaper(-1, msg);
        }
    }

    /* The Great Library hands out anything at least two other civilizations know.  The
     * original checks city[owner].owner here (a slip for owner); kept as is. */
    gl = s->wonder_city[WONDER_GREAT_LIBRARY];
    if (gl == -1 || gl >= MAX_CITIES)
        return;
    owner = (int8_t)s->cities[gl].owner;
    if (owner < 0 || owner >= MAX_CITIES || civ_has_advance((int8_t)s->cities[owner].owner, adv) ||
        !civ_has_wonder(owner, WONDER_GREAT_LIBRARY))
        return;
    for (n = 0, i = 1; i < MAX_CIVS; i++)
        if (civ_has_advance(i, adv))
            n++;
    if (n >= 2)
        give_advance(owner, adv, -2);
}

/* f_0ad5_22f5 */
int advance_value(int civ, int adv)
{
    return civ_types[game.s.civ_type[civ]].science_bias * advance_ai[adv][1] + advance_ai[adv][0];
}

static int can_research(int civ, int adv)
{
    return !civ_has_advance(civ, adv) && civ_has_advance(civ, advance_types[adv].prereq[0]) &&
           civ_has_advance(civ, advance_types[adv].prereq[1]);
}

/* f_0ad5_1569: research has paid off.  Computer players learn a (weighted random) new
 * advance at once; the human learns what was being researched and picks the next goal.
 * `from` is passed on to give_advance().  Returns the advance (or -1). */
int research_complete(int civ, int from)
{
    SaveData *s = &game.s;
    int human = s->human_civ, adv, r, best = -1, best_r = -1, n, k, again = 0;
    int list[NUM_ADVANCES];
    char msg[1024];

    if (civ == 0)
        return -1;
    for (adv = 0; adv < NUM_ADVANCES; adv++) {
        if (!can_research(civ, adv))
            continue;
        r = rnd(advance_value(civ, adv) * 4);
        if (r <= best_r || (adv == s->human_research && civ == human))
            continue;
        best_r = r;
        best = adv;
    }
    if (civ != human || s->turn == 0) {
        /* On Chieftain a rival sometimes holds back what the human does not know yet. */
        if (s->turn != 0 && s->difficulty == 0 && !civ_has_advance(human, best) && rnd(2) != 0)
            return -1;
        if (best_r != -1) {
            give_advance(civ, best, from);
            ai_rechoose_production(civ, -1);
        }
        return best;
    }

    if (s->human_research != -1) {
        int learnt = s->human_research;
        snprintf(msg, sizeof msg, "%s%s\ndiscover the secret\nof %s", s->civ_adjectives[civ],
                 civ_has_advance(human, ADV_INVENTION) ? " scientists" : " wise men", advance_types[learnt].name);
        if (learnt == ADVANCE_FUTURE_TECH) {
            char tmp[16];
            s->future_tech_count++;
            snprintf(tmp, sizeof tmp, " %d", s->future_tech_count);
            strncat(msg, tmp, sizeof msg - strlen(msg) - 1);
        }
        strncat(msg, "!\n", sizeof msg - strlen(msg) - 1);
        ui_sound(civ_types[s->civ_type[civ]].music);    /* (the civ's tune, f_01a7_046e) */
        if (learnt != ADVANCE_FUTURE_TECH && (s->unk_19c0 & 8))
            ui_discovery(civ, learnt, msg);              /* (with Animations: DISCOVRn.PIC) */
        else
            newspaper(-1, msg);
        give_advance(civ, learnt, from);
        s->human_research = -1;
    }
    if (s->advances_known[civ] <= 1)
        ui_help_message("*FIRSTCIV");
    if ((s->unk_19c0 & 1) && best != -1 && s->difficulty == 0)
        ui_research_advice(best);

    for (;;) {
        snprintf(msg, sizeof msg, "Which discovery should our\n%s be pursuing, sire?\nPick one...\n ",
                 civ_has_advance(human, ADV_INVENTION) ? "scientists" : "wise men");
        n = 0;
        for (adv = 0; adv < NUM_ADVANCES; adv++) {
            if (!can_research(civ, adv))
                continue;
            if (!again) {
                /* Above Chieftain some choices stay hidden. */
                if (n > 1 && (adv - s->advances_known[civ]) % 3 == 0 && s->difficulty != 0)
                    continue;
            } else if (list[n] != adv) {
                continue;
            }
            strncat(msg, advance_types[adv].name, sizeof msg - strlen(msg) - 1);
            strncat(msg, "\n ", sizeof msg - strlen(msg) - 1);
            list[n++] = adv;
        }
        if (n == 0) {
            char tmp[48];
            snprintf(tmp, sizeof tmp, " Future Technology %d\n", s->future_tech_count + 1);
            strncat(msg, tmp, sizeof msg - strlen(msg) - 1);
            ui_message(msg, 6);
            s->human_research = ADVANCE_FUTURE_TECH;
            return 0;
        }
        k = ui_choose(msg, 0, 80, 64, 6, 1);      /* (the Science Advisor; F1 for help) */
        if (k == -1)
            k = 0;
        adv = list[k];
        if (!ui_help_requested())
            break;
        ui_encyclopedia_advance(adv);
        again = 1;
    }
    s->human_research = (int16_t)adv;
    return adv;
}
