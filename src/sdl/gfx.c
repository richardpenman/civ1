/* The SDL front end's screen and input. */
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gfx.h"

uint8_t screen[SCR_H][SCR_W];
int mouse_x = 160, mouse_y = 100, mouse_button;   /* (the mouse starts in the middle) */

/* The mouse pointer (TORCH.PIC), drawn over what is shown as the original's driver does. */
static const Picture *cursor;
static int cursor_hidden;               /* (f_01a7_026a / f_01a7_0252 hide and show it) */

void gfx_cursor(int show)
{
    cursor_hidden = !show;
}

void gfx_set_cursor(const Picture *p)
{
    cursor = p;
}

static void cursor_overlay(uint8_t dst[SCR_H][SCR_W])
{
    int x, y, c;

    if (!cursor || !cursor->pixels || cursor_hidden)
        return;
    for (y = 0; y < cursor->height; y++)
        for (x = 0; x < cursor->width; x++) {
            c = cursor->pixels[y * cursor->width + x];
            if (c && mouse_x + x < SCR_W && mouse_y + y < SCR_H && mouse_x + x >= 0 && mouse_y + y >= 0)
                dst[mouse_y + y][mouse_x + x] = (uint8_t)c;
        }
}
int gfx_quit_requested;

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *texture;
static uint32_t palette32[256];
static int headless_mode;

int gfx_init(int scale, int headless)
{
    headless_mode = headless;
    if (headless)
        return 0;
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
        return -1;
    window = SDL_CreateWindow("Civilization", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              SCR_W * scale, SCR_H * scale, SDL_WINDOW_RESIZABLE);
    if (!window)
        return -1;
    renderer = SDL_CreateRenderer(window, -1, 0);
    SDL_ShowCursor(SDL_DISABLE);
    if (!renderer)
        return -1;
    SDL_RenderSetLogicalSize(renderer, SCR_W, SCR_H);
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, SCR_W, SCR_H);
    return texture ? 0 : -1;
}

void gfx_quit(void)
{
    if (!headless_mode)
        SDL_Quit();
}

void gfx_set_palette(const uint8_t pal[256][3])
{
    int i;

    for (i = 0; i < 256; i++)
        palette32[i] = 0xFF000000u | (uint32_t)pal[i][0] << 16 | (uint32_t)pal[i][1] << 8 | pal[i][2];
}

/* f_1dde_05db, f_0000_0374 / 043c: the sea's waves.  Colours 96-103, 104-111 and 112-127
 * rotate one place every 15 ticks of the 60 Hz timer while the map is up (f_1dde_0624 starts
 * them at each redraw_map, full-screen pages stop them: here they turn when the palette still
 * holds the map's colours there).  Colour first + i shows colour (i + pos) % n of the range. */
static const char *lockstep_dir;   /* $CIV_LOCKSTEP (gfx_script_env) */
static const struct { int first, n; } cycles[3] = { { 96, 8 }, { 104, 8 }, { 112, 16 } };
static int cycle_pos[3], cycle_on;
static uint32_t cycle_base[32], cycle_next;

void gfx_cycle_start(void)
{
    if (headless_mode || lockstep_dir)
        return;
    if (cycle_on)
        return;
    memcpy(cycle_base, palette32 + 96, sizeof cycle_base);
    cycle_on = 1;
    cycle_next = SDL_GetTicks() + 250;
}

static void cycle_palette(uint32_t *pal)
{
    int k, i;

    if (!cycle_on || memcmp(cycle_base, palette32 + 96, sizeof cycle_base))
        return;
    for (k = 0; k < 3; k++)
        for (i = 0; i < cycles[k].n; i++)
            pal[cycles[k].first + i] = cycle_base[cycles[k].first - 96 + (i + cycle_pos[k]) % cycles[k].n];
}

