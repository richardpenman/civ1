#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pic.h"

/* LZW as implemented in the original (root 0x1219-0x13B3): a 2048-entry prefix/char table;
 * every code adds an entry (even the first after a reset, which links to the stale previous
 * code), the width grows when the next code passes the mask, and the table resets once the
 * width would exceed maxbits. */
typedef struct {
    const uint8_t *src, *end;
    uint32_t bitbuf;
    int      nbits;
    int      width, maxbits;
    unsigned mask, next, old;
    uint8_t  oldchar;
    uint16_t prefix[0x800];
    uint8_t  chr[0x800];
    uint8_t  stack[0x800];
    int      sp;
} Lzw;

static void lzw_reset(Lzw *z)
{
    int i;
    for (i = 0; i < 0x800; i++)
        z->prefix[i] = 0xFFFF;
    for (i = 0; i < 256; i++)
        z->chr[i] = (uint8_t)i;
    z->width = 9;
    z->mask = 0x1FF;
    z->next = 0x100;
}

/* Returns the next decoded byte, or -1 at end of input. */
static int lzw_byte(Lzw *z)
{
    unsigned code, cur, c;

    if (z->sp > 0)
        return z->stack[--z->sp];
    while (z->nbits < z->width && z->src < z->end) {
        z->bitbuf |= (uint32_t)*z->src++ << z->nbits;
        z->nbits += 8;
    }
    if (z->nbits < z->width)
        return -1;
    code = z->bitbuf & z->mask;
    z->bitbuf >>= z->width;
    z->nbits -= z->width;

    cur = code;
    if (code >= z->next) {
        z->stack[z->sp++] = z->oldchar;
        code = z->old;
    }
    for (c = code; z->prefix[c] != 0xFFFF && z->sp < 0x7FF; c = z->prefix[c])
        z->stack[z->sp++] = z->chr[c];
    z->stack[z->sp++] = z->chr[c];
    z->oldchar = z->chr[c];

    z->chr[z->next & 0x7FF] = z->oldchar;
    z->prefix[z->next & 0x7FF] = (uint16_t)z->old;
    if (++z->next > z->mask) {
        z->width++;
        z->mask = z->mask * 2 + 1;
        if (z->width > z->maxbits)
            lzw_reset(z);
    }
    z->old = cur;
    return z->stack[--z->sp];
}

static int decode_image(const uint8_t *p, long len, int four_bit, Picture *pic)
{
    Lzw *z;
    long n, total;
    int last = 0, run = 0, b;

    if (len < 5)
        return -1;
    pic->width = p[0] | p[1] << 8;
    pic->height = p[2] | p[3] << 8;
    if (pic->width & 1)
        pic->width++;                   /* (rows are kept an even number of pixels wide: TORCH.PIC) */
    total = (long)pic->width * pic->height;
    pic->pixels = calloc(1, total ? total : 1);
    z = calloc(1, sizeof *z);
    if (!pic->pixels || !z) {
        free(z);
        return -1;
    }
    z->src = p + 5;
    z->end = p + len;
    z->maxbits = p[4] > 11 ? 11 : p[4];
    lzw_reset(z);

    for (n = 0; n < total; ) {
        if (run > 0) {
            run--;
            b = last;
        } else {
            if ((b = lzw_byte(z)) < 0)
                break;
            if (b == 0x90) {
                int cnt = lzw_byte(z);
                if (cnt < 0)
                    break;
                if (cnt == 0) {
                    b = 0x90;
                } else {
                    run = cnt - 1;
                    continue;
                }
            }
            last = b;
        }
        if (four_bit) {
            pic->pixels[n++] = b & 15;
            if (n < total)
                pic->pixels[n++] = b >> 4;
        } else {
            pic->pixels[n++] = (uint8_t)b;
        }
    }
    free(z);
    return 0;
}

