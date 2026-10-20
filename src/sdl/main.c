#define _DEFAULT_SOURCE
/* civ: the game with an SDL screen.  The original's main (seg 01a7): the title menu, a new
 * world or a saved game, then turns until the player quits. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <signal.h>
#include <execinfo.h>
#include <unistd.h>
#include "gfx.h"
#include "font.h"
#include "assets.h"
#include "mapview.h"
#include "dialog.h"
#include "front.h"
#include "palace.h"
#include "article.h"
#include "sound.h"
#include <SDL2/SDL.h>
#include "../game.h"
#include "../save.h"
#include "../util.h"
#include "../move.h"
#include "../view.h"
#include "../turn.h"
#include "../civ.h"
#include "../mapgen.h"
#include "../newgame.h"

#define S (&game.s)

void make_save_dir(void)
{
    mkdir(save_dir, 0777);
}

static int slot_paths(int slot, char *sve, char *map, size_t n)
{
    FILE *f;
    int pass;

    for (pass = 0; pass < 2; pass++) {
        const char *dir = pass == 0 ? save_dir : data_dir;
        snprintf(sve, n, "%s/CIVIL%d.SVE", dir, slot);
        snprintf(map, n, "%s/CIVIL%d.MAP", dir, slot);
        if ((f = fopen(sve, "rb")) != NULL) {
            fclose(f);
            return 1;
        }
    }
    return 0;
}

/* o_11_0103 (reading): " Emperor Genghis Khan, Mongols/560 AD" from the file's header */
void describe_slot(int slot, char *out, size_t n)
{
    static const char *const title[5] = { "Chief", "Lord", "Prince", "King", "Emperor" };
    char sve[512], map[512];
    unsigned char b[256];
    FILE *f;
    int h, year, diff;

    if (!slot_paths(slot, sve, map, sizeof sve) || !(f = fopen(sve, "rb"))) {
        snprintf(out, n, " (EMPTY)");
        return;
    }
    if (fread(b, 1, sizeof b, f) != sizeof b) {
        fclose(f);
        snprintf(out, n, " (EMPTY)");
        return;
    }
    fclose(f);
    h = (b[2] | b[3] << 8) & 7;
    year = (short)(b[8] | b[9] << 8);
    diff = (b[10] | b[11] << 8) % 5;
    snprintf(out, n, " %s %.14s, %.12s/%d %s", title[diff], (char *)b + 16 + h * 14, (char *)b + 128 + h * 12,
             year < 0 ? -year : year, year < 0 ? "BC" : "AD");
}

int slot_exists(int slot)
{
    char sve[512], map[512];

    return slot_paths(slot, sve, map, sizeof sve);
}

int load_slot(int slot)
{
    char sve[512], map[512];

    return slot_paths(slot, sve, map, sizeof sve) && load_game(sve, map, &game) == 0;
}

int choose_load_slot(void);

static int load_menu(void)   /* 1 + the slot loaded, or 0 */
{
    int k = choose_load_slot();

    if (k < 0 || !load_slot(k))
        return 0;
    return 1 + k;
}

/* f_01a7_0488, Customize World: four questions over CUSTOM.PIC (the middle one first) */
static int world_menu(void)
{
    static const char *const q[4] = { "LAND MASS:\n Small\n Normal\n Large\n",
                                      "TEMPERATURE:\n Cool\n Temperate\n Warm\n",
                                      "CLIMATE:\n Arid\n Normal\n Wet\n",
                                      "AGE:\n 3 billion years\n 4 billion years\n 5 billion years\n" };
    int *v[4] = { &world.land, &world.temperature, &world.climate, &world.age }, i, k;
    Picture p;

    gfx_fill(0, 0, 320, 200, 0);
    if (load_picture("CUSTOM.PIC", &p) == 0) {
        if (p.has_palette)
            gfx_set_palette((const uint8_t (*)[3])p.palette);
        gfx_blit(&p, 0, 0, 320, 200, 0, 0, -1);
        pic_free(&p);
    }
    for (i = 0; i < 4; i++) {
        do {
            mouse_x = 210;
            mouse_y = 11 + i * 50;
            dialog_default = 1;
            k = dialog(q[i], 200, 1 + i * 50, 0, DLG_KEEP);
        } while (k < 0);
        *v[i] = k;
    }
    return 1;
}