static void cycle_tick(void)
{
    int k, moved = 0;

    if (!cycle_on)
        return;
    while ((int32_t)(SDL_GetTicks() - cycle_next) >= 0) {
        for (k = 0; k < 3; k++)
            cycle_pos[k] = (cycle_pos[k] + cycles[k].n - 1) % cycles[k].n;
        cycle_next += 250;
        moved = 1;
    }
    if (moved && !memcmp(cycle_base, palette32 + 96, sizeof cycle_base))
        gfx_present();
}

void gfx_present(void)
{
    static uint32_t rgb[SCR_H][SCR_W];
    uint32_t pal[256];
    int x, y;

    static uint8_t shown[SCR_H][SCR_W];

    if (headless_mode)
        return;
    memcpy(shown, screen, sizeof shown);
    cursor_overlay(shown);
    memcpy(pal, palette32, sizeof pal);
    cycle_palette(pal);
    for (y = 0; y < SCR_H; y++)
        for (x = 0; x < SCR_W; x++)
            rgb[y][x] = pal[shown[y][x]];
    SDL_UpdateTexture(texture, NULL, rgb, SCR_W * 4);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer);
}

int gfx_save_bmp(const char *path)
{
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, SCR_W, SCR_H, 32, SDL_PIXELFORMAT_ARGB8888);
    int x, y, r;

    if (!s)
        return -1;
    for (y = 0; y < SCR_H; y++)
        for (x = 0; x < SCR_W; x++)
            ((uint32_t *)((uint8_t *)s->pixels + y * s->pitch))[x] = palette32[screen[y][x]];
    r = SDL_SaveBMP(s, path);
    SDL_FreeSurface(s);
    return r;
}

void gfx_pixel(int x, int y, int c)
{
    if (x >= 0 && x < SCR_W && y >= 0 && y < SCR_H)
        screen[y][x] = (uint8_t)c;
}

int gfx_get(int x, int y)
{
    return x >= 0 && x < SCR_W && y >= 0 && y < SCR_H ? screen[y][x] : 0;
}

void gfx_fill(int x, int y, int w, int h, int c)
{
    int i, j;

    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            gfx_pixel(x + i, y + j, c);
}

void gfx_hline(int x1, int x2, int y, int c)
{
    int x;

    for (x = x1; x <= x2; x++)
        gfx_pixel(x, y, c);
}

void gfx_vline(int x, int y1, int y2, int c)
{
    int y;

    for (y = y1; y <= y2; y++)
        gfx_pixel(x, y, c);
}

void gfx_rect(int x, int y, int w, int h, int c)
{
    gfx_hline(x, x + w, y, c);
    gfx_hline(x, x + w, y + h, c);
    gfx_vline(x, y, y + h, c);
    gfx_vline(x + w, y, y + h, c);
}

void gfx_blit(const Picture *p, int sx, int sy, int w, int h, int dx, int dy, int transparent)
{
    int i, j, c;

    if (!p || !p->pixels)
        return;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++) {
            if (sx + i < 0 || sx + i >= p->width || sy + j < 0 || sy + j >= p->height)
                continue;
            c = p->pixels[(sy + j) * p->width + sx + i];
            if (c != transparent)
                gfx_pixel(dx + i, dy + j, c);
        }
}

void gfx_recolour(int x, int y, int w, int h, int from, int to)
{
    int i, j;

    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            if (gfx_get(x + i, y + j) == from)
                gfx_pixel(x + i, y + j, to);
}

