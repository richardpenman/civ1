/* Caravans and trade routes (original seg 1439). */
#include <stdio.h>
#include "game.h"
#include "civ.h"
#include "city.h"
#include "map.h"
#include "unit.h"
#include "util.h"
#include "ui.h"
#include "turn.h"

/* f_1439_0940: a caravan reaches `city`.  It pays a one-off bonus in gold and science,
 * disappears, and opens a trade route in place of its home city's weakest one. */
int caravan_arrive(int civ, int unit, int city)
{
    City *dest = &game.s.cities[city];
    int home = (int8_t)game.s.units[civ][unit].home_city;
    City *src = &game.s.cities[home];
    int bonus, value, best, weakest, k, o;
    char msg[192];

    bonus = ((int8_t)dest->trade + (int8_t)src->trade) * (map_distance(dest->x, dest->y, src->x, src->y) + 10) / 24;
    /* sic: the original asks for the continent at (x, x) of the home city */
    if (map_continent(dest->x, dest->y) == map_continent(src->x, src->x))
        bonus /= 2;
    if ((int8_t)dest->owner == civ)
        bonus /= 2;
    if (civ_has_advance((int8_t)dest->owner, ADV_RAILROAD))
        bonus -= bonus / 3;
    if (civ_has_advance((int8_t)dest->owner, ADV_FLIGHT))
        bonus -= bonus / 3;
    game.s.treasury[civ] += bonus;
    game.s.research_progress[civ] += bonus;
    if (civ == game.s.human_civ) {
        snprintf(msg, sizeof msg, "%s caravan from %s\narrives in %s\nTrade route established\nRevenue: $%d.\n",
                 trade_good_names[unit & 7], game.s.city_names[src->name], game.s.city_names[dest->name], bonus);
        ui_message(msg, 0);
        status_report();
    }
    delete_unit(civ, unit);

    best = (int8_t)dest->trade;
    if ((int8_t)dest->owner != civ)
        best <<= 1;
    weakest = -1;
    for (k = 0; k < 3; k++) {
        o = src->trade_routes[k];
        if (o == city)
            return 1;
        if (o == NONE8) {
            value = -1;
        } else {
            value = (int8_t)game.s.cities[o].trade;
            if ((int8_t)game.s.cities[o].owner != civ)
                value <<= 1;
        }
        if (value < best) {
            best = value;
            weakest = k;
        }
    }
    if (weakest != -1)
        src->trade_routes[weakest] = (uint8_t)city;
    return 1;
}
