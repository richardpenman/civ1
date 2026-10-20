#ifndef SAVE_H
#define SAVE_H

#include <stddef.h>
#include <stdint.h>
#include "game.h"

#define SVE_SIZE 37856

int save_decode(const uint8_t *buf, long len, SaveData *s);   /* 0 on success */
void save_encode(const SaveData *s, uint8_t *buf);              /* buf: SVE_SIZE bytes */
int load_game(const char *sve_path, const char *map_path, Game *g);
void save_field_name(long pos, char *out, size_t n);
int save_game(const char *sve_path, const char *map_path, const Game *g);

#endif