static void add_tribe(char *menu, size_t n, int i)
{
    char name[24];

    snprintf(name, sizeof name, "%s", civ_types[i].adjective);    /* (o_05_0000: the adjectives) */
    strncat(menu, " ", n - strlen(menu) - 1);
    strncat(menu, name, n - strlen(menu) - 1);
    strncat(menu, "\n", n - strlen(menu) - 1);
}

static void tribe_menu_text(char *menu, size_t n, int rivals)
{
    int i;

    snprintf(menu, n, "Pick your tribe...\n");
    for (i = 1; i <= rivals + 1; i++)
        add_tribe(menu, n, i);
    for (i = 9; i <= rivals + 9; i++)
        add_tribe(menu, n, i);
}

/* f_01a7_0488 (a new world, with the story) and o_05_0000 (difficulty, rivals and tribe
 * beside the portraits of DIFFS.PIC) */
static unsigned startup_seed;

static int start_new_game(int earth, int custom)
{
    static Picture diffs;
    NewGameChoices ch;
    char menu[1024];
    int k, i, rivals;

    fresh_state();
    land_mass = 0;              /* (the original quits after a game, so each world starts afresh) */
    S->world_seed = (uint16_t)startup_seed;
    world.land = world.temperature = world.climate = world.age = 1;
    if (custom && !world_menu())
        return 0;
    earth_map = earth;
    generate_world();
    if (!diffs.pixels)
        load_picture("DIFFS.PIC", &diffs);
    if (diffs.has_palette)
        gfx_set_palette((const uint8_t (*)[3])diffs.palette);
    gfx_blit(&diffs, 0, 0, 320, 200, 0, 0, -1);
    dialog_font = 6;                        /* (the original's font 7) */
    k = dialog("Difficulty Level...\n Chieftain (easiest)\n Warlord\n Prince\n King\n Emperor (toughest)\n",
               160, 35, 0, 0);
    ch.difficulty = k < 0 ? 0 : k;
#define PORTRAIT(x, y) gfx_blit(&diffs, (ch.difficulty & 1) ? 80 : 21, ch.difficulty * 35 + 6, 53, 47, x, y, -1)
    gfx_fill(0, 0, 150, 200, 0);
    PORTRAIT(20, 100);
    gfx_blit(&diffs, 150, 0, 160, 200, 150, 0, -1);
    strcpy(menu, "Level of Competition...\n ");
    for (i = 7; i > 2; i--)
        snprintf(menu + strlen(menu), sizeof menu - strlen(menu), "%d Civilizations\n ", i);
    do
        k = dialog(menu, 160, 35, 0, 0);
    while (k < 0);
    ch.civ_count_choice = k;
    rivals = clamp(6 - k, 2, 6);
    for (i = rivals; i >= 0; i--)
        PORTRAIT(i * 2 + 20, i * 3 + 100);
#undef PORTRAIT
    tribe_menu_text(menu, sizeof menu, rivals);
    gfx_blit(&diffs, 150, 0, 160, 200, 150, 0, -1);
    do
        k = dialog(menu, 160, 35, 0, 0);
    while (k < 0);
    ch.tribe_choice = k;
    new_game(&ch);                          /* (the name is asked in font 7 too) */
    dialog_font = 0;
    return 1;
}

/* The Hall of Fame: the games recorded in FAME.TXT (score, leader, civilization, year,
 * difficulty), best first. */
static void hall_of_fame(void)
{
    char path[512], line[160], text[2048], rows[32][160];
    int scores[32], n = 0, i, j, best;
    FILE *f;

    snprintf(path, sizeof path, "%s/FAME.TXT", save_dir);
    if ((f = fopen(path, "r")) != NULL) {
        while (n < 32 && fgets(line, sizeof line, f)) {
            char *t = strchr(line, '\t');
            line[strcspn(line, "\n")] = 0;
            if (!t)
                continue;
            scores[n] = atoi(line);
            for (i = 0; line[i]; i++)
                if (line[i] == '\t')
                    line[i] = ' ';
            snprintf(rows[n++], sizeof rows[0], "%s", line);
        }
        fclose(f);
    }
    snprintf(text, sizeof text, "HALL OF FAME\n\n");
    for (i = 0; i < n && i < 10; i++) {
        best = i;
        for (j = i + 1; j < n; j++)
            if (scores[j] > scores[best])
                best = j;
        if (best != i) {
            int ts = scores[i];
            char tr[160];
            scores[i] = scores[best];
            scores[best] = ts;
            memcpy(tr, rows[i], sizeof tr);
            memcpy(rows[i], rows[best], sizeof tr);
            memcpy(rows[best], tr, sizeof tr);
        }
        strncat(text, rows[i], sizeof text - strlen(text) - 2);
        strcat(text, "\n");
    }
    if (n == 0)
        strcat(text, "(no games yet)\n");
    dialog(text, -1, 20, 0, DLG_DARK);
}

