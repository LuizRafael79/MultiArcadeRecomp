#include "adsprec.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

static int read_u16(FILE *f, uint16_t *v)
{
    unsigned char b[2];
    if (fread(b, 1, 2, f) != 2) return 0;
    *v = (uint16_t)(b[0] | ((uint16_t)b[1] << 8));
    return 1;
}

static int read_u32(FILE *f, uint32_t *v)
{
    unsigned char b[4];
    if (fread(b, 1, 4, f) != 4) return 0;
    *v = (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
         ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    return 1;
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: adsprec_build IMAGE_LIST OUTPUT_DIR\n");
        return 2;
    }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    uint32_t n;
    if (!read_u32(f, &n) || n > 64) { fprintf(stderr, "bad image count\n"); fclose(f); return 1; }
    adsprec_image_t *images = calloc(n, sizeof(*images));
    uint32_t **storage = calloc(n, sizeof(*storage));
    if (!images || !storage) { fclose(f); free(images); free(storage); return 1; }
    int ok = 1;
    for (uint32_t i = 0; i < n && ok; ++i) {
        uint16_t base, words;
        if (!read_u16(f, &base) || !read_u16(f, &words)) { ok = 0; break; }
        storage[i] = calloc(words, sizeof(uint32_t));
        if (!storage[i]) { ok = 0; break; }
        for (uint16_t k = 0; k < words; ++k)
            if (!read_u32(f, &storage[i][k])) { ok = 0; break; }
        images[i] = (adsprec_image_t){ base, words, storage[i] };
    }
    if (fgetc(f) != EOF) ok = 0;
    fclose(f);
    if (!ok) fprintf(stderr, "invalid ADSP image-list file\n");
    else {
        char error[512] = {0};
        ok = adsprec_generate(images, (int)n, argv[2], error, sizeof error);
        if (!ok) fprintf(stderr, "%s\n", error);
    }
    for (uint32_t i = 0; i < n; ++i) free(storage[i]);
    free(storage);
    free(images);
    return ok ? 0 : 1;
}
