cmake_minimum_required(VERSION 3.20)
project(MultiArcadeRecomp LANGUAGES C CXX)
include(CTest)

# The emulator's runtime/video loops need optimization in ordinary single-config
# builds. Keep explicit Debug/RelWithDebInfo/Release choices from presets intact.
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(LibArchive REQUIRED)
find_package(ZLIB REQUIRED)
find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)
add_library(multiarcaderecomp_rom_hash STATIC core/roms/sha1.cpp)
target_include_directories(multiarcaderecomp_rom_hash PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core/roms)
set_property(TARGET multiarcaderecomp_rom_hash PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
option(MULTIARCADERECOMP_ENABLE_OPENGL_VIDEO "Build the optional OpenGL tilemap renderer" ON)
option(MULTIARCADERECOMP_ENABLE_VUNIT_OPENGL "Build the optional V-Unit OpenGL DMA rasterizer" OFF)
option(MULTIARCADERECOMP_ENABLE_PSX_GPU_OPENGL "Build the optional PSX GP0 OpenGL rasterizer" ON)
option(MULTIARCADERECOMP_ENABLE_WINDOW "Build SDL3 live window presentation when SDL3 is available" ON)
if(MULTIARCADERECOMP_ENABLE_WINDOW OR MULTIARCADERECOMP_ENABLE_OPENGL_VIDEO OR MULTIARCADERECOMP_ENABLE_VUNIT_OPENGL OR MULTIARCADERECOMP_ENABLE_PSX_GPU_OPENGL)
    find_package(SDL3 CONFIG QUIET)
endif()

set(MULTIARCADERECOMP_GENERATED_DIR "" CACHE PATH "Legacy: one game's m68krecomp output (prefer board-specific settings)")
set(MULTIARCADERECOMP_SYSTEM16A_GENERATED_DIRS "" CACHE STRING "Semicolon-separated System 16A game translation directories")
set(MULTIARCADERECOMP_SYSTEM16B_GENERATED_DIRS "" CACHE STRING "Semicolon-separated System 16B game translation directories")
set(MULTIARCADERECOMP_SYSTEM18_GENERATED_DIRS "" CACHE STRING "Semicolon-separated System 18 game translation directories")
set(MULTIARCADERECOMP_Z80_AOT_GENERATED_DIR "" CACHE PATH "Generated native Z80 source directory for one game")
set(MULTIARCADERECOMP_KONAMI_TMNT_AOT_DIR "" CACHE PATH "Generated TMNT 68000 and Z80 source directory")
set(MULTIARCADERECOMP_KONAMI_TMNT2_AOT_DIR "" CACHE PATH "Generated TMNT2-family 68000 and Z80 source directory")
set(MULTIARCADERECOMP_KONAMI_TMNT2_GAME_ID "tmnt2" CACHE STRING "TMNT2-family game identifier for generated module symbols")
set(MULTIARCADERECOMP_SEIBU_TOKI_AOT_DIR "" CACHE PATH "Generated Toki 68000 AOT sources")
set(MULTIARCADERECOMP_SEIBU_TOKI_Z80_AOT_DIR "" CACHE PATH "Generated Toki Z80 AOT sources")
set(MULTIARCADERECOMP_CPS1_M68K_GENERATED_DIR "" CACHE PATH "Generated CPS-1 68000 source directory for one game")
set(MULTIARCADERECOMP_CPS1_GAME_ID "unknown" CACHE STRING "CPS-1 game identifier used for generated game module output")
set(MULTIARCADERECOMP_NEOGEO_HARDWARE_MODE "aes" CACHE STRING "KOF98 hardware mode for this generated game module (aes or mvs)")
set_property(CACHE MULTIARCADERECOMP_NEOGEO_HARDWARE_MODE PROPERTY STRINGS aes mvs)
if(NOT MULTIARCADERECOMP_NEOGEO_HARDWARE_MODE MATCHES "^(aes|mvs)$")
    message(FATAL_ERROR "MULTIARCADERECOMP_NEOGEO_HARDWARE_MODE must be aes or mvs")
endif()
set(MULTIARCADERECOMP_NEOGEO_MODULE_ID "${MULTIARCADERECOMP_NEOGEO_HARDWARE_MODE}" CACHE STRING "KOF98 BIOS-specific module id")
set(MULTIARCADERECOMP_NEOGEO_GENERATED_DIR "${CMAKE_BINARY_DIR}/neogeo/kof98/${MULTIARCADERECOMP_NEOGEO_HARDWARE_MODE}/generated" CACHE PATH "Mode-specific KOF98 Neo Geo AOT output directory")
set(MULTIARCADERECOMP_NEOGEO_Z80_GENERATED_DIR "${CMAKE_BINARY_DIR}/neogeo/kof98/z80_generated" CACHE PATH "KOF98 Neo Geo Z80 AOT output root (BIOS and cart subdirectories)")
set(MULTIARCADERECOMP_TMS34010_AOT_SOURCE "" CACHE FILEPATH "Generated per-game TMS34010 AOT C++ source for the T-Unit board module")
set(MULTIARCADERECOMP_M6809_AOT_SOURCE "" CACHE FILEPATH "Generated per-game MC6809(E) sound CPU AOT C++ source")
set(MULTIARCADERECOMP_ADSP2105_AOT_SOURCE "" CACHE FILEPATH "Generated per-game ADSP-2105 sound CPU AOT C++ source")
set(MULTIARCADERECOMP_CRUSNUSA_ADSP_AOT_SOURCE "" CACHE FILEPATH "Generated Cruis'n USA ADSP-2105 AOT source")
set(MULTIARCADERECOMP_CRUSNUSA_C31_ROM_IMAGE "" CACHE FILEPATH "Cruis'n USA C31 low-RAM/reset image used to generate guarded AOT blocks")
set(MULTIARCADERECOMP_CRUSNUSA_C31_OVERLAY "" CACHE FILEPATH "Optional captured live C31 RAM PC/opcode pairs for guarded AOT translation")
set(MULTIARCADERECOMP_CRUSNUSA_DCS_ROM_IMAGE "" CACHE FILEPATH "Cruis'n USA DCS sound ROM region used to generate ADSP boot AOT blocks")
set(MULTIARCADERECOMP_MK2_TMS34010_AOT_SOURCE "" CACHE FILEPATH "Generated Mortal Kombat II TMS34010 AOT C++ source")
set(MULTIARCADERECOMP_KI_MIPS_AOT_DIR "" CACHE PATH "Generated per-game Killer Instinct MIPS III C source directory")
set(MULTIARCADERECOMP_KI_MIPS_GAME_ID "ki1" CACHE STRING "Game identifier for the linked Killer Instinct MIPS AOT tables")
set(MULTIARCADERECOMP_KI_ADSP_AOT_DIR "" CACHE PATH "Generated UltraCombo-format KI ADSP-2105 translation directory")
set(MULTIARCADERECOMP_NAOMI_TRANSLATED_SOURCES "" CACHE STRING "Semicolon-separated generated Naomi SH-4 C++ sources")
option(MULTIARCADERECOMP_NAOMI_HAS_BOOT_RAM_TRANSLATION "Build the captured, decrypted Naomi boot-RAM translation" OFF)
option(MULTIARCADERECOMP_NAOMI_HAS_MAIN_ROM_TRANSLATION "Build the MvC2 REIOS diagnostic game-ROM translation" OFF)
option(MULTIARCADERECOMP_NAOMI_HAS_OBSERVED_ROM_ROOTS "Build observed MvC2 ROM-backed dynamic roots" OFF)

add_executable(multiarcaderecomp
    src/main.cpp
    src/naomi/naomi_romset.cpp
    src/commands/machine_game_command.cpp
    src/commands/neogeo_commands.cpp
    src/commands/sega_commands.cpp
    src/commands/naomi_commands.cpp
    src/commands/konami/tmnt2_runner.cpp
    src/commands/konami/tmnt_runner.cpp
    src/commands/seibu/toki_runner.cpp
    src/commands/sony/zn_runner.cpp
)
set_property(TARGET multiarcaderecomp PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
target_include_directories(multiarcaderecomp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/sega
    ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/sega/system16)

# Shared PlayStation-derived GPU device. The command processor and VRAM are
# independent from the R3000A core so Sony-derived boards can select a common
# software or hardware raster backend.
add_library(multiarcaderecomp_psx_gpu STATIC
    core/cpus/psx/gpu.cpp)
target_include_directories(multiarcaderecomp_psx_gpu PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/core/cpus/psx)
set_property(TARGET multiarcaderecomp_psx_gpu PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
if(MULTIARCADERECOMP_ENABLE_PSX_GPU_OPENGL)
    find_package(OpenGL QUIET)
    if(SDL3_FOUND AND OpenGL_FOUND)
        target_sources(multiarcaderecomp_psx_gpu PRIVATE core/cpus/psx/gpu_hw.cpp)
        target_link_libraries(multiarcaderecomp_psx_gpu PRIVATE SDL3::SDL3 OpenGL::GL)
        configure_file(core/cpus/psx/gpu_rasterize.comp
            ${CMAKE_CURRENT_BINARY_DIR}/psx_gpu_rasterize.comp COPYONLY)
        configure_file(core/cpus/psx/gpu_scanout.comp
            ${CMAKE_CURRENT_BINARY_DIR}/psx_gpu_scanout.comp COPYONLY)
        configure_file(core/cpus/psx/gpu_draw.vert
            ${CMAKE_CURRENT_BINARY_DIR}/psx_gpu_draw.vert COPYONLY)
        configure_file(core/cpus/psx/gpu_draw.frag
            ${CMAKE_CURRENT_BINARY_DIR}/psx_gpu_draw.frag COPYONLY)
        configure_file(core/cpus/psx/gpu_batch.vert
            ${CMAKE_CURRENT_BINARY_DIR}/psx_gpu_batch.vert COPYONLY)
        configure_file(core/cpus/psx/gpu_batch.frag
            ${CMAKE_CURRENT_BINARY_DIR}/psx_gpu_batch.frag COPYONLY)
        configure_file(core/cpus/psx/gpu_batch_scanline.comp
            ${CMAKE_CURRENT_BINARY_DIR}/psx_gpu_batch_scanline.comp COPYONLY)
        configure_file(core/cpus/psx/gpu_texture_cache.comp
            ${CMAKE_CURRENT_BINARY_DIR}/psx_gpu_texture_cache.comp COPYONLY)
        configure_file(core/cpus/psx/gpu_polyline.comp
            ${CMAKE_CURRENT_BINARY_DIR}/psx_gpu_polyline.comp COPYONLY)
        target_compile_definitions(multiarcaderecomp_psx_gpu PRIVATE
            MULTIARCADERECOMP_PSX_GPU_OPENGL=1
            MULTIARCADERECOMP_PSX_GPU_SHADER_PATH="${CMAKE_CURRENT_BINARY_DIR}/psx_gpu_rasterize.comp"
            MULTIARCADERECOMP_PSX_GPU_SCANOUT_SHADER_PATH="${CMAKE_CURRENT_BINARY_DIR}/psx_gpu_scanout.comp"
            MULTIARCADERECOMP_PSX_GPU_VERTEX_SHADER_PATH="${CMAKE_CURRENT_BINARY_DIR}/psx_gpu_draw.vert"
            MULTIARCADERECOMP_PSX_GPU_FRAGMENT_SHADER_PATH="${CMAKE_CURRENT_BINARY_DIR}/psx_gpu_draw.frag"
            MULTIARCADERECOMP_PSX_GPU_BATCH_VERTEX_SHADER_PATH="${CMAKE_CURRENT_BINARY_DIR}/psx_gpu_batch.vert"
            MULTIARCADERECOMP_PSX_GPU_BATCH_FRAGMENT_SHADER_PATH="${CMAKE_CURRENT_BINARY_DIR}/psx_gpu_batch.frag"
            MULTIARCADERECOMP_PSX_GPU_BATCH_SCANLINE_SHADER_PATH="${CMAKE_CURRENT_BINARY_DIR}/psx_gpu_batch_scanline.comp"
            MULTIARCADERECOMP_PSX_GPU_TEXTURE_CACHE_SHADER_PATH="${CMAKE_CURRENT_BINARY_DIR}/psx_gpu_texture_cache.comp")
        target_compile_definitions(multiarcaderecomp_psx_gpu PRIVATE
            MULTIARCADERECOMP_PSX_GPU_POLYLINE_SHADER_PATH="${CMAKE_CURRENT_BINARY_DIR}/psx_gpu_polyline.comp")
        message(STATUS "PSX GPU OpenGL hardware rasterizer enabled")
    else()
        target_sources(multiarcaderecomp_psx_gpu PRIVATE core/cpus/psx/gpu_hw_stub.cpp)
        message(STATUS "SDL3/OpenGL development files not found; PSX hardware rasterizer unavailable")
    endif()
else()
    target_sources(multiarcaderecomp_psx_gpu PRIVATE core/cpus/psx/gpu_hw_stub.cpp)
endif()

# Shared PlayStation-derived R3000A pipeline helpers. The board bus, COP2/GTE,
# interpreter, and generated game modules are connected in later ZN stages.
add_library(multiarcaderecomp_psx_cpu STATIC
    core/cpus/psx/cat702.cpp
    core/cpus/psx/pipeline.cpp
    core/cpus/psx/interpreter.cpp
    core/cpus/psx/sio.cpp
    core/cpus/psx/spu.cpp
    core/cpus/psx/dma.cpp
    core/cpus/psx/gte.cpp
    core/cpus/psx/mdec_mame_port.cpp
    core/cpus/psx/timers.cpp
    core/cpus/psx/machine_memory.cpp)
target_include_directories(multiarcaderecomp_psx_cpu PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/core/cpus/psx)
target_link_libraries(multiarcaderecomp_psx_cpu PUBLIC multiarcaderecomp_psx_gpu)
set_property(TARGET multiarcaderecomp_psx_cpu PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)

# H8/300H interpreter shared by Namco System 12 and other compatible boards.
# MAME-generated handlers are kept separate from the board-specific register map.
add_library(multiarcaderecomp_h8 STATIC core/cpus/h8/h8_cpu.cpp)
target_include_directories(multiarcaderecomp_h8 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core/cpus/h8)

add_subdirectory(core/cpus/mcs48)
add_subdirectory(core/cpus/mcs51)
add_subdirectory(core/cpus/m68000/fd1094)
add_subdirectory(src/runtime/sega)

# PSXRecomp's static R3000A translator is built as a project tool. This keeps
# game-image translation available without requiring the user's separate
# checkout or Python; its generated CPU functions will be adapted to the
# MultiArcadeRecomp PSX runtime in the next integration layer.
set(FMT_TEST OFF CACHE BOOL "Disable vendored PSXRecomp fmt tests" FORCE)
set(FMT_DOC OFF CACHE BOOL "Disable vendored PSXRecomp fmt docs" FORCE)
set(FMT_INSTALL OFF CACHE BOOL "Disable vendored PSXRecomp fmt install" FORCE)
add_subdirectory(core/third_party/psxrecomp/lib/fmt
    ${CMAKE_CURRENT_BINARY_DIR}/third_party/psxrecomp-fmt EXCLUDE_FROM_ALL)
set(MULTIARCADERECOMP_PSXRECOMP_ROOT
    ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/psxrecomp)
add_library(multiarcaderecomp_psxrecomp_rabbitizer STATIC
    ${MULTIARCADERECOMP_PSXRECOMP_ROOT}/lib/rabbitizer/cplusplus/src/analysis/LoPairingInfo.cpp
    ${MULTIARCADERECOMP_PSXRECOMP_ROOT}/lib/rabbitizer/cplusplus/src/analysis/RegistersTracker.cpp
    ${MULTIARCADERECOMP_PSXRECOMP_ROOT}/lib/rabbitizer/cplusplus/src/instructions/InstrId.cpp
    ${MULTIARCADERECOMP_PSXRECOMP_ROOT}/lib/rabbitizer/cplusplus/src/instructions/InstrIdType.cpp
    ${MULTIARCADERECOMP_PSXRECOMP_ROOT}/lib/rabbitizer/cplusplus/src/instructions/InstructionBase.cpp
    ${MULTIARCADERECOMP_PSXRECOMP_ROOT}/lib/rabbitizer/cplusplus/src/instructions/InstructionCpu.cpp
    ${MULTIARCADERECOMP_PSXRECOMP_ROOT}/lib/rabbitizer/cplusplus/src/instructions/InstructionR3000GTE.cpp
    ${MULTIARCADERECOMP_PSXRECOMP_ROOT}/lib/rabbitizer/cplusplus/src/instructions/InstructionR5900.cpp
    ${MULTIARCADERECOMP_PSXRECOMP_ROOT}/lib/rabbitizer/cplusplus/src/instructions/InstructionRsp.cpp
    ${MULTIARCADERECOMP_PSXRECOMP_ROOT}/lib/rabbitizer/src/analysis/RabbitizerLoPairingInfo.c
    ${MULTIARCADERECOMP_PSXRECOMP_ROOT}/lib/rabbitizer/src/analysis/RabbitizerRegistersTracker.c
    ${MULTIARCADERECOMP_PSXRECOMP_ROOT}/lib/rabbitizer/src/analysis/RabbitizerTrackedRegisterState.c
    ${MULTIARCADERECOMP_PSXRECOMP_ROOT}/lib/rabbitizer/src/common/RabbitizerConfig.c
    ${MULTIARCADERECOMP_PSXRECOMP_ROOT}/lib/rabbitizer/src/common/RabbitizerVersion.c
    ${MULTIARCADERECOMP_PSXRECOMP_ROOT}/lib/rabbitizer/src/common/Utils.c
    ${MULTIARCADERECOMP_PSXRECOMP_ROOT}/lib/rabbitizer/src/instructions/RabbitizerInstrCategory.c
    ${MULTIARCADERECOMP_PSXRECOMP_ROOT}/lib/rabbitizer/src/instructions/RabbitizerInstrDescriptor.c
    ${MULTIARCADERECOMP_PSXRECOMP_ROOT}/lib/rabbitizer/src/instructions/RabbitizerInstrId.c
    ${MULTIARCADERECOMP_PSXRECOMP_ROOT}/lib/rabbitizer/src/instructions/RabbitizerInstrIdType.c
    ${MULTIARCADERECOMP_PSXRECOMP_ROOT}/lib/rabbitizer/src/instructions/RabbitizerInstrSuffix.c
    ${MULTIARCADERECOMP_PSXRECOMP_ROOT}/lib/rabbitizer/src/instructions/RabbitizerInstructionCpu/RabbitizerInstructionCpu_OperandType.c
    ${MULTIARCADERECOMP_PSXRECOMP_ROOT}/lib/rabbitizer/src/instructions/RabbitizerInstructionR3000GTE/RabbitizerInstructionR3000GTE.c
    ${MULTIARCADERECOMP_PSXRECOMP_ROOT}/lib/rabbitizer/src/instructions/RabbitizerInstructionR3000GTE/RabbitizerInstructionR3000GTE_OperandType.c
    ${MULTIARCADERECOMP_PSXRECOMP_ROOT}/lib/rabbitizer/src/instructions/RabbitizerInstructionR3000GTE/RabbitizerInstructionR3000GTE_ProcessUniqueId.c
    ${MULTIARCADERECOMP_PSXRECOMP_ROOT}/lib/rabbitizer/src/instructions/RabbitizerInstructionR5900/RabbitizerInstructionR5900.c
    ${MULTIARCADERECOMP_PSXRECOMP_ROOT}/lib/rabbitizer/src/instructions/RabbitizerInstructionR5900/RabbitizerInstructionR5900_OperandType.c
    ${MULTIARCADERECOMP_PSXRECOMP_ROOT}/lib/rabbitizer/src/instructions/RabbitizerInstructionR5900/RabbitizerInstructionR5900_ProcessUniqueId.c
    ${MULTIARCADERECOMP_PSXRECOMP_ROOT}/lib/rabbitizer/src/instructions/RabbitizerInstructionRsp/RabbitizerInstructionRsp.c
    ${MULTIARCADERECOMP_PSXRECOMP_ROOT}/lib/rabbitizer/src/instructions/RabbitizerInstructionRsp/RabbitizerInstructionRsp_OperandType.c
    ${MULTIARCADERECOMP_PSXRECOMP_ROOT}/lib/rabbitizer/src/instructions/RabbitizerInstructionRsp/RabbitizerInstructionRsp_ProcessUniqueId.c
    ${MULTIARCADERECOMP_PSXRECOMP_ROOT}/lib/rabbitizer/src/instructions/RabbitizerInstruction/RabbitizerInstruction.c
    ${MULTIARCADERECOMP_PSXRECOMP_ROOT}/lib/rabbitizer/src/instructions/RabbitizerInstruction/RabbitizerInstruction_Disassemble.c
    ${MULTIARCADERECOMP_PSXRECOMP_ROOT}/lib/rabbitizer/src/instructions/RabbitizerInstruction/RabbitizerInstruction_Examination.c
    ${MULTIARCADERECOMP_PSXRECOMP_ROOT}/lib/rabbitizer/src/instructions/RabbitizerInstruction/RabbitizerInstruction_Operand.c
    ${MULTIARCADERECOMP_PSXRECOMP_ROOT}/lib/rabbitizer/src/instructions/RabbitizerInstruction/RabbitizerInstruction_ProcessUniqueId.c
    ${MULTIARCADERECOMP_PSXRECOMP_ROOT}/lib/rabbitizer/src/instructions/RabbitizerRegister.c
    ${MULTIARCADERECOMP_PSXRECOMP_ROOT}/lib/rabbitizer/src/instructions/RabbitizerRegisterDescriptor.c)
target_include_directories(multiarcaderecomp_psxrecomp_rabbitizer PRIVATE
    ${MULTIARCADERECOMP_PSXRECOMP_ROOT}/lib/rabbitizer/include
    ${MULTIARCADERECOMP_PSXRECOMP_ROOT}/lib/rabbitizer/cplusplus/include
    ${MULTIARCADERECOMP_PSXRECOMP_ROOT}/lib/rabbitizer/tables)
add_executable(multiarcaderecomp_psxrecomp
    ${MULTIARCADERECOMP_PSXRECOMP_ROOT}/recompiler/src/ps1_exe_parser.cpp
    ${MULTIARCADERECOMP_PSXRECOMP_ROOT}/recompiler/src/mips_decoder.cpp
    ${MULTIARCADERECOMP_PSXRECOMP_ROOT}/recompiler/src/control_flow.cpp
    ${MULTIARCADERECOMP_PSXRECOMP_ROOT}/recompiler/src/function_analysis.cpp
    ${MULTIARCADERECOMP_PSXRECOMP_ROOT}/recompiler/src/code_generator.cpp
    ${MULTIARCADERECOMP_PSXRECOMP_ROOT}/recompiler/src/pgxp_hook_emitter.cpp
    ${MULTIARCADERECOMP_PSXRECOMP_ROOT}/recompiler/src/bios_address_model.cpp
    ${MULTIARCADERECOMP_PSXRECOMP_ROOT}/recompiler/src/annotations.cpp
    ${MULTIARCADERECOMP_PSXRECOMP_ROOT}/recompiler/src/config_loader.cpp
    ${MULTIARCADERECOMP_PSXRECOMP_ROOT}/recompiler/src/recompiler_patch.cpp
    ${MULTIARCADERECOMP_PSXRECOMP_ROOT}/recompiler/src/game_dispatch_emitter.cpp
    ${MULTIARCADERECOMP_PSXRECOMP_ROOT}/recompiler/src/segment_variants.cpp
    ${MULTIARCADERECOMP_PSXRECOMP_ROOT}/recompiler/src/main_psx.cpp)
target_include_directories(multiarcaderecomp_psxrecomp PRIVATE
    ${MULTIARCADERECOMP_PSXRECOMP_ROOT}/recompiler/include
    ${MULTIARCADERECOMP_PSXRECOMP_ROOT}/recompiler/src
    ${MULTIARCADERECOMP_PSXRECOMP_ROOT}/lib/rabbitizer/include
    ${MULTIARCADERECOMP_PSXRECOMP_ROOT}/lib/rabbitizer/cplusplus/include
    ${MULTIARCADERECOMP_PSXRECOMP_ROOT}/lib/fmt/include
    ${MULTIARCADERECOMP_PSXRECOMP_ROOT}/lib/toml11)
target_link_libraries(multiarcaderecomp_psxrecomp PRIVATE
    fmt multiarcaderecomp_psxrecomp_rabbitizer)
add_library(multiarcaderecomp_psxrecomp_bridge STATIC
    core/cpus/psx/psxrecomp_bridge.cpp)
target_include_directories(multiarcaderecomp_psxrecomp_bridge PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/core/cpus/psx
    ${CMAKE_CURRENT_SOURCE_DIR}/core/cpus/psx/psxrecomp_abi)
target_compile_definitions(multiarcaderecomp_psxrecomp_bridge
    INTERFACE MULTIARCADERECOMP_PSX_RUNTIME_ABI=1)
target_link_libraries(multiarcaderecomp_psxrecomp_bridge PUBLIC multiarcaderecomp_psx_cpu)

# ROM-set and board profile layer shared by the ZN-1/ZN-2 runtime adapters.
add_library(multiarcaderecomp_zn_runtime STATIC
    src/zn/zn_romset.cpp
    src/zn/zn_mcu.cpp
    src/zn/zn_runtime.cpp
    src/zn/zn2_sound.cpp
    src/zn/zn1_sound.cpp)
target_include_directories(multiarcaderecomp_zn_runtime PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/zn
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/core/cpus/psx
    ${CMAKE_CURRENT_SOURCE_DIR}/core/cpus/m68000)
target_include_directories(multiarcaderecomp_zn_runtime PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/z80-aot/sound)
target_link_libraries(multiarcaderecomp_zn_runtime PUBLIC
    LibArchive::LibArchive ZLIB::ZLIB multiarcaderecomp_psx_cpu multiarcaderecomp_z80_core multiarcaderecomp_qsound multiarcaderecomp_c352 multiarcaderecomp_at28c16 multiarcaderecomp_namco_system12_runtime)
set_property(TARGET multiarcaderecomp_zn_runtime PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)

set(MULTIARCADERECOMP_ZN2_SFEX2PA_AOT_DIR "" CACHE PATH
    "Generated SFEX2PA CPU AOT sources (tools/psx/build_zn2_aot.py output)")
if(MULTIARCADERECOMP_ZN2_SFEX2PA_AOT_DIR)
    file(GLOB MULTIARCADERECOMP_ZN2_SFEX2PA_Z80_SOURCES CONFIGURE_DEPENDS
        "${MULTIARCADERECOMP_ZN2_SFEX2PA_AOT_DIR}/z80/z80_*.cpp")
    if(NOT MULTIARCADERECOMP_ZN2_SFEX2PA_Z80_SOURCES OR
       NOT EXISTS "${MULTIARCADERECOMP_ZN2_SFEX2PA_AOT_DIR}/z80/z80_table.cpp")
        message(FATAL_ERROR "SFEX2PA Z80 AOT directory is incomplete: ${MULTIARCADERECOMP_ZN2_SFEX2PA_AOT_DIR}")
    endif()
    set(_zn2_adapter "${CMAKE_CURRENT_BINARY_DIR}/multiarcaderecomp_zn2_sfex2pa_aot.cpp")
    file(WRITE "${_zn2_adapter}"
        "#include <native_z80.h>\n#include <z80_decl.h>\n"
        "extern \"C\" const multiarcaderecomp::z80::Z80Code* multiarcaderecomp_zn2_sfex2pa_z80_code() { return &multiarcaderecomp::z80::kZ80Code; }\n")
    set(_zn2_psx_program "${MULTIARCADERECOMP_ZN2_SFEX2PA_AOT_DIR}/psx/psx_program.cpp")
    set(_zn2_psx_sources)
    if(EXISTS "${_zn2_psx_program}")
        file(GLOB _zn2_psx_shards CONFIGURE_DEPENDS
            "${MULTIARCADERECOMP_ZN2_SFEX2PA_AOT_DIR}/psx/psx_program_*.cpp")
        list(APPEND _zn2_psx_sources "${_zn2_psx_program}")
        list(APPEND _zn2_psx_sources ${_zn2_psx_shards})
        file(APPEND "${_zn2_adapter}"
            "#include <cpus/psx/psx_aot_abi.h>\n"
            "extern const PsxAotEntry psx_aot_entries[]; extern const size_t psx_aot_entry_count;\n"
            "extern \"C\" void multiarcaderecomp_zn2_sfex2pa_psx_entries(const PsxAotEntry **entries, size_t *count) { *entries = psx_aot_entries; *count = psx_aot_entry_count; }\n")
    endif()
    if(MULTIARCADERECOMP_DSP16A_AOT_SOURCE)
        file(APPEND "${_zn2_adapter}"
            "#include <cpus/dsp16/dsp16a.hpp>\n"
            "extern \"C\" const multiarcaderecomp::dsp16::Dsp16ACompiledEntry* multiarcaderecomp_dsp16a_aot_entries(size_t*);\n"
            "extern \"C\" void multiarcaderecomp_zn2_sfex2pa_dsp16a_entries(const multiarcaderecomp::dsp16::Dsp16ACompiledEntry **entries, size_t *count) { *entries = multiarcaderecomp_dsp16a_aot_entries(count); }\n"
            "extern \"C\" std::uint64_t multiarcaderecomp_zn2_sfex2pa_dsp16a_run(multiarcaderecomp::dsp16::Dsp16A& dsp, std::uint64_t clocks) { return dsp.run_clocks(clocks); }\n")
    endif()
    add_library(multiarcaderecomp_zn2_sfex2pa MODULE
        ${MULTIARCADERECOMP_ZN2_SFEX2PA_Z80_SOURCES}
        "${MULTIARCADERECOMP_ZN2_SFEX2PA_AOT_DIR}/z80/z80_table.cpp"
        ${_zn2_psx_sources} "${_zn2_adapter}")
    if(MULTIARCADERECOMP_DSP16A_AOT_SOURCE)
        target_sources(multiarcaderecomp_zn2_sfex2pa PRIVATE "${MULTIARCADERECOMP_DSP16A_AOT_SOURCE}")
        target_link_libraries(multiarcaderecomp_zn2_sfex2pa PRIVATE multiarcaderecomp_dsp16)
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            # Keep DSP16A execution and its generated handlers in one LTO
            # partition, while leaving the large R3000A shards out of LTO.
            set_source_files_properties(
                "${MULTIARCADERECOMP_DSP16A_AOT_SOURCE}" "${_zn2_adapter}"
                PROPERTIES COMPILE_OPTIONS "-flto")
            target_link_options(multiarcaderecomp_zn2_sfex2pa PRIVATE "-flto")
        endif()
    endif()
    target_include_directories(multiarcaderecomp_zn2_sfex2pa PRIVATE
        "${MULTIARCADERECOMP_ZN2_SFEX2PA_AOT_DIR}/z80"
        "${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/z80-aot/runtime"
        "${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/z80-aot/sound"
        "${CMAKE_CURRENT_SOURCE_DIR}/core"
        "${CMAKE_CURRENT_SOURCE_DIR}/core/cpus/dsp16")
    target_link_libraries(multiarcaderecomp_zn2_sfex2pa PRIVATE multiarcaderecomp_z80_core)
    if(_zn2_psx_sources)
        target_link_libraries(multiarcaderecomp_zn2_sfex2pa PRIVATE multiarcaderecomp_psx_cpu)
        target_compile_definitions(multiarcaderecomp_zn2_sfex2pa PRIVATE
            MULTIARCADERECOMP_PSX_AOT_INLINE_MEMORY=1
            MULTIARCADERECOMP_PSX_AOT_INLINE_PIPELINE=1)
        # The module contains both the generated blocks and the PSX runtime.
        # Bind calls between their own runtime helpers locally instead of
        # routing every memory/exception helper through ELF PLT stubs.
        if(UNIX AND NOT APPLE)
            target_link_options(multiarcaderecomp_zn2_sfex2pa PRIVATE
                "-Wl,-Bsymbolic-functions")
        endif()
    endif()
    set_target_properties(multiarcaderecomp_zn2_sfex2pa PROPERTIES
        PREFIX "" SUFFIX ".so" OUTPUT_NAME multiarcaderecomp_zn2_sfex2pa
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}" BUILD_RPATH "$ORIGIN")
endif()

# Shared address-decoder and direct-ZIP ROM layer for TMNT2/Sunset Riders.
add_library(multiarcaderecomp_serial_eeprom STATIC core/devices/serial_eeprom.cpp)
target_include_directories(multiarcaderecomp_serial_eeprom PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core)
add_library(multiarcaderecomp_at28c16 STATIC core/devices/at28c16.cpp)
target_include_directories(multiarcaderecomp_at28c16 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core)

add_library(multiarcaderecomp_c352 STATIC core/chips/c352.cpp)
target_include_directories(multiarcaderecomp_c352 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core)

# Namco System 12 board wiring is independent from the shared PSX-derived SoC
# and from the ZN-1/ZN-2 board adapters.
add_library(multiarcaderecomp_namco_system12_board STATIC
    src/boards/namco/system12/system12_board.cpp)
target_include_directories(multiarcaderecomp_namco_system12_board PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/core/cpus/psx)
target_link_libraries(multiarcaderecomp_namco_system12_board PUBLIC multiarcaderecomp_c352)
add_library(multiarcaderecomp_namco_system12_runtime STATIC
    src/runtime/namco/system12/system12_runtime.cpp)
target_include_directories(multiarcaderecomp_namco_system12_runtime PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/core/cpus/h8
    ${CMAKE_CURRENT_SOURCE_DIR}/core/cpus/psx)
target_link_libraries(multiarcaderecomp_namco_system12_runtime PUBLIC
    multiarcaderecomp_namco_system12_board multiarcaderecomp_h8)

add_library(multiarcaderecomp_ymf271 STATIC core/chips/ymf271.cpp)
target_include_directories(multiarcaderecomp_ymf271 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core/chips)
target_link_libraries(multiarcaderecomp_zn_runtime PUBLIC multiarcaderecomp_ymf271)

add_library(multiarcaderecomp_konami_tmnt2 STATIC
    src/konami/tmnt2_machine.cpp
    src/konami/tmnt2_m68k.cpp
    src/konami/tmnt2_z80.cpp)
target_include_directories(multiarcaderecomp_konami_tmnt2 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/konami
    ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/z80-aot/sound
    ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/ymfm)
target_link_libraries(multiarcaderecomp_konami_tmnt2 PUBLIC
    LibArchive::LibArchive ZLIB::ZLIB multiarcaderecomp_z80_core multiarcaderecomp_serial_eeprom ymfm_opm
    m68k_cpu_runtime multiarcaderecomp_m68k_recomp_runtime multiarcaderecomp_m68k_musashi)

# Original 1989 TMNT ROM geometry/loader. Its board runtime is implemented
# independently from the later TMNT2/Sunset Riders hardware.
add_library(multiarcaderecomp_konami_tmnt STATIC
    src/konami/tmnt_machine.cpp
    src/konami/tmnt_z80.cpp)
target_include_directories(multiarcaderecomp_konami_tmnt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/konami)
target_include_directories(multiarcaderecomp_konami_tmnt PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/z80-aot/sound
    ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/ymfm)
target_link_libraries(multiarcaderecomp_konami_tmnt PUBLIC
    LibArchive::LibArchive ZLIB::ZLIB multiarcaderecomp_z80_core ymfm_opm
    m68k_cpu_runtime multiarcaderecomp_m68k_recomp_runtime multiarcaderecomp_m68k_musashi)

if(MULTIARCADERECOMP_KONAMI_TMNT_AOT_DIR)
    file(GLOB MULTIARCADERECOMP_KONAMI_TMNT_M68K_SOURCES CONFIGURE_DEPENDS
        "${MULTIARCADERECOMP_KONAMI_TMNT_AOT_DIR}/m68k/*.c")
    file(GLOB MULTIARCADERECOMP_KONAMI_TMNT_Z80_SOURCES CONFIGURE_DEPENDS
        "${MULTIARCADERECOMP_KONAMI_TMNT_AOT_DIR}/z80/z80_*.cpp")
    if(NOT MULTIARCADERECOMP_KONAMI_TMNT_M68K_SOURCES OR
       NOT EXISTS "${MULTIARCADERECOMP_KONAMI_TMNT_AOT_DIR}/z80/z80_table.cpp")
        message(FATAL_ERROR "TMNT AOT generated directory is incomplete: ${MULTIARCADERECOMP_KONAMI_TMNT_AOT_DIR}")
    endif()
    add_library(multiarcaderecomp_konami_tmnt_aot SHARED
        src/konami/tmnt_aot_module.cpp
        ${MULTIARCADERECOMP_KONAMI_TMNT_M68K_SOURCES}
        ${MULTIARCADERECOMP_KONAMI_TMNT_Z80_SOURCES}
        "${MULTIARCADERECOMP_KONAMI_TMNT_AOT_DIR}/z80/z80_table.cpp")
    target_include_directories(multiarcaderecomp_konami_tmnt_aot PRIVATE
        "${MULTIARCADERECOMP_KONAMI_TMNT_AOT_DIR}/z80"
        ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/m68krecomp/include
        ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/z80-aot/runtime
        ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/z80-aot/sound)
    target_link_libraries(multiarcaderecomp_konami_tmnt_aot PRIVATE
        m68k_cpu_runtime multiarcaderecomp_m68k_recomp_runtime multiarcaderecomp_z80_core)
    set_target_properties(multiarcaderecomp_konami_tmnt_aot PROPERTIES
        OUTPUT_NAME multiarcaderecomp_konami_tmnt
        PREFIX ""
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        BUILD_RPATH "$ORIGIN")
endif()

if(MULTIARCADERECOMP_KONAMI_TMNT2_AOT_DIR)
    file(GLOB MULTIARCADERECOMP_KONAMI_TMNT2_M68K_SOURCES CONFIGURE_DEPENDS
        "${MULTIARCADERECOMP_KONAMI_TMNT2_AOT_DIR}/m68k/*.c")
    file(GLOB MULTIARCADERECOMP_KONAMI_TMNT2_Z80_SOURCES CONFIGURE_DEPENDS
        "${MULTIARCADERECOMP_KONAMI_TMNT2_AOT_DIR}/z80/z80_*.cpp")
    if(NOT MULTIARCADERECOMP_KONAMI_TMNT2_M68K_SOURCES OR
       NOT EXISTS "${MULTIARCADERECOMP_KONAMI_TMNT2_AOT_DIR}/z80/z80_table.cpp")
        message(FATAL_ERROR "TMNT2-family AOT generated directory is incomplete: ${MULTIARCADERECOMP_KONAMI_TMNT2_AOT_DIR}")
    endif()
    add_library(multiarcaderecomp_konami_tmnt2_aot SHARED
        src/konami/tmnt2_aot_module.cpp
        ${MULTIARCADERECOMP_KONAMI_TMNT2_M68K_SOURCES}
        ${MULTIARCADERECOMP_KONAMI_TMNT2_Z80_SOURCES}
        "${MULTIARCADERECOMP_KONAMI_TMNT2_AOT_DIR}/z80/z80_table.cpp")
    target_include_directories(multiarcaderecomp_konami_tmnt2_aot PRIVATE
        "${MULTIARCADERECOMP_KONAMI_TMNT2_AOT_DIR}/z80"
        ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/m68krecomp/include
        ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/z80-aot/runtime
        ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/z80-aot/sound)
    target_compile_definitions(multiarcaderecomp_konami_tmnt2_aot PRIVATE
        MULTIARCADERECOMP_KONAMI_TMNT2_GAME_ID=${MULTIARCADERECOMP_KONAMI_TMNT2_GAME_ID})
    target_link_libraries(multiarcaderecomp_konami_tmnt2_aot PRIVATE
        m68k_cpu_runtime multiarcaderecomp_m68k_recomp_runtime multiarcaderecomp_z80_core)
    set_target_properties(multiarcaderecomp_konami_tmnt2_aot PROPERTIES
        OUTPUT_NAME "multiarcaderecomp_konami_${MULTIARCADERECOMP_KONAMI_TMNT2_GAME_ID}"
        PREFIX ""
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        BUILD_RPATH "$ORIGIN")
endif()

# Clocked uPD7759 is shared between the FIFO-attached System 16B boards and
# Konami boards with a directly attached sample ROM.
add_library(multiarcaderecomp_upd7759 STATIC core/audio/upd7759.cpp)
target_include_directories(multiarcaderecomp_upd7759 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core/audio)
target_link_libraries(multiarcaderecomp_konami_tmnt PUBLIC multiarcaderecomp_upd7759)

# TAD Toki board runtime. It shares generic CPU cores, but owns its original
# Seibu/TAD address map, video hardware and encrypted Z80 interface.
add_library(multiarcaderecomp_oki6295 STATIC core/chips/oki6295.cpp)
target_include_directories(multiarcaderecomp_oki6295 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core)

add_library(multiarcaderecomp_seibu_toki STATIC
    src/seibu/toki_machine.cpp
    src/seibu/toki_m68k.cpp
    src/seibu/toki_z80.cpp)
target_include_directories(multiarcaderecomp_seibu_toki PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/z80-aot/sound
    ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/m68krecomp/include)
target_link_libraries(multiarcaderecomp_seibu_toki PUBLIC
    LibArchive::LibArchive ZLIB::ZLIB m68k_cpu_runtime
    multiarcaderecomp_m68k_recomp_runtime multiarcaderecomp_m68k_musashi multiarcaderecomp_z80_core ymfm_opl
    multiarcaderecomp_oki6295)
if(MULTIARCADERECOMP_SEIBU_TOKI_AOT_DIR AND MULTIARCADERECOMP_SEIBU_TOKI_Z80_AOT_DIR)
    file(GLOB MULTIARCADERECOMP_SEIBU_TOKI_M68K_SOURCES CONFIGURE_DEPENDS
        "${MULTIARCADERECOMP_SEIBU_TOKI_AOT_DIR}/*.c")
    file(GLOB MULTIARCADERECOMP_SEIBU_TOKI_Z80_SOURCES CONFIGURE_DEPENDS
        "${MULTIARCADERECOMP_SEIBU_TOKI_Z80_AOT_DIR}/z80_*.cpp")
    if(NOT MULTIARCADERECOMP_SEIBU_TOKI_M68K_SOURCES OR
       NOT MULTIARCADERECOMP_SEIBU_TOKI_Z80_SOURCES OR
       NOT EXISTS "${MULTIARCADERECOMP_SEIBU_TOKI_Z80_AOT_DIR}/z80_table.cpp")
        message(FATAL_ERROR "Toki AOT generated directory contains no 68000 C sources: ${MULTIARCADERECOMP_SEIBU_TOKI_AOT_DIR}")
    endif()
    add_library(multiarcaderecomp_seibu_toki_aot SHARED
        src/seibu/toki_aot_module.cpp ${MULTIARCADERECOMP_SEIBU_TOKI_M68K_SOURCES}
        ${MULTIARCADERECOMP_SEIBU_TOKI_Z80_SOURCES}
        "${MULTIARCADERECOMP_SEIBU_TOKI_Z80_AOT_DIR}/z80_table.cpp")
    target_include_directories(multiarcaderecomp_seibu_toki_aot PRIVATE
        ${MULTIARCADERECOMP_SEIBU_TOKI_AOT_DIR}
        ${MULTIARCADERECOMP_SEIBU_TOKI_Z80_AOT_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/z80-aot/runtime
        ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/m68krecomp/include)
    target_link_libraries(multiarcaderecomp_seibu_toki_aot PRIVATE m68k_cpu_runtime multiarcaderecomp_z80_core)
    set_target_properties(multiarcaderecomp_seibu_toki_aot PROPERTIES
        OUTPUT_NAME multiarcaderecomp_seibu_toki PREFIX ""
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}" BUILD_RPATH "$ORIGIN")
endif()

if(BUILD_TESTING)
    function(multiarcaderecomp_enable_test_asserts target)
        if(MSVC)
            target_compile_options(${target} PRIVATE /UNDEBUG)
        else()
            target_compile_options(${target} PRIVATE -UNDEBUG)
        endif()
    endfunction()
    add_test(NAME z80_aot_decoder_test
        COMMAND ${Python3_EXECUTABLE} -m unittest tests.z80_aot.test_isa)
    set_tests_properties(z80_aot_decoder_test PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME z80_aot_routine_grouping_test
        COMMAND ${Python3_EXECUTABLE} -m unittest tests.z80_aot.test_routines)
    set_tests_properties(z80_aot_routine_grouping_test PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../mame/roms/eswat.zip")
        add_executable(system16_fd1094_test tests/system16/fd1094_test.cpp)
        target_link_libraries(system16_fd1094_test PRIVATE multiarcaderecomp_fd1094 LibArchive::LibArchive)
        multiarcaderecomp_enable_test_asserts(system16_fd1094_test)
        add_test(NAME system16_fd1094_test COMMAND system16_fd1094_test
            "${CMAKE_CURRENT_SOURCE_DIR}/../mame/roms/eswat.zip")
    endif()
    add_executable(upd7759_test tests/audio/upd7759_test.cpp)
    target_link_libraries(upd7759_test PRIVATE multiarcaderecomp_upd7759)
    multiarcaderecomp_enable_test_asserts(upd7759_test)
    add_test(NAME upd7759_test COMMAND upd7759_test)
    add_executable(dsp16a_dau_test tests/audio/dsp16a_dau_test.cpp)
    target_link_libraries(dsp16a_dau_test PRIVATE multiarcaderecomp_dsp16)
    multiarcaderecomp_enable_test_asserts(dsp16a_dau_test)
    add_test(NAME dsp16a_dau_test COMMAND dsp16a_dau_test)
    add_executable(psx_pipeline_test tests/psx/pipeline_test.cpp)
    target_link_libraries(psx_pipeline_test PRIVATE multiarcaderecomp_psx_cpu)
    multiarcaderecomp_enable_test_asserts(psx_pipeline_test)
    add_test(NAME psx_pipeline_test COMMAND psx_pipeline_test)
    add_executable(psx_interpreter_test tests/psx/interpreter_test.cpp)
    target_link_libraries(psx_interpreter_test PRIVATE multiarcaderecomp_psx_cpu)
    multiarcaderecomp_enable_test_asserts(psx_interpreter_test)
    add_test(NAME psx_interpreter_test COMMAND psx_interpreter_test)
    add_executable(psx_dma_test tests/psx/dma_test.cpp)
    target_link_libraries(psx_dma_test PRIVATE multiarcaderecomp_psx_cpu)
    multiarcaderecomp_enable_test_asserts(psx_dma_test)
    add_test(NAME psx_dma_test COMMAND psx_dma_test)
    add_executable(psx_gpu_test tests/psx/gpu_test.cpp)
    target_link_libraries(psx_gpu_test PRIVATE multiarcaderecomp_psx_cpu)
    multiarcaderecomp_enable_test_asserts(psx_gpu_test)
    add_test(NAME psx_gpu_test COMMAND psx_gpu_test)
    if(MULTIARCADERECOMP_ENABLE_PSX_GPU_OPENGL AND SDL3_FOUND AND OpenGL_FOUND)
        add_executable(psx_gpu_hw_parity_test tests/psx/gpu_hw_parity_test.cpp)
        target_link_libraries(psx_gpu_hw_parity_test PRIVATE
            multiarcaderecomp_psx_cpu SDL3::SDL3 OpenGL::GL)
        multiarcaderecomp_enable_test_asserts(psx_gpu_hw_parity_test)
        add_test(NAME psx_gpu_hw_parity_test COMMAND psx_gpu_hw_parity_test)
        set_tests_properties(psx_gpu_hw_parity_test PROPERTIES SKIP_RETURN_CODE 77)
        add_executable(psx_gpu_hw_transfer_test tests/psx/gpu_hw_transfer_test.cpp)
        target_link_libraries(psx_gpu_hw_transfer_test PRIVATE
            multiarcaderecomp_psx_cpu SDL3::SDL3 OpenGL::GL)
        multiarcaderecomp_enable_test_asserts(psx_gpu_hw_transfer_test)
        add_test(NAME psx_gpu_hw_transfer_test COMMAND psx_gpu_hw_transfer_test)
        set_tests_properties(psx_gpu_hw_transfer_test PROPERTIES SKIP_RETURN_CODE 77)
    endif()
    add_executable(psx_sio_test tests/psx/sio_test.cpp)
    target_link_libraries(psx_sio_test PRIVATE multiarcaderecomp_psx_cpu)
    multiarcaderecomp_enable_test_asserts(psx_sio_test)
    add_test(NAME psx_sio_test COMMAND psx_sio_test)
    add_executable(psx_rom_bus_test tests/psx/rom_bus_test.cpp)
    target_link_libraries(psx_rom_bus_test PRIVATE multiarcaderecomp_psx_cpu)
    multiarcaderecomp_enable_test_asserts(psx_rom_bus_test)
    add_test(NAME psx_rom_bus_test COMMAND psx_rom_bus_test)
    add_executable(zn_mcu_test tests/zn/zn_mcu_test.cpp src/zn/zn_mcu.cpp)
    target_include_directories(zn_mcu_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/zn)
    multiarcaderecomp_enable_test_asserts(zn_mcu_test)
    add_test(NAME zn_mcu_test COMMAND zn_mcu_test)
    add_executable(zn_board_test tests/zn/board_test.cpp)
    target_link_libraries(zn_board_test PRIVATE multiarcaderecomp_zn_runtime)
    multiarcaderecomp_enable_test_asserts(zn_board_test)
    add_test(NAME zn_board_test COMMAND zn_board_test)
    add_executable(zn2_sound_timing_test tests/zn/zn2_sound_timing_test.cpp)
    target_include_directories(zn2_sound_timing_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/zn)
    multiarcaderecomp_enable_test_asserts(zn2_sound_timing_test)
    add_test(NAME zn2_sound_timing_test COMMAND zn2_sound_timing_test)
    add_executable(zn_boot_smoke tools/psx/zn_boot_smoke.cpp)
    target_link_libraries(zn_boot_smoke PRIVATE multiarcaderecomp_zn_runtime m68k_cpu_runtime)
    add_executable(konami_tmnt2_board_test tests/konami/tmnt2_board_test.cpp)
    target_link_libraries(konami_tmnt2_board_test PRIVATE multiarcaderecomp_konami_tmnt2 multiarcaderecomp_neogeo)
    multiarcaderecomp_enable_test_asserts(konami_tmnt2_board_test)
    add_test(NAME konami_tmnt2_board_test COMMAND konami_tmnt2_board_test)
    add_executable(konami_tmnt2_cpu_test tests/konami/tmnt2_cpu_test.cpp)
    target_link_libraries(konami_tmnt2_cpu_test PRIVATE multiarcaderecomp_konami_tmnt2 multiarcaderecomp_neogeo)
    multiarcaderecomp_enable_test_asserts(konami_tmnt2_cpu_test)
    add_test(NAME konami_tmnt2_cpu_test COMMAND konami_tmnt2_cpu_test)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../mame/roms/tmnt.zip")
        add_executable(konami_tmnt_rom_test tests/konami/tmnt_rom_test.cpp)
        target_link_libraries(konami_tmnt_rom_test PRIVATE multiarcaderecomp_konami_tmnt multiarcaderecomp_neogeo)
        multiarcaderecomp_enable_test_asserts(konami_tmnt_rom_test)
        add_test(NAME konami_tmnt_rom_test COMMAND konami_tmnt_rom_test
            "${CMAKE_CURRENT_SOURCE_DIR}/../mame/roms/tmnt.zip")
    endif()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../mame/roms/tmnt2.zip")
        add_executable(konami_tmnt2_rom_test tests/konami/tmnt2_rom_test.cpp)
        target_link_libraries(konami_tmnt2_rom_test PRIVATE multiarcaderecomp_konami_tmnt2 multiarcaderecomp_neogeo)
        multiarcaderecomp_enable_test_asserts(konami_tmnt2_rom_test)
        add_test(NAME konami_tmnt2_rom_test COMMAND konami_tmnt2_rom_test
            "${CMAKE_CURRENT_SOURCE_DIR}/../mame/roms/tmnt2.zip"
            "${CMAKE_CURRENT_SOURCE_DIR}/../mame/roms/ssriders.zip")
    endif()
    add_test(NAME psx_recompiler_test
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/psx/test_recomp.py)
endif()

# Midway T-Unit board adapter for the shared TMS34010 core. The generated
# per-game CPU tables will be linked by the Mortal Kombat build pipeline;
# board memory, VRAM, I/O registers and graphics-ROM windows stay in this SO.
add_library(multiarcaderecomp_tms34010 SHARED
    core/cpus/tms34010/interpreter.cpp
    core/cpus/tms34010/mame_interpreter.cpp)
target_include_directories(multiarcaderecomp_tms34010 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/core)
set_target_properties(multiarcaderecomp_tms34010 PROPERTIES
    OUTPUT_NAME multiarcaderecomp_cpu_tms34010
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_library(multiarcaderecomp_tunit SHARED
    core/cpus/interpreter_trace.cpp
    src/mk/tunit.cpp
    src/mk/williams_adpcm.cpp
    src/mk/mk_machine.cpp)
if(MULTIARCADERECOMP_TMS34010_AOT_SOURCE)
    if(NOT EXISTS "${MULTIARCADERECOMP_TMS34010_AOT_SOURCE}")
        message(FATAL_ERROR "TMS34010 AOT source does not exist: ${MULTIARCADERECOMP_TMS34010_AOT_SOURCE}")
    endif()
    target_sources(multiarcaderecomp_tunit PRIVATE "${MULTIARCADERECOMP_TMS34010_AOT_SOURCE}")
    target_compile_definitions(multiarcaderecomp_tunit PUBLIC MULTIARCADERECOMP_TMS34010_HAS_AOT=1)

    # Bounded developer runner for tracing reachable MK code and inspecting a
    # post-run T-Unit VRAM snapshot. This is not the interactive machine app.
    add_executable(multiarcaderecomp_mk_trace tools/mk/trace.cpp)
    target_link_libraries(multiarcaderecomp_mk_trace PRIVATE multiarcaderecomp_tunit)
    set_target_properties(multiarcaderecomp_mk_trace PROPERTIES
        BUILD_RPATH "$ORIGIN")
endif()
target_include_directories(multiarcaderecomp_tunit PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/core)
set_target_properties(multiarcaderecomp_tunit PROPERTIES
    OUTPUT_NAME multiarcaderecomp_tunit
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
target_link_libraries(multiarcaderecomp_tunit PRIVATE ymfm_opm multiarcaderecomp_tms34010)
set_target_properties(multiarcaderecomp_tunit PROPERTIES BUILD_RPATH "$ORIGIN")

# Shared MC6809/MC6809E fallback backend. It contains CPU semantics only;
# the MK sound board remains in multiarcaderecomp_tunit and is accessed via a bus.
add_library(multiarcaderecomp_m6809 STATIC core/cpus/m6809/interpreter.cpp)
target_include_directories(multiarcaderecomp_m6809 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/core)
if(TARGET multiarcaderecomp_mk_trace)
    target_link_libraries(multiarcaderecomp_mk_trace PRIVATE multiarcaderecomp_m6809)
endif()

# Shared ADSP-2105 CPU backend for DCS audio boards. Keep the DCS host protocol,
# ROM banking and DAC runtime in the board module, outside this CPU library.
if(MULTIARCADERECOMP_ADSP2105_AOT_SOURCE)
    if(NOT EXISTS "${MULTIARCADERECOMP_ADSP2105_AOT_SOURCE}")
        message(FATAL_ERROR "ADSP-2105 AOT source does not exist: ${MULTIARCADERECOMP_ADSP2105_AOT_SOURCE}")
    endif()
endif()
add_library(multiarcaderecomp_adsp2105 STATIC
    core/cpus/adsp2105/interpreter.cpp
    core/cpus/adsp2105/runtime.cpp)
target_include_directories(multiarcaderecomp_adsp2105 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/core)

# Shared Midway DCS Audio 2K host/audio runtime. The ADSP instruction core is
# generic; this layer owns DCS boot ROM extraction, latches, banking and SPORT.
add_library(multiarcaderecomp_dcs_audio_2k STATIC core/chips/dcs/audio_2k.cpp)
target_include_directories(multiarcaderecomp_dcs_audio_2k PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/core)
target_link_libraries(multiarcaderecomp_dcs_audio_2k PUBLIC multiarcaderecomp_adsp2105)

# Shared TI TMS320C31 interpreter backend. Board maps and video/DCS behavior
# belong to V-Unit; instruction semantics are kept in this CPU library.
if(MULTIARCADERECOMP_CRUSNUSA_C31_ROM_IMAGE)
    if(NOT EXISTS "${MULTIARCADERECOMP_CRUSNUSA_C31_ROM_IMAGE}")
        message(FATAL_ERROR "Cruis'n USA C31 image does not exist: ${MULTIARCADERECOMP_CRUSNUSA_C31_ROM_IMAGE}")
    endif()
    set(MULTIARCADERECOMP_CRUSNUSA_C31_AOT_SOURCE
        "${CMAKE_BINARY_DIR}/generated/vunit/crusnusa_c31_aot.cpp")
    set(MULTIARCADERECOMP_CRUSNUSA_C31_AOT_SHARDS)
    set(MULTIARCADERECOMP_CRUSNUSA_C31_AOT_OUTPUTS
        "${MULTIARCADERECOMP_CRUSNUSA_C31_AOT_SOURCE}")
    foreach(_c31_page RANGE 0 31)
        if(_c31_page LESS 10)
            set(_c31_page_name "0${_c31_page}")
        else()
            set(_c31_page_name "${_c31_page}")
        endif()
        list(APPEND MULTIARCADERECOMP_CRUSNUSA_C31_AOT_SHARDS
            "${CMAKE_BINARY_DIR}/generated/vunit/crusnusa_c31_aot_shard_${_c31_page_name}.cpp")
    endforeach()
    list(APPEND MULTIARCADERECOMP_CRUSNUSA_C31_AOT_SHARDS
        "${CMAKE_BINARY_DIR}/generated/vunit/crusnusa_c31_aot_extra.cpp")
    list(APPEND MULTIARCADERECOMP_CRUSNUSA_C31_AOT_OUTPUTS
        ${MULTIARCADERECOMP_CRUSNUSA_C31_AOT_SHARDS})
    set(MULTIARCADERECOMP_CRUSNUSA_C31_RECOMP_ARGS)
    set(MULTIARCADERECOMP_CRUSNUSA_C31_RECOMP_DEPENDS)
    if(MULTIARCADERECOMP_CRUSNUSA_C31_OVERLAY)
        if(NOT EXISTS "${MULTIARCADERECOMP_CRUSNUSA_C31_OVERLAY}")
            message(FATAL_ERROR "Cruis'n USA C31 overlay does not exist: ${MULTIARCADERECOMP_CRUSNUSA_C31_OVERLAY}")
        endif()
        list(APPEND MULTIARCADERECOMP_CRUSNUSA_C31_RECOMP_ARGS --overlay
             "${MULTIARCADERECOMP_CRUSNUSA_C31_OVERLAY}")
        list(APPEND MULTIARCADERECOMP_CRUSNUSA_C31_RECOMP_DEPENDS
             "${MULTIARCADERECOMP_CRUSNUSA_C31_OVERLAY}")
    endif()
    add_custom_command(
        OUTPUT ${MULTIARCADERECOMP_CRUSNUSA_C31_AOT_OUTPUTS}
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/tms320c31/recomp.py"
                "${MULTIARCADERECOMP_CRUSNUSA_C31_ROM_IMAGE}" "${MULTIARCADERECOMP_CRUSNUSA_C31_AOT_SOURCE}"
                --ops-source "${CMAKE_CURRENT_SOURCE_DIR}/core/cpus/tms320c31/mame_source/320c3x_ops.ipp"
                ${MULTIARCADERECOMP_CRUSNUSA_C31_RECOMP_ARGS}
        DEPENDS "${MULTIARCADERECOMP_CRUSNUSA_C31_ROM_IMAGE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/tms320c31/recomp.py"
                "${CMAKE_CURRENT_SOURCE_DIR}/core/cpus/tms320c31/mame_source/320c3x_ops.ipp"
                ${MULTIARCADERECOMP_CRUSNUSA_C31_RECOMP_DEPENDS}
        VERBATIM)
else()
    set(MULTIARCADERECOMP_CRUSNUSA_C31_AOT_SOURCE
        "${CMAKE_CURRENT_SOURCE_DIR}/core/cpus/tms320c31/aot_stub.cpp")
endif()
add_library(multiarcaderecomp_tms320c31 STATIC
    core/cpus/tms320c31/mame_interpreter.cpp
    ${MULTIARCADERECOMP_CRUSNUSA_C31_AOT_SHARDS}
    "${MULTIARCADERECOMP_CRUSNUSA_C31_AOT_SOURCE}")
if(MULTIARCADERECOMP_CRUSNUSA_C31_ROM_IMAGE)
    # Generated C31 AOT blocks are split into PC shards so the hot dispatch
    # path can use the same optimization level as the Release runtime.
    set_source_files_properties(${MULTIARCADERECOMP_CRUSNUSA_C31_AOT_SHARDS}
        "${MULTIARCADERECOMP_CRUSNUSA_C31_AOT_SOURCE}"
        PROPERTIES COMPILE_OPTIONS "-O3;-fno-strict-aliasing")
endif()
target_include_directories(multiarcaderecomp_tms320c31 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/core)

# MK2 gets an isolated T-Unit module so its generated TMS table and DCS audio
# adapter do not replace or alter the MK1 prototype's AOT module.
if(MULTIARCADERECOMP_MK2_TMS34010_AOT_SOURCE AND
   NOT EXISTS "${MULTIARCADERECOMP_MK2_TMS34010_AOT_SOURCE}")
    message(FATAL_ERROR "MK2 TMS34010 AOT source does not exist: ${MULTIARCADERECOMP_MK2_TMS34010_AOT_SOURCE}")
endif()
if(MULTIARCADERECOMP_MK2_TMS34010_AOT_SOURCE)
    set(MULTIARCADERECOMP_MK2_TMS_AOT_INPUT "${MULTIARCADERECOMP_MK2_TMS34010_AOT_SOURCE}")
else()
    set(MULTIARCADERECOMP_MK2_TMS_AOT_INPUT core/cpus/tms34010/aot_stub.cpp)
endif()
if(MULTIARCADERECOMP_ADSP2105_AOT_SOURCE)
    set(MULTIARCADERECOMP_MK2_ADSP_AOT_INPUT "${MULTIARCADERECOMP_ADSP2105_AOT_SOURCE}")
else()
    set(MULTIARCADERECOMP_MK2_ADSP_AOT_INPUT core/cpus/adsp2105/aot_stub.cpp)
endif()
add_library(multiarcaderecomp_tunit_mk2 SHARED
    ${MULTIARCADERECOMP_MK2_TMS_AOT_INPUT}
    core/cpus/interpreter_trace.cpp
    src/mk/tunit.cpp
    src/mk2/protection.cpp
    src/mk2/mk2_machine.cpp
    ${MULTIARCADERECOMP_MK2_ADSP_AOT_INPUT})
if(MULTIARCADERECOMP_MK2_TMS34010_AOT_SOURCE)
    target_compile_definitions(multiarcaderecomp_tunit_mk2 PUBLIC
        MULTIARCADERECOMP_TMS34010_HAS_AOT=1)
endif()
target_include_directories(multiarcaderecomp_tunit_mk2 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/core)
target_link_libraries(multiarcaderecomp_tunit_mk2 PRIVATE
    multiarcaderecomp_dcs_audio_2k multiarcaderecomp_tms34010)
set_target_properties(multiarcaderecomp_tunit_mk2 PROPERTIES
    OUTPUT_NAME multiarcaderecomp_tunit_mk2
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
add_executable(multiarcaderecomp_mk2_trace tools/mk2/trace.cpp)
target_link_libraries(multiarcaderecomp_mk2_trace PRIVATE multiarcaderecomp_tunit_mk2 multiarcaderecomp_tms34010)
set_target_properties(multiarcaderecomp_mk2_trace PROPERTIES BUILD_RPATH "$ORIGIN")

# Midway V-Unit prototype module. C31 and ADSP-2105 instruction backends are
# shared CPUs; V-Unit bus, ADC, DMA/video, controls and CMOS stay board-local.
if(NOT MULTIARCADERECOMP_CRUSNUSA_ADSP_AOT_SOURCE AND MULTIARCADERECOMP_CRUSNUSA_DCS_ROM_IMAGE)
    if(NOT EXISTS "${MULTIARCADERECOMP_CRUSNUSA_DCS_ROM_IMAGE}")
        message(FATAL_ERROR "Cruis'n USA DCS image does not exist: ${MULTIARCADERECOMP_CRUSNUSA_DCS_ROM_IMAGE}")
    endif()
    set(MULTIARCADERECOMP_CRUSNUSA_ADSP_PACKED_IMAGE
        "${CMAKE_BINARY_DIR}/generated/vunit/crusnusa_adsp_boot.bin")
    set(MULTIARCADERECOMP_CRUSNUSA_ADSP_AOT_SOURCE
        "${CMAKE_BINARY_DIR}/generated/vunit/crusnusa_adsp_aot.cpp")
    set(MULTIARCADERECOMP_CRUSNUSA_ADSP_AOT_GENERATED ON)
    add_custom_command(
        OUTPUT "${MULTIARCADERECOMP_CRUSNUSA_ADSP_PACKED_IMAGE}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/vunit/extract_dcs_boot.py"
                "${MULTIARCADERECOMP_CRUSNUSA_DCS_ROM_IMAGE}" "${MULTIARCADERECOMP_CRUSNUSA_ADSP_PACKED_IMAGE}"
        DEPENDS "${MULTIARCADERECOMP_CRUSNUSA_DCS_ROM_IMAGE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/vunit/extract_dcs_boot.py"
        VERBATIM)
    add_custom_command(
        OUTPUT "${MULTIARCADERECOMP_CRUSNUSA_ADSP_AOT_SOURCE}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/adsp2105/recomp.py"
                "${MULTIARCADERECOMP_CRUSNUSA_ADSP_PACKED_IMAGE}" "${MULTIARCADERECOMP_CRUSNUSA_ADSP_AOT_SOURCE}"
        DEPENDS "${MULTIARCADERECOMP_CRUSNUSA_ADSP_PACKED_IMAGE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/adsp2105/recomp.py"
        VERBATIM)
endif()
if(MULTIARCADERECOMP_CRUSNUSA_ADSP_AOT_SOURCE AND
   NOT MULTIARCADERECOMP_CRUSNUSA_ADSP_AOT_GENERATED AND
   NOT EXISTS "${MULTIARCADERECOMP_CRUSNUSA_ADSP_AOT_SOURCE}")
    message(FATAL_ERROR "Cruis'n USA ADSP-2105 AOT source does not exist: ${MULTIARCADERECOMP_CRUSNUSA_ADSP_AOT_SOURCE}")
endif()
if(MULTIARCADERECOMP_CRUSNUSA_ADSP_AOT_SOURCE)
    set(MULTIARCADERECOMP_CRUSNUSA_ADSP_AOT_INPUT "${MULTIARCADERECOMP_CRUSNUSA_ADSP_AOT_SOURCE}")
else()
    set(MULTIARCADERECOMP_CRUSNUSA_ADSP_AOT_INPUT core/cpus/adsp2105/aot_stub.cpp)
endif()
add_library(multiarcaderecomp_vunit SHARED
    core/cpus/interpreter_trace.cpp
    core/chips/adc0844/adc0844.cpp
    src/midway/vunit/core/vunit_video.cpp
    src/midway/vunit/vunit_machine.cpp
    ${MULTIARCADERECOMP_CRUSNUSA_ADSP_AOT_INPUT})
target_include_directories(multiarcaderecomp_vunit PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/core)
target_link_libraries(multiarcaderecomp_vunit PRIVATE
    multiarcaderecomp_tms320c31
    multiarcaderecomp_dcs_audio_2k)
set_target_properties(multiarcaderecomp_vunit PROPERTIES
    OUTPUT_NAME multiarcaderecomp_vunit
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
add_executable(multiarcaderecomp_vunit_trace tools/vunit/trace.cpp)
target_link_libraries(multiarcaderecomp_vunit_trace PRIVATE multiarcaderecomp_vunit)
set_target_properties(multiarcaderecomp_vunit_trace PROPERTIES BUILD_RPATH "$ORIGIN")

if(MULTIARCADERECOMP_ENABLE_VUNIT_OPENGL)
    find_package(OpenGL QUIET)
    if(NOT SDL3_FOUND OR NOT OpenGL_FOUND)
        message(FATAL_ERROR "V-Unit OpenGL backend requires SDL3 and OpenGL development files")
    endif()
    target_sources(multiarcaderecomp_vunit PRIVATE src/midway/vunit/core/vunit_video_gl.cpp)
    target_link_libraries(multiarcaderecomp_vunit PRIVATE SDL3::SDL3 OpenGL::GL)
    target_compile_definitions(multiarcaderecomp_vunit PUBLIC MULTIARCADERECOMP_VUNIT_OPENGL=1)
    message(STATUS "V-Unit optional OpenGL DMA raster backend enabled")
endif()
if(MULTIARCADERECOMP_ENABLE_WINDOW AND SDL3_FOUND)
    target_link_libraries(multiarcaderecomp_vunit_trace PRIVATE SDL3::SDL3)
    target_compile_definitions(multiarcaderecomp_vunit_trace PRIVATE MULTIARCADERECOMP_VUNIT_SDL=1)
endif()

# Optional GCC PGO for the V-Unit runtime. Keep it isolated from normal
# Release builds: collect representative gameplay runs first, then rebuild
# with the matching profile. Debug info remains enabled in both PGO passes so
# perf reports can resolve hot symbols without dropping -O3.
set(MULTIARCADERECOMP_VUNIT_PGO_MODE "OFF" CACHE STRING
    "V-Unit GCC PGO mode: OFF, GENERATE, or USE")
set_property(CACHE MULTIARCADERECOMP_VUNIT_PGO_MODE PROPERTY STRINGS OFF GENERATE USE)
set(MULTIARCADERECOMP_VUNIT_PGO_DIR "${CMAKE_BINARY_DIR}/pgo/vunit" CACHE PATH
    "Directory for V-Unit GCC PGO profile data")
string(TOUPPER "${MULTIARCADERECOMP_VUNIT_PGO_MODE}" _vunit_pgo_mode)
if(NOT _vunit_pgo_mode STREQUAL "OFF")
    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        message(FATAL_ERROR "MULTIARCADERECOMP_VUNIT_PGO_MODE requires GCC")
    endif()
    if(NOT _vunit_pgo_mode STREQUAL "GENERATE" AND
       NOT _vunit_pgo_mode STREQUAL "USE")
        message(FATAL_ERROR "MULTIARCADERECOMP_VUNIT_PGO_MODE must be OFF, GENERATE, or USE")
    endif()
    set(_vunit_pgo_targets multiarcaderecomp_adsp2105 multiarcaderecomp_dcs_audio_2k
        multiarcaderecomp_tms320c31 multiarcaderecomp_vunit multiarcaderecomp_vunit_trace)
    foreach(_target IN LISTS _vunit_pgo_targets)
        target_compile_options(${_target} PRIVATE -g)
        if(_vunit_pgo_mode STREQUAL "GENERATE")
            target_compile_options(${_target} PRIVATE
                "-fprofile-generate=${MULTIARCADERECOMP_VUNIT_PGO_DIR}")
        else()
            target_compile_options(${_target} PRIVATE
                "-fprofile-use=${MULTIARCADERECOMP_VUNIT_PGO_DIR}" -Wno-missing-profile)
        endif()
    endforeach()
    if(_vunit_pgo_mode STREQUAL "GENERATE")
        target_link_options(multiarcaderecomp_vunit PRIVATE
            "-fprofile-generate=${MULTIARCADERECOMP_VUNIT_PGO_DIR}")
        target_link_options(multiarcaderecomp_vunit_trace PRIVATE
            "-fprofile-generate=${MULTIARCADERECOMP_VUNIT_PGO_DIR}")
    endif()
    message(STATUS "V-Unit GCC PGO mode: ${_vunit_pgo_mode} (${MULTIARCADERECOMP_VUNIT_PGO_DIR})")
endif()

# The Williams ADPCM sound CPU is a shared MC6809(E) backend with a
# per-machine/per-game generated AOT table. Keep its translation artifact
# separate from the board library until the sound CPU scheduler is connected.
if(MULTIARCADERECOMP_M6809_AOT_SOURCE)
    if(NOT EXISTS "${MULTIARCADERECOMP_M6809_AOT_SOURCE}")
        message(FATAL_ERROR "MC6809(E) AOT source does not exist: ${MULTIARCADERECOMP_M6809_AOT_SOURCE}")
    endif()
    add_library(multiarcaderecomp_m6809_aot STATIC "${MULTIARCADERECOMP_M6809_AOT_SOURCE}")
    target_include_directories(multiarcaderecomp_m6809_aot PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/core)
    if(TARGET multiarcaderecomp_mk_trace)
        target_link_libraries(multiarcaderecomp_mk_trace PRIVATE multiarcaderecomp_m6809_aot)
        target_compile_definitions(multiarcaderecomp_mk_trace PRIVATE
            MULTIARCADERECOMP_M6809_HAS_AOT=1)
    endif()
endif()
target_include_directories(multiarcaderecomp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/core/cpus/z80)
target_compile_definitions(multiarcaderecomp PRIVATE
    MULTIARCADERECOMP_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    MULTIARCADERECOMP_BINARY_DIR="${CMAKE_CURRENT_BINARY_DIR}"
    MULTIARCADERECOMP_PYTHON_EXECUTABLE="${Python3_EXECUTABLE}")

# Shared MIPS III/R4600 backend used by Killer Instinct board modules. The
# Seattle memory/bus implementation remains in src/ki/runtime/seattle.
set(MULTIARCADERECOMP_MIPS3_SOURCES
    core/cpus/mips3/interpreter.c
    core/cpus/mips3/disasm.c
    core/cpus/mips3/rec_runtime.c
    core/cpus/mips3/linked_module.c)
if(MULTIARCADERECOMP_KI_MIPS_AOT_DIR)
    if(NOT EXISTS "${MULTIARCADERECOMP_KI_MIPS_AOT_DIR}/rec_tab.c")
        message(FATAL_ERROR "KI MIPS AOT directory has no rec_tab.c: ${MULTIARCADERECOMP_KI_MIPS_AOT_DIR}")
    endif()
    file(GLOB MULTIARCADERECOMP_KI_MIPS_AOT_SOURCES CONFIGURE_DEPENDS
        "${MULTIARCADERECOMP_KI_MIPS_AOT_DIR}/rec_*.c")
    list(APPEND MULTIARCADERECOMP_MIPS3_SOURCES ${MULTIARCADERECOMP_KI_MIPS_AOT_SOURCES})
endif()
if(MULTIARCADERECOMP_KI_ADSP_AOT_DIR AND
   NOT EXISTS "${MULTIARCADERECOMP_KI_ADSP_AOT_DIR}/rec_dsp.c")
    message(FATAL_ERROR "KI ADSP AOT directory has no rec_dsp.c: ${MULTIARCADERECOMP_KI_ADSP_AOT_DIR}")
endif()
add_library(multiarcaderecomp_mips3 STATIC ${MULTIARCADERECOMP_MIPS3_SOURCES})
target_include_directories(multiarcaderecomp_mips3 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/core/cpus
    ${CMAKE_CURRENT_SOURCE_DIR}/core/cpus/mips3
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ki/runtime/seattle)
target_link_libraries(multiarcaderecomp_mips3 PUBLIC m)
if(MULTIARCADERECOMP_KI_MIPS_AOT_DIR)
    target_compile_definitions(multiarcaderecomp_mips3 PRIVATE
        MULTIARCADERECOMP_MIPS3_HAS_AOT=1
        MULTIARCADERECOMP_MIPS3_GAME_ID="${MULTIARCADERECOMP_KI_MIPS_GAME_ID}")
endif()
target_link_libraries(multiarcaderecomp PRIVATE multiarcaderecomp_mips3)

# Seattle runtime: board bus/IDE/DCS are separate from the shared MIPS and
# UltraCombo ADSP implementations. The runner selects a game profile at start.
add_library(multiarcaderecomp_libchdr STATIC core/third_party/libchdr/libchdr_all.c)
target_include_directories(multiarcaderecomp_libchdr PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party
    ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/libchdr/include
    ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/libchdr/deps/lzma-26.02/include
    ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/libchdr/deps/miniz-3.1.2
    ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/libchdr/deps/zstd-1.5.7)
add_library(multiarcaderecomp_ki_seattle STATIC
    src/ki/runtime/seattle/mem.c
    src/ki/runtime/seattle/ide/ide.c
    src/ki/runtime/seattle/machine.cpp
    src/ki/runtime/dcs/dcs.cpp
    src/ki/games/game.c
    src/ki/games/ki1/profile.c
    src/ki/games/ki2/profile.c
    core/third_party/ultracombo-adsp/adsp.c)
if(MULTIARCADERECOMP_KI_ADSP_AOT_DIR)
    file(GLOB MULTIARCADERECOMP_KI_ADSP_AOT_SOURCES CONFIGURE_DEPENDS
        "${MULTIARCADERECOMP_KI_ADSP_AOT_DIR}/rec_dsp*.c")
    set_source_files_properties(${MULTIARCADERECOMP_KI_ADSP_AOT_SOURCES} PROPERTIES
        COMPILE_OPTIONS "-O1;-fno-strict-aliasing")
    target_sources(multiarcaderecomp_ki_seattle PRIVATE ${MULTIARCADERECOMP_KI_ADSP_AOT_SOURCES})
else()
    target_sources(multiarcaderecomp_ki_seattle PRIVATE
        core/third_party/ultracombo-adsp/aot_stub.c)
endif()
target_include_directories(multiarcaderecomp_ki_seattle PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/core/cpus
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ki/runtime/seattle
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ki/runtime/seattle/ide
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ki/runtime/dcs
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ki/games
    ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/ultracombo-adsp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party
    ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/libchdr/include)
target_link_libraries(multiarcaderecomp_ki_seattle PUBLIC
    multiarcaderecomp_mips3 multiarcaderecomp_libchdr ZLIB::ZLIB)
if(MULTIARCADERECOMP_KI_MIPS_AOT_DIR)
    add_executable(multiarcaderecomp_ki1
        src/ki/runner.cpp
        core/cpus/interpreter_trace.cpp)
    target_include_directories(multiarcaderecomp_ki1 PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/core
        ${CMAKE_CURRENT_SOURCE_DIR}/core/cpus
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ki/runtime/seattle
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ki/runtime/seattle/ide
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ki/runtime/dcs
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ki/games)
    target_link_libraries(multiarcaderecomp_ki1 PRIVATE
        multiarcaderecomp_ki_seattle LibArchive::LibArchive)
    if(SDL3_FOUND)
        target_link_libraries(multiarcaderecomp_ki1 PRIVATE SDL3::SDL3)
        target_compile_definitions(multiarcaderecomp_ki1 PRIVATE MULTIARCADERECOMP_KI_SDL3=1)
    endif()
    set_target_properties(multiarcaderecomp_ki1 PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        OUTPUT_NAME "multiarcaderecomp_${MULTIARCADERECOMP_KI_MIPS_GAME_ID}")
endif()

# One shared Z80 AOT backend. Generated tables are per game; the CPU, decoder,
# instruction helpers and optional fallback are common across board profiles.
if(MULTIARCADERECOMP_Z80_AOT_GENERATED_DIR)
    file(GLOB MULTIARCADERECOMP_Z80_AOT_SOURCES CONFIGURE_DEPENDS
        "${MULTIARCADERECOMP_Z80_AOT_GENERATED_DIR}/z80_*.cpp")
    if(NOT EXISTS "${MULTIARCADERECOMP_Z80_AOT_GENERATED_DIR}/z80_table.cpp")
        message(FATAL_ERROR "Z80 AOT generated directory has no z80_table.cpp: ${MULTIARCADERECOMP_Z80_AOT_GENERATED_DIR}")
    endif()
    list(FILTER MULTIARCADERECOMP_Z80_AOT_SOURCES EXCLUDE REGEX "/z80_table\\.cpp$")
    list(APPEND MULTIARCADERECOMP_Z80_AOT_SOURCES
        "${MULTIARCADERECOMP_Z80_AOT_GENERATED_DIR}/z80_table.cpp"
        core/cpus/z80/native_registry.cpp)
    add_library(multiarcaderecomp_z80_aot STATIC ${MULTIARCADERECOMP_Z80_AOT_SOURCES})
    target_include_directories(multiarcaderecomp_z80_aot PRIVATE
        "${MULTIARCADERECOMP_Z80_AOT_GENERATED_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/z80-aot/runtime"
        "${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/z80-aot/sound")
endif()

# CPS-1 board runtime and per-game AOT modules are separate. The dispatcher
# loads the selected game's tables while the board library owns hardware state.
add_library(multiarcaderecomp_cps1 SHARED
    src/cps1/cps1_module.cpp
    src/cps1/core/cps1_machine.cpp
    src/cps1/core/cps1_bus.cpp
    src/cps1/core/cps1_cpu.cpp
    src/cps1/core/cps1_cpsa.cpp
    src/cps1/core/cps1_cpsb.cpp
    src/cps1/core/cps1_gfxram.cpp
    src/cps1/core/cps1_inputs.cpp
    src/cps1/core/cps1_eeprom.cpp
    src/cps1/core/cps1_state.cpp
    src/cps1/core/cps1_sound_ym_oki.cpp
    src/cps1/core/cps1_sound_qsound.cpp
    src/cps1/core/cps1_video.cpp)
add_library(multiarcaderecomp_qsound_hle STATIC core/third_party/qsound-hle/qsound.c)
target_include_directories(multiarcaderecomp_qsound_hle PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/qsound-hle)

# Board-independent BSD-3-Clause DSP16A interpreter used by QSound LLE.
add_library(multiarcaderecomp_dsp16 STATIC core/cpus/dsp16/dsp16a.cpp)
target_include_directories(multiarcaderecomp_dsp16 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core/cpus/dsp16)
target_compile_features(multiarcaderecomp_dsp16 PUBLIC cxx_std_20)
set_target_properties(multiarcaderecomp_dsp16 PROPERTIES POSITION_INDEPENDENT_CODE ON)
set(MULTIARCADERECOMP_DSP16A_AOT_SOURCE "" CACHE FILEPATH
    "Generated DSP16A AOT source (tools/dsp16a/build_aot.py output)")
if(MULTIARCADERECOMP_DSP16A_AOT_SOURCE)
    if(NOT EXISTS "${MULTIARCADERECOMP_DSP16A_AOT_SOURCE}")
        message(FATAL_ERROR "DSP16A AOT source does not exist: ${MULTIARCADERECOMP_DSP16A_AOT_SOURCE}")
    endif()
endif()
set_property(TARGET multiarcaderecomp_dsp16 PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)

# Board-neutral MAME-derived QSound peripheral with DSP16A LLE and MAME HLE.
add_library(multiarcaderecomp_qsound STATIC core/chips/qsound.cpp)
target_include_directories(multiarcaderecomp_qsound PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/core/cpus/dsp16
    ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/qsound-hle)
target_link_libraries(multiarcaderecomp_qsound PUBLIC multiarcaderecomp_dsp16)
set_property(TARGET multiarcaderecomp_qsound PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
if(BUILD_TESTING)
    add_executable(qsound_mixed_fallback_test tests/audio/qsound_mixed_fallback_test.cpp)
    target_link_libraries(qsound_mixed_fallback_test PRIVATE multiarcaderecomp_qsound)
    multiarcaderecomp_enable_test_asserts(qsound_mixed_fallback_test)
    add_test(NAME qsound_mixed_fallback_test COMMAND qsound_mixed_fallback_test)
endif()

target_include_directories(multiarcaderecomp_cps1 PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/cps1/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/cps1"
    "${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/neogeo-runtime/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/musashi"
    "${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/z80-aot/runtime"
    "${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/z80-aot/sound"
    "${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/qsound-hle"
    "${CMAKE_CURRENT_SOURCE_DIR}/core/cpus/z80"
    "${CMAKE_CURRENT_SOURCE_DIR}/core")
target_link_libraries(multiarcaderecomp_cps1 PRIVATE musashi multiarcaderecomp_z80_core multiarcaderecomp_qsound_hle)
set_target_properties(multiarcaderecomp_cps1 PROPERTIES
    OUTPUT_NAME multiarcaderecomp_cps1 LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    BUILD_RPATH "$ORIGIN")

# C++ game drivers are the runtime source of truth for supported CPS-1 ROM
# loads and their machine/input/CPS-B configuration. The standalone exporter
# lets the existing static translation pipeline consume those tables.
add_library(multiarcaderecomp_cps1_game_data STATIC
    src/cps1/core/cps1_driver.cpp
    src/cps1/machine_profiles/cps1_10mhz.cpp
    src/cps1/machine_profiles/cps1_12mhz.cpp
    src/cps1/machine_profiles/cps1_qsound.cpp
    src/cps1/games/sf2ce/driver.cpp
    src/cps1/games/sf2ce/inputs.cpp
    src/cps1/games/sf2ce/romset.cpp
    src/cps1/games/dino/driver.cpp
    src/cps1/games/dino/inputs.cpp
    src/cps1/games/dino/romset.cpp)
target_include_directories(multiarcaderecomp_cps1_game_data PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/cps1/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/cps1")
add_executable(cps1_romset_export
    tools/cps1/export_romset.cpp
    src/cps1/core/cps1_romset.cpp)
target_link_libraries(cps1_romset_export PRIVATE
    multiarcaderecomp_cps1_game_data LibArchive::LibArchive ZLIB::ZLIB)

if(MULTIARCADERECOMP_CPS1_M68K_GENERATED_DIR)
    file(GLOB MULTIARCADERECOMP_CPS1_M68K_SOURCES CONFIGURE_DEPENDS
        "${MULTIARCADERECOMP_CPS1_M68K_GENERATED_DIR}/*recomp_*.c")
    if(NOT MULTIARCADERECOMP_CPS1_M68K_SOURCES)
        message(FATAL_ERROR "CPS-1 68000 generated directory has no *recomp_*.c files: ${MULTIARCADERECOMP_CPS1_M68K_GENERATED_DIR}")
    endif()
    target_include_directories(multiarcaderecomp_cps1 PRIVATE
        "${MULTIARCADERECOMP_CPS1_M68K_GENERATED_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/neogeo-runtime/include")

    file(GLOB cps1_game_tables CONFIGURE_DEPENDS "${MULTIARCADERECOMP_CPS1_M68K_GENERATED_DIR}/*recomp_table.c")
    list(LENGTH cps1_game_tables cps1_game_table_count)
    if(NOT cps1_game_table_count EQUAL 1)
        message(FATAL_ERROR "Expected one *_recomp_table.c in CPS-1 generated directory")
    endif()
    list(GET cps1_game_tables 0 cps1_game_table)
    file(READ "${cps1_game_table}" cps1_table_contents)
    string(REGEX MATCH "const ng_func_entry_t ([A-Za-z0-9_]+)_recomp_table\\[\\]" cps1_table_match "${cps1_table_contents}")
    if(NOT cps1_table_match)
        message(FATAL_ERROR "Could not identify CPS-1 game table in generated m68k table")
    endif()
    set(cps1_game_prefix "${CMAKE_MATCH_1}")
    set(cps1_game_adapter "${CMAKE_CURRENT_BINARY_DIR}/cps1_${MULTIARCADERECOMP_CPS1_GAME_ID}_game_adapter.cpp")
    if(TARGET multiarcaderecomp_z80_aot)
        set(cps1_z80_getter_decl "const multiarcaderecomp::z80::Z80Code* multiarcaderecomp_z80_aot_code();")
        set(cps1_z80_getter_body "return multiarcaderecomp_z80_aot_code();")
    else()
        set(cps1_z80_getter_decl "")
        set(cps1_z80_getter_body "return nullptr;")
    endif()
    if(NOT MULTIARCADERECOMP_CPS1_GAME_ID STREQUAL "sf2ce" AND NOT MULTIARCADERECOMP_CPS1_GAME_ID STREQUAL "dino")
        message(FATAL_ERROR "CPS-1 runtime game driver is not implemented: ${MULTIARCADERECOMP_CPS1_GAME_ID}")
    endif()
    file(WRITE "${cps1_game_adapter}"
        "#include <cstddef>\n#include <neogeorecomp/neogeo.h>\n#include <native_z80.h>\n#include <cps1_driver.hpp>\n"
        "extern \"C\" { extern const ng_func_entry_t ${cps1_game_prefix}_recomp_table[]; extern const size_t ${cps1_game_prefix}_recomp_count; const uint8_t* ${cps1_game_prefix}_ram_snapshot(size_t*); ${cps1_z80_getter_decl} }\n"
        "extern \"C\" unsigned multiarcaderecomp_game_abi() { return 1; }\n"
        "extern \"C\" const char* multiarcaderecomp_game_id() { return \"${MULTIARCADERECOMP_CPS1_GAME_ID}\"; }\n"
        "extern \"C\" const char* multiarcaderecomp_game_board_id() { return \"cps1\"; }\n"
        "extern \"C\" const ng_func_entry_t* multiarcaderecomp_cps1_game_m68k_table(size_t* count) { *count=${cps1_game_prefix}_recomp_count; return ${cps1_game_prefix}_recomp_table; }\n"
        "extern \"C\" const uint8_t* multiarcaderecomp_cps1_game_ram_snapshot(size_t* size) { return ${cps1_game_prefix}_ram_snapshot(size); }\n"
        "extern \"C\" const multiarcaderecomp::z80::Z80Code* multiarcaderecomp_cps1_game_z80_code() { ${cps1_z80_getter_body} }\n"
        "extern \"C\" const multiarcaderecomp::cps1::Cps1GameDriver* multiarcaderecomp_cps1_game_driver() { return multiarcaderecomp::cps1::find_game_driver(\"${MULTIARCADERECOMP_CPS1_GAME_ID}\"); }\n")
    add_library(multiarcaderecomp_game_${MULTIARCADERECOMP_CPS1_GAME_ID} SHARED
        ${MULTIARCADERECOMP_CPS1_M68K_SOURCES} "${cps1_game_adapter}")
    target_include_directories(multiarcaderecomp_game_${MULTIARCADERECOMP_CPS1_GAME_ID} PRIVATE
        "${MULTIARCADERECOMP_CPS1_M68K_GENERATED_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/cps1/core"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/cps1"
        "${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/neogeo-runtime/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/z80-aot/runtime")
    target_compile_options(multiarcaderecomp_game_${MULTIARCADERECOMP_CPS1_GAME_ID} PRIVATE
        $<$<COMPILE_LANGUAGE:C>:-O1>)
    target_link_libraries(multiarcaderecomp_game_${MULTIARCADERECOMP_CPS1_GAME_ID} PRIVATE
        multiarcaderecomp_cps1 multiarcaderecomp_cps1_game_data m68k_cpu_runtime)
    if(TARGET multiarcaderecomp_z80_aot)
        target_link_libraries(multiarcaderecomp_game_${MULTIARCADERECOMP_CPS1_GAME_ID} PRIVATE multiarcaderecomp_z80_aot)
    endif()
    target_link_libraries(multiarcaderecomp_game_${MULTIARCADERECOMP_CPS1_GAME_ID} PRIVATE multiarcaderecomp_z80_core)
    set_target_properties(multiarcaderecomp_game_${MULTIARCADERECOMP_CPS1_GAME_ID} PROPERTIES
        OUTPUT_NAME "multiarcaderecomp_game_${MULTIARCADERECOMP_CPS1_GAME_ID}"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        BUILD_RPATH "$ORIGIN")
    add_executable(cps1_boot src/cps1/cps1_boot.cpp)
    target_include_directories(cps1_boot PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/cps1"
        "${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/neogeo-runtime/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/z80-aot/runtime")
    target_link_libraries(cps1_boot PRIVATE
        multiarcaderecomp_cps1 multiarcaderecomp_game_${MULTIARCADERECOMP_CPS1_GAME_ID}
        m68k_cpu_runtime multiarcaderecomp_z80_core)
    target_compile_definitions(cps1_boot PRIVATE
        MULTIARCADERECOMP_CPS1_GAME_ID=\"${MULTIARCADERECOMP_CPS1_GAME_ID}\")
    set_target_properties(cps1_boot PROPERTIES
        OUTPUT_NAME "multiarcaderecomp_${MULTIARCADERECOMP_CPS1_GAME_ID}"
        BUILD_RPATH "$ORIGIN")
endif()

add_library(multiarcaderecomp_chip_3155195 SHARED core/chips/315_5195/mapper.cpp)
target_include_directories(multiarcaderecomp_chip_3155195 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core)
set_target_properties(multiarcaderecomp_chip_3155195 PROPERTIES
    OUTPUT_NAME multiarcaderecomp_chip_3155195
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_library(multiarcaderecomp_chip_3155296 SHARED core/chips/315_5296/io.cpp)
target_include_directories(multiarcaderecomp_chip_3155296 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core)
set_target_properties(multiarcaderecomp_chip_3155296 PROPERTIES
    OUTPUT_NAME multiarcaderecomp_chip_3155296
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_library(multiarcaderecomp_chip_3155313 SHARED core/chips/315_5313/vdp.cpp)
target_include_directories(multiarcaderecomp_chip_3155313 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core)
set_target_properties(multiarcaderecomp_chip_3155313 PROPERTIES
    OUTPUT_NAME multiarcaderecomp_chip_3155313
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# Naomi metadata, SH-4 execution state and board/game ABI are separate from
# the 68000 modules. The AOT dispatcher and device map are still in progress.
add_library(multiarcaderecomp_naomi MODULE
    src/naomi/naomi_module.cpp
    src/naomi/naomi_address_space.cpp
    src/naomi/naomi_aica_dsp.cpp
    src/naomi/naomi_aica_voices.cpp
    src/naomi/naomi_arm7.cpp
    src/naomi/naomi_x76f100.cpp
    src/naomi/naomi_sh4_context.cpp
    src/naomi/naomi_sh4_helpers.cpp
    core/cpus/sh4/flycast_interpreter.cpp
    core/cpus/sh4/flycast_fpu.cpp
    core/cpus/sh4/flycast_cycles.cpp
    core/third_party/mame-sh4-fpu/sh4fpu.cpp
    core/cpus/interpreter_trace.cpp
    src/naomi/naomi_pvr2.cpp
    src/naomi/naomi_holly.cpp
    src/naomi/naomi_jvs_837_13551.cpp
    src/naomi/naomi_m1crypt.cpp
    src/naomi/naomi_m2crypt.cpp)
if(MULTIARCADERECOMP_NAOMI_TRANSLATED_SOURCES)
    target_sources(multiarcaderecomp_naomi PRIVATE ${MULTIARCADERECOMP_NAOMI_TRANSLATED_SOURCES})
    target_compile_definitions(multiarcaderecomp_naomi PRIVATE MULTIARCADERECOMP_NAOMI_HAS_TRANSLATED_SH4=1)
endif()
if(MULTIARCADERECOMP_NAOMI_HAS_BOOT_RAM_TRANSLATION)
    target_compile_definitions(multiarcaderecomp_naomi PRIVATE MULTIARCADERECOMP_NAOMI_HAS_BOOT_RAM_TRANSLATION=1)
endif()
if(MULTIARCADERECOMP_NAOMI_HAS_MAIN_ROM_TRANSLATION)
    target_compile_definitions(multiarcaderecomp_naomi PRIVATE MULTIARCADERECOMP_NAOMI_HAS_MAIN_ROM_TRANSLATION=1)
endif()
if(MULTIARCADERECOMP_NAOMI_HAS_OBSERVED_ROM_ROOTS)
    target_compile_definitions(multiarcaderecomp_naomi PRIVATE MULTIARCADERECOMP_NAOMI_HAS_OBSERVED_ROM_ROOTS=1)
endif()
target_include_directories(multiarcaderecomp_naomi PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/flycast-aica/naomi_compat)
set_target_properties(multiarcaderecomp_naomi PROPERTIES
    PREFIX "" SUFFIX ".so"
    OUTPUT_NAME multiarcaderecomp_naomi
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# Neo Geo has its own ROM/profile and board runtime module; it does not use the
# Sega System 16/18 bus or video implementations.
set(MULTIARCADERECOMP_NEOGEO_KOF98_MANIFEST
    "${CMAKE_CURRENT_SOURCE_DIR}/src/romsets/snk/neogeo/neogeo_mvs/kof98.toml")
set(MULTIARCADERECOMP_NEOGEO_KOF98_DATA
    "${CMAKE_CURRENT_BINARY_DIR}/generated/neogeo/kof98_romset_data.hpp")
add_custom_command(
    OUTPUT "${MULTIARCADERECOMP_NEOGEO_KOF98_DATA}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/generators/romsets/emit_neogeo_runtime.py"
        --manifest "${MULTIARCADERECOMP_NEOGEO_KOF98_MANIFEST}"
        --output "${MULTIARCADERECOMP_NEOGEO_KOF98_DATA}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/generators/romsets/emit_neogeo_runtime.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/generators/romsets/manifest_io.py"
        "${MULTIARCADERECOMP_NEOGEO_KOF98_MANIFEST}"
    VERBATIM)
add_library(multiarcaderecomp_neogeo SHARED
    src/neogeo/neogeo_romset.cpp
    src/neogeo/neogeo_protection_state.c
    "${MULTIARCADERECOMP_NEOGEO_KOF98_DATA}")
target_include_directories(multiarcaderecomp_neogeo PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/neogeo-runtime/include
    ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/m68krecomp/include
    PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated/neogeo")
target_link_libraries(multiarcaderecomp_neogeo PRIVATE LibArchive::LibArchive ZLIB::ZLIB)
set_target_properties(multiarcaderecomp_neogeo PROPERTIES
    OUTPUT_NAME multiarcaderecomp_neogeo
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

if(MULTIARCADERECOMP_ENABLE_WINDOW AND SDL3_FOUND)
    target_sources(multiarcaderecomp PRIVATE
        core/video/sdl_video_presenter.cpp
        src/naomi/naomi_presenter.cpp
        src/konami/tmnt_presenter.cpp
        src/seibu/toki_presenter.cpp
        core/third_party/imgui/imgui.cpp
        core/third_party/imgui/imgui_draw.cpp
        core/third_party/imgui/imgui_tables.cpp
        core/third_party/imgui/imgui_widgets.cpp
        core/third_party/imgui/backends/imgui_impl_sdl3.cpp
        core/third_party/imgui/backends/imgui_impl_sdlrenderer3.cpp)
    target_include_directories(multiarcaderecomp PRIVATE core/third_party/imgui core/third_party/imgui/backends)
    target_link_libraries(multiarcaderecomp PRIVATE SDL3::SDL3)
    target_compile_definitions(multiarcaderecomp PRIVATE MULTIARCADERECOMP_HAS_SDL3_PRESENTER=1)
    if(TARGET multiarcaderecomp_mk_trace)
        target_link_libraries(multiarcaderecomp_mk_trace PRIVATE SDL3::SDL3)
        target_compile_definitions(multiarcaderecomp_mk_trace PRIVATE MULTIARCADERECOMP_MK_SDL=1)
    endif()
    if(TARGET multiarcaderecomp_mk2_trace)
        target_link_libraries(multiarcaderecomp_mk2_trace PRIVATE SDL3::SDL3)
        target_compile_definitions(multiarcaderecomp_mk2_trace PRIVATE MULTIARCADERECOMP_MK2_SDL=1)
    endif()
    message(STATUS "MultiArcadeRecomp SDL3 live window presentation enabled")
    if(TARGET cps1_boot)
        target_link_libraries(cps1_boot PRIVATE SDL3::SDL3)
        target_compile_definitions(cps1_boot PRIVATE MULTIARCADERECOMP_CPS1_SDL=1)
        target_link_libraries(multiarcaderecomp_cps1 PRIVATE SDL3::SDL3)
        target_compile_definitions(multiarcaderecomp_cps1 PRIVATE MULTIARCADERECOMP_CPS1_SDL=1)
    endif()
endif()

# Musashi (MIT) is the independent 68000 oracle/cycle source. Native code
# generation remains in the local MultiArcadeRecomp M68K AoT frontend target.
set(MUSASHI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/musashi)
set(MUSASHI_GEN ${CMAKE_CURRENT_BINARY_DIR}/musashi_gen)
file(MAKE_DIRECTORY ${MUSASHI_GEN})
add_executable(m68kmake ${MUSASHI_DIR}/m68kmake.c)
add_custom_command(
    OUTPUT ${MUSASHI_GEN}/m68kops.c ${MUSASHI_GEN}/m68kops.h
    COMMAND m68kmake ${MUSASHI_GEN} ${MUSASHI_DIR}/m68k_in.c
    DEPENDS m68kmake ${MUSASHI_DIR}/m68k_in.c
    COMMENT "Generating Musashi opcode tables")
add_library(musashi STATIC
    ${MUSASHI_DIR}/m68kcpu.c
    ${MUSASHI_DIR}/m68kdasm.c
    ${MUSASHI_DIR}/softfloat/softfloat.c
    ${MUSASHI_GEN}/m68kops.c
    core/third_party/m68krecomp/runtime/musashi_glue.c)
target_include_directories(musashi PUBLIC
    ${MUSASHI_DIR}
    ${MUSASHI_GEN}
    ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/m68krecomp/runtime)
target_compile_definitions(musashi PUBLIC MUSASHI_CNF=\"ng_musashi_conf.h\")
target_link_libraries(musashi PUBLIC m)

# Local vendored 68000 frontend/emitter/discovery (MIT), imported from
# NeoGeoRecomp; provenance and notices are kept under docs/third_party.
# Generated code uses the CPU ABI adapters built below; board devices remain ours.
add_library(multiarcaderecomp_m68k_aot_frontend STATIC
    core/cpus/m68000/recomp_board_profile.c
    core/third_party/m68krecomp/decode.c
    core/third_party/m68krecomp/emit.c
    core/third_party/m68krecomp/recomp.c
    core/third_party/m68krecomp/runtime/romload.c)
target_include_directories(multiarcaderecomp_m68k_aot_frontend
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/core/cpus/m68000
        ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/m68krecomp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/m68krecomp/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/m68krecomp/runtime)
target_link_libraries(multiarcaderecomp_m68k_aot_frontend PUBLIC musashi)
add_executable(m68krecomp core/third_party/m68krecomp/main.c)
target_link_libraries(m68krecomp PRIVATE multiarcaderecomp_m68k_aot_frontend)
add_library(m68krecomp_frontend ALIAS multiarcaderecomp_m68k_aot_frontend)

# Shared 68000 register, exception, and AOT-dispatch helpers use the
# m68krecomp ABI. Each board installs its own bus callbacks and device hooks.
add_library(m68k_cpu_runtime SHARED core/cpus/m68000/runtime/cpu_runtime.c)
target_include_directories(m68k_cpu_runtime
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/m68krecomp/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/core/cpus/m68000/runtime
)
set_target_properties(m68k_cpu_runtime PROPERTIES
    OUTPUT_NAME multiarcaderecomp_cpu_m68000
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_library(multiarcaderecomp_m68k_recomp_runtime SHARED
    core/cpus/m68000/runtime/recomp_bus.c
    core/cpus/m68000/runtime/recomp_dispatch.c)
target_include_directories(multiarcaderecomp_m68k_recomp_runtime
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/m68krecomp/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/core)
target_link_libraries(multiarcaderecomp_m68k_recomp_runtime PRIVATE m68k_cpu_runtime)
set_target_properties(multiarcaderecomp_m68k_recomp_runtime PROPERTIES
    OUTPUT_NAME multiarcaderecomp_m68k_recomp_runtime
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# Shared Musashi 68000 interpreter fallback for boards using this CPU. Keep
# its symbols local to avoid colliding with the compiler frontend instance.
add_library(multiarcaderecomp_m68k_musashi SHARED
    ${MUSASHI_DIR}/m68kcpu.c
    ${MUSASHI_DIR}/m68kdasm.c
    ${MUSASHI_DIR}/softfloat/softfloat.c
    ${MUSASHI_GEN}/m68kops.c
    core/third_party/m68krecomp/runtime/musashi_glue.c
    core/cpus/m68000/runtime/musashi_fallback.cpp)
target_include_directories(multiarcaderecomp_m68k_musashi PRIVATE
    ${MUSASHI_DIR}
    ${MUSASHI_GEN}
    ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/m68krecomp/runtime
    ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/m68krecomp/include
    ${CMAKE_CURRENT_SOURCE_DIR}/core/cpus/m68000/runtime)
target_compile_definitions(multiarcaderecomp_m68k_musashi PRIVATE
    MUSASHI_CNF=\"multiarcaderecomp_musashi_conf.h\"
    MULTIARCADERECOMP_MUSASHI_YIELD_BEFORE_INSTRUCTION=1)
target_link_libraries(multiarcaderecomp_m68k_musashi PRIVATE
    m68k_cpu_runtime multiarcaderecomp_m68k_recomp_runtime)
set_target_properties(multiarcaderecomp_m68k_musashi PROPERTIES
    OUTPUT_NAME multiarcaderecomp_m68k_musashi
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    BUILD_RPATH "$ORIGIN"
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES)

if(BUILD_TESTING)
    add_executable(m68k_musashi_boundary_test tests/system16/musashi_boundary_test.cpp)
    target_include_directories(m68k_musashi_boundary_test PRIVATE ${PROJECT_SOURCE_DIR}/core)
    target_link_libraries(m68k_musashi_boundary_test PRIVATE
        multiarcaderecomp_m68k_musashi multiarcaderecomp_m68k_recomp_runtime m68k_cpu_runtime)
    multiarcaderecomp_enable_test_asserts(m68k_musashi_boundary_test)
    add_test(NAME m68k_musashi_boundary_test COMMAND m68k_musashi_boundary_test)
endif()

# Board-independent Musashi 68000 runtime. Keep its symbols local so it can
# coexist with other Musashi instances loaded by the host.
add_library(multiarcaderecomp_musashi_68000 SHARED
    ${MUSASHI_DIR}/m68kcpu.c
    ${MUSASHI_DIR}/softfloat/softfloat.c
    ${MUSASHI_GEN}/m68kops.c
    core/cpus/m68000/musashi_runtime.cpp)
target_include_directories(multiarcaderecomp_musashi_68000 PRIVATE
    ${MUSASHI_DIR} ${MUSASHI_GEN}
    ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/m68krecomp/runtime
    ${CMAKE_CURRENT_SOURCE_DIR}/core/cpus/m68000/runtime
    ${CMAKE_CURRENT_SOURCE_DIR}/core/cpus/m68000)
target_compile_definitions(multiarcaderecomp_musashi_68000 PRIVATE MUSASHI_CNF=\"multiarcaderecomp_musashi_conf.h\")
target_link_libraries(multiarcaderecomp_musashi_68000 PRIVATE m)
set_target_properties(multiarcaderecomp_musashi_68000 PROPERTIES
    OUTPUT_NAME multiarcaderecomp_musashi_68000
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    BUILD_RPATH "$ORIGIN"
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES)
target_link_libraries(multiarcaderecomp_zn_runtime PUBLIC multiarcaderecomp_musashi_68000)

if(TARGET multiarcaderecomp_cps1)
    target_link_libraries(multiarcaderecomp_cps1 PRIVATE m68k_cpu_runtime)
endif()
target_link_options(multiarcaderecomp PRIVATE "LINKER:-rpath,$ORIGIN")
if(UNIX AND NOT APPLE)
    # Optional per-game settings panels in loadable game modules call the
    # host-owned ImGui context through this exported executable ABI.
    target_link_options(multiarcaderecomp PRIVATE "LINKER:--export-dynamic")
endif()

function(add_multiarcaderecomp_game_module board_id revision_value generated_dir)
    if(NOT IS_DIRECTORY "${generated_dir}")
        message(FATAL_ERROR "Generated-code directory does not exist: ${generated_dir}")
    endif()
    get_filename_component(game_id "${generated_dir}" NAME)
    set(sega_z80_generated_dir "${generated_dir}/z80")
    if(NOT IS_DIRECTORY "${sega_z80_generated_dir}")
        set(sega_z80_generated_dir "")
    endif()
    set(target_name "multiarcaderecomp_${board_id}_${game_id}")
    set(module_name "multiarcaderecomp_${board_id}_${game_id}")
    set(module_sources)
    set(module_adapter "#include <cstring>\n#include <neogeorecomp/neogeo.h>\n")
    if(sega_z80_generated_dir)
        string(APPEND module_adapter "#include <native_z80.h>\n#include <z80_decl.h>\n")
    endif()
    string(APPEND module_adapter "extern \"C\" {\n")
    file(GLOB game_tables CONFIGURE_DEPENDS "${generated_dir}/*_recomp_table.c")
    list(LENGTH game_tables game_table_count)
    if(NOT game_table_count EQUAL 1)
        message(FATAL_ERROR "Expected one *_recomp_table.c in ${generated_dir}, found ${game_table_count}")
    endif()
    list(GET game_tables 0 game_table)
    file(READ "${game_table}" game_table_contents)
    string(REGEX MATCH "const ng_func_entry_t ([A-Za-z0-9_]+)_ng_recomp_table\\[\\]" table_match "${game_table_contents}")
    if(NOT table_match)
        message(FATAL_ERROR "Could not identify game table symbol in ${game_table}")
    endif()
    set(game_prefix "${CMAKE_MATCH_1}")
    string(APPEND module_adapter "extern const ng_func_entry_t ${game_prefix}_ng_recomp_table[];\nextern const size_t ${game_prefix}_ng_recomp_count;\n")
    file(GLOB game_sources CONFIGURE_DEPENDS "${generated_dir}/*_recomp_*.c")
    if(NOT game_sources)
        message(FATAL_ERROR "No *_recomp_*.c sources found in ${generated_dir}")
    endif()
    list(APPEND module_sources ${game_sources})
    if(sega_z80_generated_dir)
        file(GLOB sega_z80_sources CONFIGURE_DEPENDS "${sega_z80_generated_dir}/z80_*.cpp")
        if(NOT EXISTS "${sega_z80_generated_dir}/z80_table.cpp")
            message(FATAL_ERROR "Sega Z80 AOT generated directory has no z80_table.cpp: ${sega_z80_generated_dir}")
        endif()
        list(APPEND sega_z80_sources "${sega_z80_generated_dir}/z80_table.cpp")
        list(APPEND module_sources ${sega_z80_sources})
        string(APPEND module_adapter "const multiarcaderecomp::z80::Z80Code* multiarcaderecomp_sega_z80_code(void) { return &multiarcaderecomp::z80::kZ80Code; }\n")
    endif()
    string(APPEND module_adapter "}\nextern \"C\" unsigned multiarcaderecomp_board_abi(void) { return 1; }\nextern \"C\" int multiarcaderecomp_board_revision(void) { return ${revision_value}; }\nextern \"C\" const ng_func_entry_t* multiarcaderecomp_generated_table(const char* profile_id, size_t* count) {\n")
    string(APPEND module_adapter "    if (std::strcmp(profile_id, \"${game_prefix}\") == 0) { *count = ${game_prefix}_ng_recomp_count; return ${game_prefix}_ng_recomp_table; }\n")
    string(APPEND module_adapter "    *count = 0; return nullptr;\n}\n")
    set(adapter_path "${CMAKE_CURRENT_BINARY_DIR}/${target_name}_adapter.cpp")
    file(WRITE "${adapter_path}" "${module_adapter}")
    add_library(${target_name} MODULE ${module_sources} "${adapter_path}")
    set(MULTIARCADERECOMP_GAME_MODULE_TARGET "${target_name}")
    set(game_module_cmake "${PROJECT_SOURCE_DIR}/src/games/sega/${board_id}/${game_id}/CMakeLists.txt")
    if(EXISTS "${game_module_cmake}")
        include("${game_module_cmake}")
    endif()
    unset(MULTIARCADERECOMP_GAME_MODULE_TARGET)
    target_include_directories(${target_name} PRIVATE
        "${generated_dir}"
        ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/m68krecomp/include)
    target_link_libraries(${target_name} PRIVATE multiarcaderecomp_m68k_recomp_runtime)
    if(sega_z80_generated_dir)
        target_include_directories(${target_name} PRIVATE
            "${sega_z80_generated_dir}"
            "${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/z80-aot/runtime"
            "${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/z80-aot/sound")
        target_link_libraries(${target_name} PRIVATE multiarcaderecomp_z80_core)
    endif()
    set_target_properties(${target_name} PROPERTIES
        PREFIX "" SUFFIX ".so" OUTPUT_NAME "${module_name}"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        BUILD_RPATH "$ORIGIN")
    target_compile_options(${target_name} PRIVATE
        $<$<COMPILE_LANGUAGE:C>:$<$<C_COMPILER_ID:GNU,Clang>:-O3>>)
endfunction()

function(add_multiarcaderecomp_board_modules board_id revision_value generated_dirs)
    foreach(generated_dir IN LISTS generated_dirs)
        add_multiarcaderecomp_game_module("${board_id}" ${revision_value} "${generated_dir}")
    endforeach()
endfunction()

if(MULTIARCADERECOMP_GENERATED_DIR)
    file(GLOB legacy_tables "${MULTIARCADERECOMP_GENERATED_DIR}/*_recomp_table.c")
    if(legacy_tables)
        list(GET legacy_tables 0 legacy_table)
        get_filename_component(legacy_table_name "${legacy_table}" NAME)
        if(legacy_table_name MATCHES "^(goldnaxeud|shinobi5)_recomp_table\\.c$")
            set(MULTIARCADERECOMP_SYSTEM16B_GENERATED_DIRS "${MULTIARCADERECOMP_GENERATED_DIR}")
        elseif(legacy_table_name MATCHES "^shdancer_recomp_table\\.c$")
            set(MULTIARCADERECOMP_SYSTEM18_GENERATED_DIRS "${MULTIARCADERECOMP_GENERATED_DIR}")
        else()
            set(MULTIARCADERECOMP_SYSTEM16A_GENERATED_DIRS "${MULTIARCADERECOMP_GENERATED_DIR}")
        endif()
    endif()
endif()

add_multiarcaderecomp_board_modules(system16a 16 "${MULTIARCADERECOMP_SYSTEM16A_GENERATED_DIRS}")
add_multiarcaderecomp_board_modules(system16b 18 "${MULTIARCADERECOMP_SYSTEM16B_GENERATED_DIRS}")
add_multiarcaderecomp_board_modules(system18 18 "${MULTIARCADERECOMP_SYSTEM18_GENERATED_DIRS}")
target_compile_definitions(multiarcaderecomp PRIVATE MULTIARCADERECOMP_HAS_GENERATED_RECOMP=1)

# General-purpose Z80 interpreter from the NeoGeoRecomp dependency set (MIT).
add_library(z80_core STATIC core/third_party/superzazu-z80/z80.c)
target_include_directories(z80_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/superzazu-z80)

# Board-neutral native Z80 execution and interpreter fallback used directly by
# CPS-1; each board retains its own bus, banking, clocking, and device hooks.
add_library(multiarcaderecomp_z80_core SHARED
    core/third_party/z80-aot/sound/z80.cpp
    core/third_party/z80-aot/runtime/recomp_z80.cpp)
target_include_directories(multiarcaderecomp_z80_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/z80-aot/runtime
    ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/z80-aot/sound)
set_target_properties(multiarcaderecomp_z80_core PROPERTIES
    OUTPUT_NAME multiarcaderecomp_cpu_z80
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    BUILD_RPATH "$ORIGIN")

add_subdirectory(core/chips/yamaha)
if(TARGET multiarcaderecomp_cps1)
    target_link_libraries(multiarcaderecomp_cps1 PRIVATE ymfm_opm)
endif()

add_subdirectory(core/audio)
add_subdirectory(core/chips/rf5c68)
add_subdirectory(core/chips/sega_psg)
add_subdirectory(core/chips/sega_315_5196)
add_subdirectory(core/chips/sega_315_5197)
add_subdirectory(core/chips/sega_315_5248)
add_subdirectory(core/chips/sega_315_5250)
add_subdirectory(core/chips/sega_315_5049)
add_subdirectory(core/chips/sega_315_5011)
add_subdirectory(core/chips/sega_315_5012)
add_subdirectory(core/chips/sega_i8751)
add_subdirectory(core/chips/i8255)
add_subdirectory(core/chips/i8243)
add_subdirectory(core/chips/ymfm_timer)
add_subdirectory(src/boards/sega)
add_subdirectory(src/runtime/sega/runner)

if(BUILD_TESTING)
    add_executable(sega_video_cache_test tests/system16/video_cache_test.cpp)
    target_include_directories(sega_video_cache_test PRIVATE ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/core)
    target_link_libraries(sega_video_cache_test PRIVATE multiarcaderecomp_chip_sega_315_5197 multiarcaderecomp_sega_board_common)
    multiarcaderecomp_enable_test_asserts(sega_video_cache_test)
    add_test(NAME sega_video_cache_test COMMAND sega_video_cache_test)
    add_executable(rf5c68_test tests/audio/rf5c68_test.cpp)
    target_link_libraries(rf5c68_test PRIVATE multiarcaderecomp_chip_rf5c68)
    multiarcaderecomp_enable_test_asserts(rf5c68_test)
    add_test(NAME rf5c68_test COMMAND rf5c68_test)
endif()

target_link_libraries(multiarcaderecomp PRIVATE
    LibArchive::LibArchive ZLIB::ZLIB
    multiarcaderecomp_rom_hash
    multiarcaderecomp_neogeo multiarcaderecomp_konami_tmnt multiarcaderecomp_konami_tmnt2
    multiarcaderecomp_seibu_toki multiarcaderecomp_zn_runtime multiarcaderecomp_upd7759
    m68k_cpu_runtime multiarcaderecomp_m68k_recomp_runtime
    multiarcaderecomp_m68k_musashi multiarcaderecomp_z80_core
    multiarcaderecomp_sega_runtime multiarcaderecomp_sega_runner
    ymfm_opm ymfm_opn ${CMAKE_DL_LIBS})

# The local Neo Geo board runtime is kept separate from the Sega runners.
# Game profile and AoT data are compiled into the selected game's module.
add_library(multiarcaderecomp_neogeo_runtime SHARED
    core/third_party/neogeo-runtime/src/neogeo.c
    core/third_party/neogeo-runtime/src/exec.c
    core/third_party/neogeo-runtime/src/cpu.c
    core/third_party/neogeo-runtime/src/bus.c
    core/third_party/neogeo-runtime/src/lspc.c
    core/third_party/neogeo-runtime/src/video.c
    core/third_party/neogeo-runtime/src/io.c
    core/third_party/neogeo-runtime/src/audio.c
    core/third_party/neogeo-runtime/src/ym2610.cpp
    core/third_party/z80-aot/sound/z80.cpp
    core/third_party/z80-aot/runtime/recomp_z80.cpp
    src/neogeo/kof98_z80_host.cpp
    src/neogeo/neogeo_board_module.cpp
    core/third_party/neogeo-runtime/src/platform.c
    core/third_party/neogeo-runtime/src/romload.c)
target_include_directories(multiarcaderecomp_neogeo_runtime
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/neogeo-runtime/include
        ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/m68krecomp/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/neogeo-runtime/src
        ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/superzazu-z80
        ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/z80-aot/runtime
        ${CMAKE_CURRENT_SOURCE_DIR}/src/neogeo)
target_link_libraries(multiarcaderecomp_neogeo_runtime PUBLIC
    musashi z80_core ymfm_opn multiarcaderecomp_neogeo ${CMAKE_DL_LIBS})
if(UNIX AND NOT APPLE)
    # The application also loads the shared generic MultiArcadeRecomp Musashi
    # runtime. This module contains its own Musashi instance with Neo Geo bus
    # callbacks; bind its internal Musashi references locally so ELF symbol
    # interposition cannot route BIOS execution through the generic CPU copy.
    target_link_options(multiarcaderecomp_neogeo_runtime PRIVATE "LINKER:-Bsymbolic")
endif()
target_compile_definitions(multiarcaderecomp_neogeo_runtime PRIVATE
    MULTIARCADERECOMP_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    MULTIARCADERECOMP_BINARY_DIR="${CMAKE_CURRENT_BINARY_DIR}")
set_target_properties(multiarcaderecomp_neogeo_runtime PROPERTIES
    PREFIX ""
    OUTPUT_NAME multiarcaderecomp_neogeo_runtime
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    BUILD_RPATH "$ORIGIN")
find_package(SDL2 CONFIG QUIET)
if(TARGET SDL2::SDL2)
    # Keep SDL2 in a C-only helper DSO. The app links SDL3; loading this
    # helper with glibc RTLD_DEEPBIND isolates only SDL entry points without
    # deep-binding the C++ Neo Geo runtime and its standard-library state.
    add_library(multiarcaderecomp_neogeo_window SHARED
        core/third_party/neogeo-runtime/src/window_sdl.c)
    target_include_directories(multiarcaderecomp_neogeo_window PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/neogeo-runtime/include
        ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/neogeo-runtime/src)
    target_link_libraries(multiarcaderecomp_neogeo_window PRIVATE SDL2::SDL2)
    set_target_properties(multiarcaderecomp_neogeo_window PROPERTIES
        PREFIX ""
        SUFFIX ".so"
        OUTPUT_NAME multiarcaderecomp_neogeo_window
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        BUILD_RPATH "$ORIGIN")
    target_compile_definitions(multiarcaderecomp_neogeo_runtime PRIVATE NG_HAVE_SDL=1)
    target_link_libraries(multiarcaderecomp_neogeo_runtime PUBLIC multiarcaderecomp_neogeo_window)
endif()

add_executable(kof98_aot src/neogeo/kof98_aot_main.cpp src/neogeo/kof98_game.c)
target_link_libraries(kof98_aot PRIVATE multiarcaderecomp_neogeo multiarcaderecomp_m68k_aot_frontend)

# Pacrecomp MIT frontend probe used to develop the project-owned Z80 emitter.
# It is deliberately excluded from the default build and is not linked into
# any production board or game module.
add_executable(multiarcaderecomp_z80_pacrecomp_probe EXCLUDE_FROM_ALL
    tools/z80_aot/pacrecomp_probe.c
    tools/z80_aot/pacrecomp/analyze.c
    tools/z80_aot/pacrecomp/decode.c
    tools/z80_aot/pacrecomp/emit.c)
target_include_directories(multiarcaderecomp_z80_pacrecomp_probe PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/z80_aot")
set_target_properties(multiarcaderecomp_z80_pacrecomp_probe PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON)

if(MULTIARCADERECOMP_NEOGEO_GENERATED_DIR AND IS_DIRECTORY "${MULTIARCADERECOMP_NEOGEO_GENERATED_DIR}")
    file(GLOB neogeo_aot_sources CONFIGURE_DEPENDS
        "${MULTIARCADERECOMP_NEOGEO_GENERATED_DIR}/recomp_*.c")
    if(NOT neogeo_aot_sources)
        message(FATAL_ERROR "No KOF98 AOT C files found in ${MULTIARCADERECOMP_NEOGEO_GENERATED_DIR}")
    endif()
    set(neogeo_z80_aot_sources)
    if(MULTIARCADERECOMP_NEOGEO_Z80_GENERATED_DIR AND IS_DIRECTORY "${MULTIARCADERECOMP_NEOGEO_Z80_GENERATED_DIR}")
        file(GLOB_RECURSE neogeo_z80_aot_sources CONFIGURE_DEPENDS
            "${MULTIARCADERECOMP_NEOGEO_Z80_GENERATED_DIR}/*/z80_*.cpp")
        if(NOT neogeo_z80_aot_sources)
            message(FATAL_ERROR "No generated KOF98 Z80 sources found in ${MULTIARCADERECOMP_NEOGEO_Z80_GENERATED_DIR}")
        endif()
    endif()
    # The 68000 AoT embeds the selected BIOS image, so AES and MVS must never
    # share a CMake target/object directory even though their runtime ABI is
    # identical.
    set(neogeo_game_target "multiarcaderecomp_game_kof98_${MULTIARCADERECOMP_NEOGEO_MODULE_ID}")
    add_library(${neogeo_game_target} MODULE
        ${neogeo_aot_sources}
        ${neogeo_z80_aot_sources}
        src/neogeo/kof98_game.c
        src/neogeo/kof98_module.cpp)
    target_include_directories(${neogeo_game_target} PRIVATE
        ${MULTIARCADERECOMP_NEOGEO_GENERATED_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/neogeo-runtime/include
        ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/z80-aot/runtime)
    set_target_properties(${neogeo_game_target} PROPERTIES
        PREFIX "" SUFFIX ".so"
        OUTPUT_NAME "multiarcaderecomp_game_kof98_${MULTIARCADERECOMP_NEOGEO_MODULE_ID}"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    target_link_libraries(${neogeo_game_target} PRIVATE multiarcaderecomp_neogeo_runtime)
    if(MULTIARCADERECOMP_NEOGEO_Z80_GENERATED_DIR)
        target_compile_definitions(${neogeo_game_target} PRIVATE MULTIARCADERECOMP_KOF98_HAS_Z80_AOT=1)
        target_include_directories(${neogeo_game_target} PRIVATE
            "${MULTIARCADERECOMP_NEOGEO_Z80_GENERATED_DIR}/bios"
            "${MULTIARCADERECOMP_NEOGEO_Z80_GENERATED_DIR}/cart")
    endif()
endif()

target_compile_options(multiarcaderecomp PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:$<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>>
)