void advisor_portraits(void);

/* loaded: 0 for a new game, else 1 + the save slot */
static void play(int loaded)
{
    int h = S->human_civ;

    advisor_portraits();
    if (loaded) {
        /* the original's load (o_11_02fd): the turn resumes with the human's move, the units
         * keeping their moves in slots 0-3 (2 for the end-of-turn slots 4-9) */
        midturn_loaded = loaded - 1 < 4 ? 1 : 2;
        view_x = S->capital_x[h] - 7;              /* (f_01a7_00xx: the view row is fixed) */
        view_y = 19;
        redraw_map();
    }
    quit_flag = 0;
    while (!quit_flag && !gfx_quit_requested) {
        game_turn();
        if (getenv("CIV_TRACE_TURNS"))
            fprintf(stderr, "turn %d year %d quit %d cities %d settlers %d units %d 4c88 %x\n", S->turn, S->year,
                    quit_flag, S->city_count[S->human_civ], S->settler_count[S->human_civ],
                    S->unit_count[S->human_civ], S->unk_4c88);
    }
    if (getenv("CIV_HEADLESS"))
        fprintf(stderr, "game ended: turn %d year %d quit_flag %d conquest %d alive %x human %d cities %d\n",
                S->turn, S->year, quit_flag, game_won_conquest, S->civs_alive, S->human_civ, S->city_count[S->human_civ]);
}

static void title(void)
{
    static Picture logo;
    static int tried;

    if (!tried) {
        tried = 1;
        load_picture("LOGO.PIC", &logo);
    }
    gfx_fill(0, 0, 320, 200, 0);
    if (logo.pixels) {
        if (logo.has_palette)
            gfx_set_palette((const uint8_t (*)[3])logo.palette);
        gfx_blit(&logo, 0, 0, 320, 200, 0, 0, -1);
    } else {
        gfx_set_palette(game_palette);
        draw_text_centered(5, 160, 30, 14, "CIVILIZATION");
    }
    gfx_present();
}

const char *shot_path;      /* --city-shot */

/* $CIV_LOCKSTEP: the saved game bytes and the random state at each wait for input */
static void lockstep_dump(const char *prefix)
{
    static uint8_t buf[SVE_SIZE];
    char path[512];
    FILE *f;

    save_encode(&game.s, buf);
    snprintf(path, sizeof path, "%s.sve", prefix);
    if ((f = fopen(path, "wb"))) {
        fwrite(buf, 1, sizeof buf, f);
        fclose(f);
    }
    snprintf(path, sizeof path, "%s.rng", prefix);
    if ((f = fopen(path, "w"))) {
        fprintf(f, "%lu\nview %d %d\n", game_rand_state(), view_x, view_y);
        fclose(f);
    }
}

/* kill -USR1: where is the game? (a debugging aid for hangs) */
static void where(int sig)
{
    void *bt[32];
    int n = backtrace(bt, 32);

    (void)sig;
    backtrace_symbols_fd(bt, n, 2);
}

