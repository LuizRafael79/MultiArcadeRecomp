/* SPDX-License-Identifier: MIT
 * KOF98 feasibility probe for the vendored Pacrecomp frontend.
 * The imported frontend source retains its upstream MIT license separately.
 */
#include "pacrecomp/analyze.h"
#include "pacrecomp/emit.h"

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { kFixedRomSize = 0x8000 };

static int append_seed(uint16_t **seeds, size_t *count, size_t *capacity,
                       unsigned long value) {
    if (value >= kFixedRomSize) return 0;
    if (*count == *capacity) {
        size_t next = *capacity ? *capacity * 2 : 32;
        uint16_t *grown = realloc(*seeds, next * sizeof(**seeds));
        if (!grown) return -1;
        *seeds = grown;
        *capacity = next;
    }
    (*seeds)[(*count)++] = (uint16_t)value;
    return 0;
}

static int read_fixed_rom(const char *path, uint8_t *image) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "cannot open ROM image %s: %s\n", path, strerror(errno));
        return -1;
    }
    const size_t count = fread(image, 1, kFixedRomSize, file);
    const int extra = fgetc(file);
    fclose(file);
    if (count != kFixedRomSize || extra == EOF) {
        fprintf(stderr, "expected image with at least %u bytes: %s\n",
                kFixedRomSize, path);
        return -1;
    }
    return 0;
}

static int read_entries(const char *path, uint16_t **seeds, size_t *count,
                        size_t *capacity) {
    FILE *file = fopen(path, "r");
    if (!file) {
        fprintf(stderr, "cannot open entry list %s: %s\n", path, strerror(errno));
        return -1;
    }
    char line[256];
    while (fgets(line, sizeof line, file)) {
        char *cursor = line;
        while (isspace((unsigned char)*cursor)) ++cursor;
        if (!*cursor || *cursor == '#') continue;
        errno = 0;
        char *end = NULL;
        const unsigned long value = strtoul(cursor, &end, 16);
        if (end == cursor || errno || (*end && !isspace((unsigned char)*end) && *end != '#')) {
            fprintf(stderr, "invalid entry in %s: %s", path, line);
            fclose(file);
            return -1;
        }
        if (append_seed(seeds, count, capacity, value) != 0) {
            fclose(file);
            return -1;
        }
    }
    fclose(file);
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: %s <fixed-rom-image> <entries.txt> <output-dir>\n", argv[0]);
        return 2;
    }

    uint8_t *image = calloc(kFixedRomSize, 1);
    uint16_t *seeds = NULL;
    size_t seed_count = 0, seed_capacity = 0;
    if (!image || read_fixed_rom(argv[1], image) != 0 ||
        append_seed(&seeds, &seed_count, &seed_capacity, 0) != 0 ||
        append_seed(&seeds, &seed_count, &seed_capacity, 0x38) != 0 ||
        read_entries(argv[2], &seeds, &seed_count, &seed_capacity) != 0) {
        free(image);
        free(seeds);
        return 1;
    }

    z80_prog_t program;
    if (z80_analyze(image, kFixedRomSize, 0, seeds, (int)seed_count, &program) != 0) {
        fprintf(stderr, "Pacrecomp analysis failed\n");
        free(image);
        free(seeds);
        return 1;
    }
    fprintf(stderr, "Pacrecomp analyzer: seeds=%zu instructions=%d blocks=%d outside=%d\n",
            seed_count, program.ninsns, program.nblocks, program.next_ext);
    const int emitted = z80_emit(image, kFixedRomSize, 0, program.blocks,
                                 program.nblocks, argv[3], "kof98", program.smc);
    fprintf(stderr, "Pacrecomp emitter: emitted=%d\n", emitted);

    z80_prog_free(&program);
    free(image);
    free(seeds);
    return emitted < 0 ? 1 : 0;
}
