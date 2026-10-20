/* The SDL front end's screen: a 320 x 200 indexed framebuffer, like the original's VGA. */
#ifndef GFX_H
#define GFX_H

#include <stdint.h>
#include "../pic.h"

#define SCR_W 320
#define SCR_H 200

extern uint8_t screen[SCR_H][SCR_W];

int  gfx_init(int scale, int headless);
void gfx_quit(void);
void gfx_set_palette(const uint8_t pal[256][3]);
void gfx_present(void);
void gfx_cycle_start(void);          /* f_1dde_0624: the sea's colours turn while the map is up */
int  gfx_save_bmp(const char *path);
void gfx_set_cursor(const Picture *p);            /* the mouse pointer picture */
void gfx_cursor(int show);                        /* the pointer shown or hidden */

void gfx_pixel(int x, int y, int c);
int  gfx_get(int x, int y);
void gfx_fill(int x, int y, int w, int h, int c);
void gfx_rect(int x, int y, int w, int h, int c);        /* outline (x..x+w, y..y+h) */
void gfx_hline(int x1, int x2, int y, int c);
void gfx_vline(int x, int y1, int y2, int c);
/* Copy a rectangle of a picture; `transparent` >= 0 skips that colour. */
void gfx_blit(const Picture *p, int sx, int sy, int w, int h, int dx, int dy, int transparent);
/* Replace colour `from` by `to` in a screen rectangle (the original's recolouring). */
void gfx_recolour(int x, int y, int w, int h, int from, int to);

/* Input.  Keys are reported as the original's codes: '1'..'9' for the numeric pad
 * directions, letters, 13 Enter, 27 Escape, ' ' and the KEY_* values below. */
enum { KEY_NONE = 0, KEY_UP = 0x148, KEY_DOWN = 0x150, KEY_LEFT = 0x14B, KEY_RIGHT = 0x14D,
       KEY_F1 = 0x13B, KEY_F10 = 0x144, KEY_MOUSE = 0x400, KEY_QUIT = 0x401 };
extern int mouse_x, mouse_y, mouse_button;
extern int gfx_quit_requested;
extern const char *gfx_shot_path;           /* the window was closed (or a test script ended) */
int  gfx_poll_key(void);              /* 0 if none */
int  gfx_poll_wait(void);             /* the same where the game idles waiting for input */
extern void (*gfx_lockstep_dump)(const char *prefix);   /* $CIV_LOCKSTEP: more state per wait */
int  gfx_wait_key(void);              /* blocks; presents the screen while waiting */
void gfx_delay(int ms);
unsigned gfx_ticks(void);                 /* milliseconds */
/* Scripted input (tests): keys returned before any real ones; "M x y" clicks. */
void gfx_script(const int *keys, int n);
void gfx_script_env(void);                /* from $CIV_KEYS */

#endif