int pic_decode(const uint8_t *data, long len, Picture *pic)
{
    long p = 0;

    memset(pic, 0, sizeof *pic);
    while (p + 4 <= len) {
        char tag = (char)data[p];
        int n = data[p + 2] | data[p + 3] << 8;
        if (data[p + 1] != '0' && data[p + 1] != '1')
            break;
        if (tag == 'X') {
            if (n == 4 && pic->has_palette)     /* palette files end with an empty image */
                return 0;
            return decode_image(data + p + 4, len - p - 4, data[p + 1] == '1', pic);
        }
        if (p + 4 + n > len)
            break;
        if (tag == 'M' && n >= 2) {
            int i, first = data[p + 4], lastc = data[p + 5];
            for (i = first; i <= lastc && 6 + 3 * (i - first) + 2 < n + 4; i++) {
                const uint8_t *rgb = data + p + 6 + 3 * (i - first);
                pic->palette[i][0] = rgb[0] * 255 / 63;
                pic->palette[i][1] = rgb[1] * 255 / 63;
                pic->palette[i][2] = rgb[2] * 255 / 63;
            }
            pic->has_palette = 1;
        } else if (tag == 'E' && n >= 2) {
            memcpy(pic->ega_map, data + p + 6, n - 2 > 256 ? 256 : n - 2);
            pic->has_ega_map = 1;
        }
        p += 4 + n;
    }
    return 0;
}

int pic_load(const char *path, Picture *pic)
{
    FILE *f = fopen(path, "rb");
    uint8_t *buf;
    long len;
    int r;

    memset(pic, 0, sizeof *pic);
    if (!f)
        return -1;
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = malloc(len ? len : 1);
    if (!buf || fread(buf, 1, len, f) != (size_t)len) {
        free(buf);
        fclose(f);
        return -1;
    }
    fclose(f);
    r = pic_decode(buf, len, pic);
    free(buf);
    return r;
}

void pic_free(Picture *pic)
{
    free(pic->pixels);
    pic->pixels = NULL;
}

/* Writing pictures (saved maps).  The pixels are run-length coded as the decoder expects
 * (0x90 n = the last byte n times in all, 0x90 0 = a literal 0x90), then sent as LZW codes
 * that are all literals: the decoder still grows its table and code width as usual, and
 * the encoder follows it, so any reader of the format decodes it. */
typedef struct {
    uint8_t *out;
    long     len, cap;
    uint32_t bits;
    int      nbits, width, maxbits;
    unsigned mask, next;
} LzwOut;

static void put_byte(LzwOut *w, int b)
{
    if (w->len == w->cap) {
        w->cap = w->cap ? w->cap * 2 : 4096;
        w->out = realloc(w->out, (size_t)w->cap);
    }
    w->out[w->len++] = (uint8_t)b;
}

static void put_code(LzwOut *w, int code)
{
    w->bits |= (uint32_t)code << w->nbits;
    w->nbits += w->width;
    while (w->nbits >= 8) {
        put_byte(w, (int)(w->bits & 0xFF));
        w->bits >>= 8;
        w->nbits -= 8;
    }
    if (++w->next > w->mask) {
        w->width++;
        w->mask = w->mask * 2 + 1;
        if (w->width > w->maxbits) {
            w->width = 9;
            w->mask = 0x1FF;
            w->next = 0x100;
        }
    }
}

int pic_save_image(const char *path, const Picture *pic)
{
    LzwOut w;
    long n, total = (long)pic->width * pic->height, i, run;
    uint8_t hdr[9];
    FILE *f;
    int b;

    memset(&w, 0, sizeof w);
    w.width = 9;
    w.maxbits = 11;
    w.mask = 0x1FF;
    w.next = 0x100;
    for (i = 0; i < total; i += run) {
        b = pic->pixels[i];
        for (run = 1; i + run < total && pic->pixels[i + run] == b && run < 255; run++)
            ;
        if (b == 0x90) {
            put_code(&w, 0x90);
            put_code(&w, 0);
            run = 1;
        } else if (run > 2) {
            put_code(&w, b);
            put_code(&w, 0x90);
            put_code(&w, (int)run);
        } else {
            put_code(&w, b);
            run = 1;
        }
    }
    if (w.nbits > 0)
        put_byte(&w, (int)(w.bits & 0xFF));
    n = w.len + 5;
    hdr[0] = 'X';
    hdr[1] = '0';
    hdr[2] = (uint8_t)(n & 0xFF);
    hdr[3] = (uint8_t)(n >> 8);
    hdr[4] = (uint8_t)(pic->width & 0xFF);
    hdr[5] = (uint8_t)(pic->width >> 8);
    hdr[6] = (uint8_t)(pic->height & 0xFF);
    hdr[7] = (uint8_t)(pic->height >> 8);
    hdr[8] = (uint8_t)w.maxbits;
    f = fopen(path, "wb");
    if (!f) {
        free(w.out);
        return -1;
    }
    fwrite(hdr, 1, sizeof hdr, f);
    fwrite(w.out, 1, (size_t)w.len, f);
    free(w.out);
    return fclose(f) ? -1 : 0;
}