static int translate(const SDL_Keysym *k)
{
    SDL_Keycode c = k->sym;

    switch (c) {
    case SDLK_KP_1: return '1';
    case SDLK_KP_2: case SDLK_DOWN: return k->mod & KMOD_SHIFT ? KEY_DOWN : '2';
    case SDLK_KP_3: return '3';
    case SDLK_KP_4: case SDLK_LEFT: return k->mod & KMOD_SHIFT ? KEY_LEFT : '4';
    case SDLK_KP_5: return '5';
    case SDLK_KP_6: case SDLK_RIGHT: return k->mod & KMOD_SHIFT ? KEY_RIGHT : '6';
    case SDLK_KP_7: case SDLK_HOME: return '7';
    case SDLK_KP_8: case SDLK_UP: return k->mod & KMOD_SHIFT ? KEY_UP : '8';
    case SDLK_KP_9: case SDLK_PAGEUP: return '9';
    case SDLK_END: return '1';
    case SDLK_PAGEDOWN: return '3';
    case SDLK_RETURN: case SDLK_KP_ENTER: return 13;
    case SDLK_ESCAPE: return 27;
    case SDLK_SPACE: return ' ';
    case SDLK_BACKSPACE: return 8;
    default: break;
    }
    if (c >= SDLK_F1 && c <= SDLK_F10)
        return KEY_F1 + (int)(c - SDLK_F1);
    if (c >= 'a' && c <= 'z')
        return (k->mod & KMOD_SHIFT) ? (int)c - 32 : (int)c;
    if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '=' || c == '.' || c == ',')
        return (int)c;
    return 0;
}

static const int *script;
static const char *lockstep_dir;
static int script_n, script_pos;

void gfx_script(const int *keys, int n)
{
    script = keys;
    script_n = n;
    script_pos = 0;
}

/* CIV_KEYS="a b ENTER ESC F1 M:x,y SHOT:file.bmp ...": a test script of input. */
static char *shots[64];
static int nshots;
static int script_buf[65536];

void gfx_script_env(void)
{
    const char *env = getenv("CIV_KEYS");
    char tok[256];
    int n = 0, len;

    lockstep_dir = getenv("CIV_LOCKSTEP");
    if (!env)
        return;
    while (*env && n < 65530) {
        while (*env == ' ')
            env++;
        for (len = 0; *env && *env != ' ' && len < 255; )
            tok[len++] = *env++;
        tok[len] = 0;
        if (!len)
            break;
        if (!strcmp(tok, "ENTER")) script_buf[n++] = 13;
        else if (!strcmp(tok, "ESC")) script_buf[n++] = 27;
        else if (!strcmp(tok, "SPACE")) script_buf[n++] = ' ';
        else if (!strcmp(tok, "BKSP")) script_buf[n++] = 8;
        else if (!strcmp(tok, "TAB")) script_buf[n++] = 9;
        /* the keys as translate() reports them */
        else if (!strcmp(tok, "UP")) script_buf[n++] = '8';
        else if (!strcmp(tok, "DOWN")) script_buf[n++] = '2';
        else if (!strcmp(tok, "LEFT")) script_buf[n++] = '4';
        else if (!strcmp(tok, "RIGHT")) script_buf[n++] = '6';
        else if (!strcmp(tok, "HOME")) script_buf[n++] = '7';
        else if (!strcmp(tok, "END")) script_buf[n++] = '1';
        else if (!strcmp(tok, "PGUP")) script_buf[n++] = '9';
        else if (!strcmp(tok, "PGDN")) script_buf[n++] = '3';
        else if (!strncmp(tok, "KP", 2) && tok[2] >= '0' && tok[2] <= '9') script_buf[n++] = tok[2];
        else if (tok[0] == 'F' && tok[1] >= '1' && tok[1] <= '9') script_buf[n++] = KEY_F1 + atoi(tok + 1) - 1;
        else if (!strncmp(tok, "M:", 2) || !strncmp(tok, "R:", 2)) {
            script_buf[n++] = tok[0] == 'R' ? KEY_MOUSE + 0x100 : KEY_MOUSE;   /* (R: the right button) */
            script_buf[n++] = atoi(tok + 2);
            script_buf[n++] = atoi(strchr(tok, ',') ? strchr(tok, ',') + 1 : "0");
        } else if (!strncmp(tok, "SHOT:", 5) && nshots < 64) {
            shots[nshots] = malloc(strlen(tok + 5) + 1);
            strcpy(shots[nshots], tok + 5);
            script_buf[n++] = 0x1000 + nshots++;
        } else
            script_buf[n++] = (unsigned char)tok[0];
    }
    gfx_script(script_buf, n);
}

unsigned gfx_ticks(void)
{
    static unsigned fake;
    if (headless_mode)
        return fake += 50;
    return SDL_GetTicks();
}

