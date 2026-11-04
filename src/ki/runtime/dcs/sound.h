#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
bool sound_init(const char *rom_dir);
void sound_power_on(void);
void sound_reset_w(bool released);
void sound_data_w(uint16_t data);
bool sound_input_empty(void);
void sound_run(int cycles);
void sound_set_interpreter_only(bool enabled);
int sound_fetch(int16_t *out, int max);
void sound_report(void);
void sound_report_file(const char *path);
#ifdef __cplusplus
}
#endif
