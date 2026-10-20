/* .PIC / .PAL / .MAP picture files (see NOTES.md for the chunk format). */
#ifndef PIC_H
#define PIC_H

#include <stdint.h>

typedef struct {
    int      width, height;
    uint8_t *pixels;          /* width*height palette indices, NULL if no image chunk */
    int      has_palette;
    uint8_t  palette[256][3]; /* 8-bit RGB */
    int      has_ega_map;
    uint8_t  ega_map[256];
} Picture;

int  pic_load(const char *path, Picture *pic);   /* 0 on success */
int  pic_decode(const uint8_t *data, long len, Picture *pic);
void pic_free(Picture *pic);
int  pic_save_image(const char *path, const Picture *pic);   /* an X0 chunk only, like .MAP files */

#endif
