/* Shared parts of the SDL front end (ui_sdl.c, main.c). */
#ifndef FRONT_H
#define FRONT_H

#include <stddef.h>

extern const char *save_dir;
void main_screen(void);
int  save_slot(int slot);
void make_save_dir(void);
void describe_slot(int slot, char *out, size_t n);
int  slot_exists(int slot);
int  load_slot(int slot);
int  ui_city_screen_again(int city);

#endif
