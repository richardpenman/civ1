/* shot: render the main screen of a saved game to a .bmp (a check of the drawing code). */
#include <stdio.h>
#include <stdlib.h>
#include "gfx.h"
#include "font.h"
#include "assets.h"
#include "mapview.h"
#include "../save.h"
#include "../move.h"
#include "../unit.h"

int main(int argc, char **argv)
{
    int h, u = 0, i;
    char path[512];

    if (argc < 4) {
        fprintf(stderr, "usage: shot GAME.SVE GAME.MAP out.bmp [fonts.bmp]\n");
        return 2;
    }
    if (load_game(argv[1], argv[2], &game))
        return 1;
    gfx_init(1, 1);
    if (getenv("SHOW_ALL"))
        show_all = 1;
    snprintf(path, sizeof path, "%s/FONTS.CV", data_dir);
    if (font_load(path) || assets_load())
        return 1;
    gfx_set_palette(game_palette);
    h = game.s.human_civ;
    for (i = 0; i < MAX_UNITS; i++)
        if (game.s.units[h][i].type != 0xFF) {
            u = i;
            break;
        }
    view_x = game.s.units[h][u].x - 7;
    view_y = game.s.units[h][u].y - 6;
    if (view_y < 0) view_y = 0;
    if (view_y > 38) view_y = 38;
    view_x = (view_x + 80) % 80;
    draw_menu_bar();
    draw_view(h, view_x, view_y, 0);
    draw_status_panel();
    draw_unit_panel(h, u);
    gfx_save_bmp(argv[3]);
    if (argc > 4) {
        gfx_fill(0, 0, 320, 200, 0);
        for (i = 0; i < 9; i++)
            draw_text(i, 2, 2 + i * 22, 15, "The quick brown fox 0123 #$ ABC");
        gfx_save_bmp(argv[4]);
    }
    return 0;
}
