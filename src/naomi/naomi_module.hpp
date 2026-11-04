#pragma once

#include <cstddef>
#include <cstdint>

// C-compatible descriptor exported by mamerecomp_naomi.so. Keep this POD
// stable; translated SH-4 entry points will use a separate ABI from 68k.
struct NaomiProfileRecord {
    const char* set_id;
    const char* title;
    const char* parent_id;
    const char* rom_board;
    const char* key_parameter;
    const char* security_key;
    std::uint32_t rom_board_region_size;
    std::uint8_t hardware_generation; // 1 = NAOMI, 2 = NAOMI 2
    std::uint8_t sh4_count;
    std::uint8_t pvr2_count;
    bool has_arm7_aica;
    bool has_maple_jvs;
    bool has_elan;
    bool has_gdrom;
};

struct NaomiSh4ProbeResult {
    std::uint64_t instructions_executed;
    std::uint64_t native_instructions;
    std::uint64_t interpreted_instructions;
    std::uint32_t entry;
    std::uint32_t pc;
    std::uint32_t unresolved_target;
    std::uint32_t exception_code;
    std::uint64_t pvr_ta_bytes;
    std::uint64_t pvr_yuv_bytes;
    std::uint64_t pvr_vram_bytes;
    std::uint32_t pvr_register_writes;
    std::uint32_t pvr_scanline;
    std::uint64_t pvr_render_start_requests;
    std::uint32_t pvr_framebuffer_enabled;
    std::uint64_t pvr_ta_completed_lists;
    std::uint64_t pvr_ta_parameter_counts[8];
    std::uint64_t pvr_ta_list_packet_counts[5];
    std::uint32_t holly_normal_status;
    std::uint32_t holly_level2_mask;
    std::uint32_t holly_level4_mask;
    std::uint32_t holly_level6_mask;
    std::uint64_t interrupts_accepted;
    std::uint64_t indirect_calls;
    std::uint32_t last_indirect_target;
    std::uint32_t recent_indirect_targets[16];
    std::uint32_t r1;
    std::uint32_t r2;
    std::uint32_t r3;
    std::uint32_t r8;
    std::uint32_t r9;
    std::uint32_t r10;
    std::uint32_t r11;
    std::uint32_t r12;
    std::uint32_t r14;
    std::uint32_t r13;
    std::uint32_t r15;
    int stopped_by_budget;
    int execution_stopped;
    char error[192];
};

struct NaomiPvrFrameInfo {
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t control;
    std::uint32_t size;
    std::uint32_t start;
    std::size_t rgb_bytes;
};

extern "C" {
std::uint32_t mamerecomp_naomi_abi();
const NaomiProfileRecord* mamerecomp_naomi_profiles(std::size_t* count);
// Returns 1 when a translated game set is available and writes its SH-4 entry.
// `dispatcher` points to mamerecomp::naomi::Sh4Dispatcher in the C++ runtime ABI.
int mamerecomp_naomi_register_sh4(const char* set_id, void* dispatcher,
                                  std::uint32_t* entry);
void* mamerecomp_naomi_sh4_probe_create(const char* set_id, int interpreter_only);
void mamerecomp_naomi_sh4_probe_destroy(void* probe);
int mamerecomp_naomi_sh4_probe_start(void* probe, const std::uint8_t* rom,
                                     std::size_t rom_size,
                                     const std::uint8_t* board_id_eeprom,
                                     std::size_t board_id_eeprom_size,
                                     const std::uint8_t* bios,
                                     std::size_t bios_size,
                                     NaomiSh4ProbeResult* result);
int mamerecomp_naomi_sh4_probe_run(void* probe, std::uint64_t budget,
                                   NaomiSh4ProbeResult* result);
// Copies a diagnostic RGB readback of FB_R_SOF1 into caller-owned memory.
// A null RGB pointer reports the required capacity in frame_info.rgb_bytes.
int mamerecomp_naomi_sh4_probe_frame(void* probe, NaomiPvrFrameInfo* frame_info,
                                     std::uint8_t* rgb, std::size_t rgb_capacity);
}