/* Lockstep tracing ($CIV_LOCKSTEP=dir, tools/lockstep.py): input comes only from the script
 * and only where the game waits for it; at each such wait the screen (NNNN.scr: 64000 pixels
 * then 256 palette entries 0xAARRGGBB) and whatever gfx_lockstep_dump adds are written first.
 * The game exits when the script runs out. */
static int lockstep_n;
void (*gfx_lockstep_dump)(const char *prefix);

static int lockstep_take(void)
{
    char path[512];
    FILE *f;
    int k;

    static uint8_t shown[SCR_H][SCR_W];

    snprintf(path, sizeof path, "%s/%04d.scr", lockstep_dir, lockstep_n);
    f = fopen(path, "wb");
    if (f) {
        memcpy(shown, screen, sizeof shown);
        cursor_overlay(shown);
        fwrite(shown, 1, sizeof shown, f);
        fwrite(palette32, 4, 256, f);
        fclose(f);
    }
    snprintf(path, sizeof path, "%s/%04d", lockstep_dir, lockstep_n++);
    if (gfx_lockstep_dump)
        gfx_lockstep_dump(path);
    while (script_pos < script_n && script[script_pos] >= 0x1000 && script[script_pos] < 0x1000 + nshots)
        script_pos++;
    if (script_pos >= script_n)
        exit(0);
    k = script[script_pos++];
    if ((k == KEY_MOUSE || k == KEY_MOUSE + 0x100) && script_pos + 2 <= script_n) {
        mouse_x = script[script_pos++];
        mouse_y = script[script_pos++];
        mouse_button = k == KEY_MOUSE ? 1 : 2;
        k = KEY_MOUSE;
    }
    return k;
}

int gfx_poll_wait(void)
{
    if (lockstep_dir)
        return lockstep_take();
    return gfx_poll_key();
}

int gfx_poll_key(void)
{
    SDL_Event e;

    if (lockstep_dir)
        return 0;

    while (script_pos < script_n && script[script_pos] >= 0x1000 && script[script_pos] < 0x1000 + nshots) {
        gfx_save_bmp(shots[script[script_pos++] - 0x1000]);
    }
    if (script_pos < script_n) {
        int k = script[script_pos++];
        if ((k == KEY_MOUSE || k == KEY_MOUSE + 0x100) && script_pos + 2 <= script_n) {
            mouse_x = script[script_pos++];
            mouse_y = script[script_pos++];
            mouse_button = k == KEY_MOUSE ? 1 : 2;
            k = KEY_MOUSE;
        }
        return k;
    }
    if (headless_mode) {
        gfx_quit_requested = 1;
        return KEY_QUIT;
    }
    cycle_tick();
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT:
            gfx_quit_requested = 1;
            return KEY_QUIT;
        case SDL_KEYDOWN: {
            int k = translate(&e.key.keysym);
            if (k)
                return k;
            break;
        }
        case SDL_MOUSEMOTION:
            mouse_x = e.motion.x;
            mouse_y = e.motion.y;
            gfx_present();
            break;
        case SDL_MOUSEBUTTONDOWN:
            mouse_x = e.button.x;
            mouse_y = e.button.y;
            mouse_button = e.button.button == SDL_BUTTON_RIGHT ? 2 : 1;
            return KEY_MOUSE;
        default:
            break;
        }
    }
    return 0;
}

const char *gfx_shot_path;     /* debugging: the next wait for a key saves the screen here */

int gfx_wait_key(void)
{
    int k;

    gfx_present();
    if (gfx_shot_path) {
        gfx_save_bmp(gfx_shot_path);
        gfx_shot_path = NULL;
    }
    if (lockstep_dir)
        return lockstep_take();
    for (;;) {
        k = gfx_poll_key();
        if (k)
            return k;
        if (!headless_mode)
            SDL_Delay(15);
    }
}

void gfx_delay(int ms)
{
    if (!headless_mode) {
        gfx_present();
        SDL_Delay((Uint32)ms);
    }
}