int main(int argc, char **argv)
{
    char path[512];
    const char *play_sve = NULL, *play_map = NULL;
    int scale = 3, k, i;

    signal(SIGUSR1, where);
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--data") && i + 1 < argc)
            data_dir = argv[++i];
        else if (!strcmp(argv[i], "--saves") && i + 1 < argc)
            save_dir = argv[++i];
        else if (!strcmp(argv[i], "--scale") && i + 1 < argc)
            scale = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--play") && i + 2 < argc) {
            /* straight into a saved game ($CIV_SEED: the random state just after loading) */
            play_sve = argv[++i];
            play_map = argv[++i];
        }
        else if (!strcmp(argv[i], "--sound-test") && i + 1 < argc) {
            /* play sound N to the end through the audio device, then quit */
            int n = atoi(argv[i + 1]);
            if (sound_init() != 0) {
                fprintf(stderr, "no sound: %s\n", SDL_GetError());
                return 1;
            }
            sound_play(n);
            while (sound_playing())
                SDL_Delay(50);
            SDL_Delay(200);
            return 0;
        } else if (!strcmp(argv[i], "--article-shot") && i + 3 < argc) {
            /* debugging: Civilopedia article (category, item) to a .bmp */
            char fp[512];
            gfx_init(1, 1);
            snprintf(fp, sizeof fp, "%s/FONTS.CV", data_dir);
            if (font_load(fp) || assets_load())
                return 1;
            fresh_state();
            gfx_shot_path = argv[i + 3];
            article_show(atoi(argv[i + 1]), atoi(argv[i + 2]), "ROADS", 0);
            return 0;
        } else if (!strcmp(argv[i], "--palace-shot") && i + 3 < argc) {
            /* debugging: the palace of a saved game, to a .bmp */
            char fp[512];
            gfx_init(1, 1);
            snprintf(fp, sizeof fp, "%s/FONTS.CV", data_dir);
            if (font_load(fp) || assets_load() || load_game(argv[i + 1], argv[i + 2], &game))
                return 1;
            palace_draw(0);
            return gfx_save_bmp(argv[i + 3]) ? 1 : 0;
        } else if (!strcmp(argv[i], "--city-shot") && i + 4 < argc) {
            /* debugging: the city view of city N of a saved game, to a .bmp */
            static const int keys[2] = { 0x1000, 27 };
            const char *sve = argv[i + 1], *map = argv[i + 2], *out = argv[i + 4];
            int city = atoi(argv[i + 3]);
            char fp[512];
            gfx_init(1, 1);
            snprintf(fp, sizeof fp, "%s/FONTS.CV", data_dir);
            if (font_load(fp) || assets_load() || load_game(sve, map, &game))
                return 1;
            (void)keys;
            shot_path = out;
            city_view(city, -1);
            return 0;
        }
        else {
            fprintf(stderr, "usage: civ [--data DIR] [--saves DIR] [--scale N] [--play SVE MAP]\n");
            return 2;
        }
    }
    if (gfx_init(scale, getenv("CIV_HEADLESS") != NULL)) {
        fprintf(stderr, "cannot open the screen\n");
        return 1;
    }
    snprintf(path, sizeof path, "%s/FONTS.CV", data_dir);
    if (font_load(path) || assets_load()) {
        fprintf(stderr, "cannot load the game's files from %s\n", data_dir);
        return 1;
    }
    gfx_set_palette(game_palette);
    {
        static Picture torch;
        if (load_picture("TORCH.PIC", &torch) == 0)
            gfx_set_cursor(&torch);
    }
    if (!getenv("CIV_HEADLESS") && sound_init() != 0)
        fprintf(stderr, "no sound: %s\n", SDL_GetError());
    gfx_script_env();
    gfx_lockstep_dump = lockstep_dump;
    if (play_sve) {
        dialog_texture = 1;
        if (load_game(play_sve, play_map, &game)) {
            fprintf(stderr, "cannot load %s\n", play_sve);
            return 1;
        }
        if (getenv("CIV_SEED"))
            game_srand((unsigned)strtoul(getenv("CIV_SEED"), NULL, 0));
        {
            const char *d = strrchr(play_sve, '.');    /* CIVILn.SVE: slot n */
            play(d && d > play_sve && d[-1] >= '0' && d[-1] <= '9' ? 1 + d[-1] - '0' : 1);
        }
        gfx_quit();
        return 0;
    }
    /* (f_1dde_0040: the original seeds its random numbers once, from the clock, and keeps the
     * seed as world_seed, which places the specials and huts) */
    startup_seed = (unsigned)time(NULL) & 0x7FFF;
    if (getenv("CIV_WORLD_SEED"))
        startup_seed = (unsigned)strtoul(getenv("CIV_WORLD_SEED"), NULL, 0);
    game_srand(getenv("CIV_SEED") ? (unsigned)strtoul(getenv("CIV_SEED"), NULL, 0) : startup_seed);
    for (;;) {
        title();
        /* f_01a7_0488 */
        k = dialog(" Start a New Game\n Load a Saved Game\n EARTH\n Customize World\n View Hall of Fame\n", 100, 140, 0,
                   0);
        gfx_set_palette(game_palette);
        if (gfx_quit_requested)
            break;
        if (k < 0)
            continue;
        if (k != 4)
            dialog_texture = 1;                  /* (L0812: g_2f48 = 1 once a game is chosen) */
        if (k == 4) {
            hall_of_fame();
            continue;
        }
        if (k == 1) {
            if ((k = load_menu()) && !gfx_quit_requested)
                play(k);
            continue;
        }
        if (start_new_game(k == 2, k == 3) && !gfx_quit_requested)
            play(0);
    }
    gfx_quit();
    return 0;
}
