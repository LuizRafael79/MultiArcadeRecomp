#ifndef MULTIARCADERECOMP_GAME_DRIVER_V1_H
#define MULTIARCADERECOMP_GAME_DRIVER_V1_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#define MULTIARCADERECOMP_GAME_DRIVER_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define MULTIARCADERECOMP_GAME_DRIVER_EXPORT __attribute__((visibility("default")))
#else
#define MULTIARCADERECOMP_GAME_DRIVER_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define MULTIARCADERECOMP_GAME_DRIVER_ABI_V1 1u

typedef struct MultiArcadeRecompGameHostV1 {
    uint32_t struct_size;
    uint32_t abi_version;
    void *userdata;
    const void *(*get_board_api)(void *userdata, const char *board_id,
                                 uint32_t requested_version, size_t *api_size);
    void (*log)(void *userdata, uint32_t level, const char *message);
} MultiArcadeRecompGameHostV1;

/* The board owns this typed interface. Game modules receive it as an opaque
 * pointer and include that board's C API header before using it. */
typedef int (*MultiArcadeRecompGameConfigureV1)(const MultiArcadeRecompGameHostV1 *host,
                                         const void *board_api,
                                         size_t board_api_size);

typedef struct MultiArcadeRecompGameDriverV1 {
    uint32_t struct_size;
    uint32_t abi_version;
    const char *game_id;
    const char *parent_id;
    const char *board_id;
    uint32_t board_api_version;
    MultiArcadeRecompGameConfigureV1 configure;
} MultiArcadeRecompGameDriverV1;

MULTIARCADERECOMP_GAME_DRIVER_EXPORT
const MultiArcadeRecompGameDriverV1 *multiarcaderecomp_game_driver_v1(void);

#ifdef __cplusplus
}
#endif

#endif
