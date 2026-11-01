cmake_minimum_required(VERSION 3.20)
project(MameRecomp LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(LibArchive REQUIRED)
find_package(ZLIB REQUIRED)
option(MAMERECOMP_ENABLE_OPENGL_VIDEO "Build the optional OpenGL tilemap renderer" ON)
option(MAMERECOMP_ENABLE_WINDOW "Build SDL3 live window presentation when SDL3 is available" ON)

set(MAMERECOMP_GENERATED_DIR "" CACHE PATH "Legacy: one game's m68krecomp output (prefer board-specific settings)")
set(MAMERECOMP_SYSTEM16A_GENERATED_DIRS "" CACHE STRING "Semicolon-separated System 16A game translation directories")
set(MAMERECOMP_SYSTEM16B_GENERATED_DIRS "" CACHE STRING "Semicolon-separated System 16B game translation directories")
set(MAMERECOMP_SYSTEM18_GENERATED_DIRS "" CACHE STRING "Semicolon-separated System 18 game translation directories")
set(MAMERECOMP_Z80_AOT_GENERATED_DIR "" CACHE PATH "Generated native Z80 source directory for one game")
set(MAMERECOMP_CPS1_M68K_GENERATED_DIR "" CACHE PATH "Generated CPS-1 68000 source directory for one game")
set(MAMERECOMP_CPS1_GAME_ID "unknown" CACHE STRING "CPS-1 game identifier used for generated game module output")
set(MAMERECOMP_NEOGEO_GENERATED_DIR "" CACHE PATH "KOF98 Neo Geo AOT output directory")
set(MAMERECOMP_NEOGEO_Z80_GENERATED_DIR "" CACHE PATH "KOF98 Neo Geo Z80 AOT output root (BIOS and cart subdirectories)")
set(MAMERECOMP_TMS34010_AOT_SOURCE "" CACHE FILEPATH "Generated per-game TMS34010 AOT C++ source for the T-Unit board module")
set(MAMERECOMP_M6809_AOT_SOURCE "" CACHE FILEPATH "Generated per-game MC6809(E) sound CPU AOT C++ source")
set(MAMERECOMP_ADSP2105_AOT_SOURCE "" CACHE FILEPATH "Generated per-game ADSP-2105 sound CPU AOT C++ source")
set(MAMERECOMP_MK2_TMS34010_AOT_SOURCE "" CACHE FILEPATH "Generated Mortal Kombat II TMS34010 AOT C++ source")
set(MAMERECOMP_NAOMI_TRANSLATED_SOURCES "" CACHE STRING "Semicolon-separated generated Naomi SH-4 C++ sources")
option(MAMERECOMP_NAOMI_HAS_BOOT_RAM_TRANSLATION "Build the captured, decrypted Naomi boot-RAM translation" OFF)
option(MAMERECOMP_NAOMI_HAS_MAIN_ROM_TRANSLATION "Build the MvC2 REIOS diagnostic game-ROM translation" OFF)
option(MAMERECOMP_NAOMI_HAS_OBSERVED_ROM_ROOTS "Build observed MvC2 ROM-backed dynamic roots" OFF)

add_executable(mamerecomp
    src/main.cpp
    src/naomi/naomi_module.cpp
    src/naomi/naomi_romset.cpp
    src/system16/m68k_debugger.cpp
    core/cpus/m68000/m68k.cpp
    src/system16/system16.cpp
    src/system16/system16_profiles.cpp
    src/system18/system18_board.cpp
    src/system18/system18_bus.cpp
    src/romsets/shinobi.cpp
    src/romsets/shinobi5.cpp
    src/romsets/goldnaxe.cpp
    src/romsets/goldnaxeud.cpp
    src/romsets/goldnaxe_shared.cpp
    src/romsets/shdancer.cpp
    src/romsets/mwalkd.cpp
    src/system16/system16_video.cpp
    src/system16/system16_bus.cpp
    src/system16/system16b_mapper.cpp
    src/system16/system16_config.cpp
    src/system16/system16a_sound.cpp
    src/system16/system16b_sound.cpp
    src/system18/system18_sound.cpp
    core/cpus/mcs48/mcs48.cpp
    src/system16/system16_m68k_bus.cpp
)

# Midway T-Unit board adapter for the shared TMS34010 core. The generated
# per-game CPU tables will be linked by the Mortal Kombat build pipeline;
# board memory, VRAM, I/O registers and graphics-ROM windows stay in this SO.
add_library(mamerecomp_tunit SHARED
    core/cpus/tms34010/interpreter.cpp
    core/cpus/tms34010/mame_interpreter.cpp
    core/cpus/interpreter_trace.cpp
    src/mk/tunit.cpp
    src/mk/williams_adpcm.cpp
    src/mk/mk_machine.cpp)
if(MAMERECOMP_TMS34010_AOT_SOURCE)
    if(NOT EXISTS "${MAMERECOMP_TMS34010_AOT_SOURCE}")
        message(FATAL_ERROR "TMS34010 AOT source does not exist: ${MAMERECOMP_TMS34010_AOT_SOURCE}")
    endif()
    target_sources(mamerecomp_tunit PRIVATE "${MAMERECOMP_TMS34010_AOT_SOURCE}")
    target_compile_definitions(mamerecomp_tunit PUBLIC MAMERECOMP_TMS34010_HAS_AOT=1)

    # Bounded developer runner for tracing reachable MK code and inspecting a
    # post-run T-Unit VRAM snapshot. This is not the interactive machine app.
    add_executable(mamerecomp_mk_trace tools/mk/trace.cpp)
    target_link_libraries(mamerecomp_mk_trace PRIVATE mamerecomp_tunit)
    set_target_properties(mamerecomp_mk_trace PROPERTIES
        BUILD_RPATH "$ORIGIN")
endif()
target_include_directories(mamerecomp_tunit PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/core)
set_target_properties(mamerecomp_tunit PROPERTIES
    OUTPUT_NAME mamerecomp_tunit
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
target_link_libraries(mamerecomp_tunit PRIVATE ymfm_opm)

# Shared MC6809/MC6809E fallback backend. It contains CPU semantics only;
# the MK sound board remains in mamerecomp_tunit and is accessed via a bus.
add_library(mamerecomp_m6809 STATIC core/cpus/m6809/interpreter.cpp)
target_include_directories(mamerecomp_m6809 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/core)
if(TARGET mamerecomp_mk_trace)
    target_link_libraries(mamerecomp_mk_trace PRIVATE mamerecomp_m6809)
endif()

# Shared ADSP-2105 CPU backend for DCS audio boards. Keep the DCS host protocol,
# ROM banking and DAC runtime in the board module, outside this CPU library.
if(MAMERECOMP_ADSP2105_AOT_SOURCE)
    if(NOT EXISTS "${MAMERECOMP_ADSP2105_AOT_SOURCE}")
        message(FATAL_ERROR "ADSP-2105 AOT source does not exist: ${MAMERECOMP_ADSP2105_AOT_SOURCE}")
    endif()
endif()
add_library(mamerecomp_adsp2105 STATIC
    core/cpus/adsp2105/interpreter.cpp
    core/cpus/adsp2105/runtime.cpp)
target_include_directories(mamerecomp_adsp2105 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/core)

# MK2 gets an isolated T-Unit module so its generated TMS table and DCS audio
# adapter do not replace or alter the MK1 prototype's AOT module.
if(MAMERECOMP_MK2_TMS34010_AOT_SOURCE AND
   NOT EXISTS "${MAMERECOMP_MK2_TMS34010_AOT_SOURCE}")
    message(FATAL_ERROR "MK2 TMS34010 AOT source does not exist: ${MAMERECOMP_MK2_TMS34010_AOT_SOURCE}")
endif()
if(MAMERECOMP_MK2_TMS34010_AOT_SOURCE)
    set(MAMERECOMP_MK2_TMS_AOT_INPUT "${MAMERECOMP_MK2_TMS34010_AOT_SOURCE}")
else()
    set(MAMERECOMP_MK2_TMS_AOT_INPUT core/cpus/tms34010/aot_stub.cpp)
endif()
if(MAMERECOMP_ADSP2105_AOT_SOURCE)
    set(MAMERECOMP_MK2_ADSP_AOT_INPUT "${MAMERECOMP_ADSP2105_AOT_SOURCE}")
else()
    set(MAMERECOMP_MK2_ADSP_AOT_INPUT core/cpus/adsp2105/aot_stub.cpp)
endif()
add_library(mamerecomp_tunit_mk2 SHARED
    core/cpus/tms34010/interpreter.cpp
    core/cpus/tms34010/mame_interpreter.cpp
    ${MAMERECOMP_MK2_TMS_AOT_INPUT}
    core/cpus/interpreter_trace.cpp
    src/mk/tunit.cpp
    src/mk2/dcs2k.cpp
    src/mk2/protection.cpp
    src/mk2/mk2_machine.cpp
    ${MAMERECOMP_MK2_ADSP_AOT_INPUT})
if(MAMERECOMP_MK2_TMS34010_AOT_SOURCE)
    target_compile_definitions(mamerecomp_tunit_mk2 PUBLIC
        MAMERECOMP_TMS34010_HAS_AOT=1)
endif()
target_include_directories(mamerecomp_tunit_mk2 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/core)
target_link_libraries(mamerecomp_tunit_mk2 PRIVATE
    mamerecomp_adsp2105)
set_target_properties(mamerecomp_tunit_mk2 PROPERTIES
    OUTPUT_NAME mamerecomp_tunit_mk2
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
add_executable(mamerecomp_mk2_trace tools/mk2/trace.cpp)
target_link_libraries(mamerecomp_mk2_trace PRIVATE mamerecomp_tunit_mk2)
set_target_properties(mamerecomp_mk2_trace PROPERTIES BUILD_RPATH "$ORIGIN")

# The Williams ADPCM sound CPU is a shared MC6809(E) backend with a
# per-machine/per-game generated AOT table. Keep its translation artifact
# separate from the board library until the sound CPU scheduler is connected.
if(MAMERECOMP_M6809_AOT_SOURCE)
    if(NOT EXISTS "${MAMERECOMP_M6809_AOT_SOURCE}")
        message(FATAL_ERROR "MC6809(E) AOT source does not exist: ${MAMERECOMP_M6809_AOT_SOURCE}")
    endif()
    add_library(mamerecomp_m6809_aot STATIC "${MAMERECOMP_M6809_AOT_SOURCE}")
    target_include_directories(mamerecomp_m6809_aot PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/core)
    if(TARGET mamerecomp_mk_trace)
        target_link_libraries(mamerecomp_mk_trace PRIVATE mamerecomp_m6809_aot)
        target_compile_definitions(mamerecomp_mk_trace PRIVATE
            MAMERECOMP_M6809_HAS_AOT=1)
    endif()
endif()
target_include_directories(mamerecomp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/core/cpus/mcs48
    ${CMAKE_CURRENT_SOURCE_DIR}/core/cpus/z80)
target_compile_definitions(mamerecomp PRIVATE
    MAMERECOMP_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    MAMERECOMP_PYTHON_EXECUTABLE="${Python3_EXECUTABLE}")

# One shared Z80 AOT backend. Generated tables are per game; the CPU, decoder,
# instruction helpers and optional fallback are common across board profiles.
if(MAMERECOMP_Z80_AOT_GENERATED_DIR)
    file(GLOB MAMERECOMP_Z80_AOT_SOURCES CONFIGURE_DEPENDS
        "${MAMERECOMP_Z80_AOT_GENERATED_DIR}/z80_*.cpp")
    if(NOT EXISTS "${MAMERECOMP_Z80_AOT_GENERATED_DIR}/z80_table.cpp")
        message(FATAL_ERROR "Z80 AOT generated directory has no z80_table.cpp: ${MAMERECOMP_Z80_AOT_GENERATED_DIR}")
    endif()
    list(FILTER MAMERECOMP_Z80_AOT_SOURCES EXCLUDE REGEX "/z80_table\\.cpp$")
    list(APPEND MAMERECOMP_Z80_AOT_SOURCES
        "${MAMERECOMP_Z80_AOT_GENERATED_DIR}/z80_table.cpp"
        core/cpus/z80/native_registry.cpp)
    add_library(mamerecomp_z80_aot STATIC ${MAMERECOMP_Z80_AOT_SOURCES})
    target_include_directories(mamerecomp_z80_aot PRIVATE
        "${MAMERECOMP_Z80_AOT_GENERATED_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/z80-aot/runtime"
        "${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/z80-aot/sound")
    target_link_libraries(mamerecomp PRIVATE mamerecomp_z80_aot)
endif()

# CPS-1's 68000 pass currently emits the Neo Geo runtime ABI. The CPS adapter
# supplies that CPU helper ABI over a CPS-1 address map while a neutral 68k ABI
# is developed; it remains isolated from the Neo Geo game runtime.
if(MAMERECOMP_CPS1_M68K_GENERATED_DIR)
    file(GLOB MAMERECOMP_CPS1_M68K_SOURCES CONFIGURE_DEPENDS
        "${MAMERECOMP_CPS1_M68K_GENERATED_DIR}/sf2cerecomp_*.c")
    if(NOT MAMERECOMP_CPS1_M68K_SOURCES)
        message(FATAL_ERROR "CPS-1 68000 generated directory has no sf2cerecomp_*.c files: ${MAMERECOMP_CPS1_M68K_GENERATED_DIR}")
    endif()
    add_library(mamerecomp_cps1_m68k_aot STATIC ${MAMERECOMP_CPS1_M68K_SOURCES})
    target_include_directories(mamerecomp_cps1_m68k_aot PRIVATE
        "${MAMERECOMP_CPS1_M68K_GENERATED_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/neogeorecomp-runtime/include")
    # Keep host compilation tractable while the discovered translation table
    # is still expanding; generated routines remain native code at -O1.
    target_compile_options(mamerecomp_cps1_m68k_aot PRIVATE $<$<COMPILE_LANGUAGE:C>:-O1>)
    add_executable(cps1_boot src/cps1/cps1_boot.cpp)
    target_include_directories(cps1_boot PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/neogeorecomp-runtime/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/musashi"
        "${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/z80-aot/runtime"
        "${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/z80-aot/sound"
        "${CMAKE_CURRENT_SOURCE_DIR}/core/cpus/z80")
    target_link_libraries(cps1_boot PRIVATE mamerecomp_cps1_m68k_aot mamerecomp_z80_aot musashi mamerecomp_z80_core)
endif()

add_library(mamerecomp_chip_3155195 SHARED core/chips/315_5195/mapper.cpp)
target_include_directories(mamerecomp_chip_3155195 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core)
set_target_properties(mamerecomp_chip_3155195 PROPERTIES
    OUTPUT_NAME mamerecomp_chip_3155195
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_library(mamerecomp_chip_3155296 SHARED core/chips/315_5296/io.cpp)
target_include_directories(mamerecomp_chip_3155296 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core)
set_target_properties(mamerecomp_chip_3155296 PROPERTIES
    OUTPUT_NAME mamerecomp_chip_3155296
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_library(mamerecomp_chip_3155313 SHARED core/chips/315_5313/vdp.cpp)
target_include_directories(mamerecomp_chip_3155313 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core)
set_target_properties(mamerecomp_chip_3155313 PROPERTIES
    OUTPUT_NAME mamerecomp_chip_3155313
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# Naomi metadata, SH-4 execution state and board/game ABI are separate from
# the 68000 modules. The AOT dispatcher and device map are still in progress.
add_library(mamerecomp_naomi MODULE
    src/naomi/naomi_module.cpp
    src/naomi/naomi_address_space.cpp
    src/naomi/naomi_arm7.cpp
    src/naomi/naomi_x76f100.cpp
    src/naomi/naomi_sh4_context.cpp
    src/naomi/naomi_sh4_helpers.cpp
    src/naomi/naomi_pvr2.cpp
    src/naomi/naomi_holly.cpp
    src/naomi/naomi_jvs_837_13551.cpp
    src/naomi/naomi_m1crypt.cpp
    src/naomi/naomi_m2crypt.cpp
    src/naomi/naomi_reios_boot.cpp)
if(MAMERECOMP_NAOMI_TRANSLATED_SOURCES)
    target_sources(mamerecomp_naomi PRIVATE ${MAMERECOMP_NAOMI_TRANSLATED_SOURCES})
    target_compile_definitions(mamerecomp_naomi PRIVATE MAMERECOMP_NAOMI_HAS_TRANSLATED_SH4=1)
endif()
if(MAMERECOMP_NAOMI_HAS_BOOT_RAM_TRANSLATION)
    target_compile_definitions(mamerecomp_naomi PRIVATE MAMERECOMP_NAOMI_HAS_BOOT_RAM_TRANSLATION=1)
endif()
if(MAMERECOMP_NAOMI_HAS_MAIN_ROM_TRANSLATION)
    target_compile_definitions(mamerecomp_naomi PRIVATE MAMERECOMP_NAOMI_HAS_MAIN_ROM_TRANSLATION=1)
endif()
if(MAMERECOMP_NAOMI_HAS_OBSERVED_ROM_ROOTS)
    target_compile_definitions(mamerecomp_naomi PRIVATE MAMERECOMP_NAOMI_HAS_OBSERVED_ROM_ROOTS=1)
endif()
target_include_directories(mamerecomp_naomi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
set_target_properties(mamerecomp_naomi PROPERTIES
    PREFIX "" SUFFIX ".so"
    OUTPUT_NAME mamerecomp_naomi
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# Neo Geo has its own ROM/profile and board runtime module; it does not use the
# Sega System 16/18 bus or video implementations.
add_library(mamerecomp_neogeo SHARED src/neogeo/neogeo_romset.cpp)
target_sources(mamerecomp_neogeo PRIVATE src/neogeo/kof98_game.c)
target_include_directories(mamerecomp_neogeo PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/neogeorecomp-runtime/include
    ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/neogeorecomp-m68krecomp/include)
target_link_libraries(mamerecomp_neogeo PRIVATE LibArchive::LibArchive ZLIB::ZLIB)
set_target_properties(mamerecomp_neogeo PROPERTIES
    OUTPUT_NAME mamerecomp_neogeo
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

target_link_libraries(mamerecomp PRIVATE LibArchive::LibArchive ZLIB::ZLIB mamerecomp_neogeo m68k_cpu_runtime m68krecomp_frontend z80_core ymfm_opm ymfm_opn mamerecomp_chip_3155195 mamerecomp_chip_3155296 mamerecomp_chip_3155313 ${CMAKE_DL_LIBS})

if(MAMERECOMP_ENABLE_WINDOW OR MAMERECOMP_ENABLE_OPENGL_VIDEO)
    find_package(SDL3 CONFIG QUIET)
endif()

if(MAMERECOMP_ENABLE_WINDOW AND SDL3_FOUND)
    target_sources(mamerecomp PRIVATE
        src/system16/system16_presenter.cpp
        src/naomi/naomi_presenter.cpp
        core/third_party/imgui/imgui.cpp
        core/third_party/imgui/imgui_draw.cpp
        core/third_party/imgui/imgui_tables.cpp
        core/third_party/imgui/imgui_widgets.cpp
        core/third_party/imgui/backends/imgui_impl_sdl3.cpp
        core/third_party/imgui/backends/imgui_impl_sdlrenderer3.cpp)
    target_include_directories(mamerecomp PRIVATE core/third_party/imgui core/third_party/imgui/backends)
    target_link_libraries(mamerecomp PRIVATE SDL3::SDL3)
    target_compile_definitions(mamerecomp PRIVATE MAMERECOMP_HAS_SDL3_PRESENTER=1)
    message(STATUS "MameRecomp SDL3 live window presentation enabled")
    if(TARGET cps1_boot)
        target_link_libraries(cps1_boot PRIVATE SDL3::SDL3)
        target_compile_definitions(cps1_boot PRIVATE MAMERECOMP_CPS1_SDL=1)
    endif()
else()
    target_sources(mamerecomp PRIVATE src/system16/system16_presenter_stub.cpp)
endif()

if(MAMERECOMP_ENABLE_OPENGL_VIDEO)
    find_package(OpenGL QUIET)
    if(SDL3_FOUND AND OpenGL_FOUND)
        target_sources(mamerecomp PRIVATE src/system16/system16_video_gl.cpp)
        target_link_libraries(mamerecomp PRIVATE SDL3::SDL3 OpenGL::GL)
        configure_file(src/system16/system16a_tilemap.comp ${CMAKE_CURRENT_BINARY_DIR}/system16a_tilemap.comp COPYONLY)
        configure_file(src/system16/system16b_tilemap.comp ${CMAKE_CURRENT_BINARY_DIR}/system16b_tilemap.comp COPYONLY)
        configure_file(src/system16/system16b_sprite_mix.comp ${CMAKE_CURRENT_BINARY_DIR}/system16b_sprite_mix.comp COPYONLY)
        target_compile_definitions(mamerecomp PRIVATE
            MAMERECOMP_HAS_OPENGL_VIDEO=1
            MAMERECOMP_SYSTEM16A_SHADER_PATH="${CMAKE_CURRENT_BINARY_DIR}/system16a_tilemap.comp"
            MAMERECOMP_SYSTEM16B_SHADER_PATH="${CMAKE_CURRENT_BINARY_DIR}/system16b_tilemap.comp"
            MAMERECOMP_SYSTEM16B_SPRITE_MIX_SHADER_PATH="${CMAKE_CURRENT_BINARY_DIR}/system16b_sprite_mix.comp")
        message(STATUS "MameRecomp OpenGL tilemap backend enabled")
    else()
        message(STATUS "SDL3/OpenGL development files not found; building CPU video backend only")
    endif()
endif()

# Musashi (MIT) is the independent 68000 oracle/cycle source. Native code
# generation remains in m68krecomp_frontend.
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
    core/third_party/neogeorecomp-m68krecomp/runtime/musashi_glue.c)
target_include_directories(musashi PUBLIC
    ${MUSASHI_DIR}
    ${MUSASHI_GEN}
    ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/neogeorecomp-m68krecomp/runtime)
target_compile_definitions(musashi PUBLIC MUSASHI_CNF=\"ng_musashi_conf.h\")
target_link_libraries(musashi PUBLIC m)

# Full raw 68000 frontend/emitter/discovery from NeoGeoRecomp (MIT).
# Generated code uses the CPU ABI adapters built below; board devices remain ours.
add_library(m68krecomp_frontend STATIC
    core/third_party/neogeorecomp-m68krecomp/decode.c
    core/third_party/neogeorecomp-m68krecomp/emit.c
    core/third_party/neogeorecomp-m68krecomp/recomp.c
    core/third_party/neogeorecomp-m68krecomp/runtime/romload.c)
target_include_directories(m68krecomp_frontend
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/neogeorecomp-m68krecomp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/neogeorecomp-m68krecomp/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/neogeorecomp-m68krecomp/runtime)
target_link_libraries(m68krecomp_frontend PUBLIC musashi)
add_executable(m68krecomp core/third_party/neogeorecomp-m68krecomp/main.c)
target_link_libraries(m68krecomp PRIVATE m68krecomp_frontend)

# CPU register/flag helpers and exception entry are board-independent. The
# Neo Geo bus header is included only for stack helper declarations; the
# System 16 bus adapter will provide the actual memory operations.
add_library(m68k_cpu_runtime SHARED
    core/third_party/neogeorecomp-m68krecomp/runtime/cpu.c
    src/system16/system16_m68k_bus.c
    src/system16/m68k_recomp_dispatch.c
)
target_include_directories(m68k_cpu_runtime
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/neogeorecomp-m68krecomp/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)
set_target_properties(m68k_cpu_runtime PROPERTIES
    OUTPUT_NAME mamerecomp_core
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
target_link_options(mamerecomp PRIVATE "LINKER:-rpath,$ORIGIN")

function(add_mamerecomp_board_module target_name module_name revision_value generated_dirs)
    if(NOT generated_dirs)
        return()
    endif()
    set(module_sources)
    set(module_includes)
    set(module_prefixes)
    set(module_adapter "#include <cstring>\n#include <neogeorecomp/neogeo.h>\nextern \"C\" {\n")
    foreach(generated_dir IN LISTS generated_dirs)
        if(NOT IS_DIRECTORY "${generated_dir}")
            message(FATAL_ERROR "Generated-code directory does not exist: ${generated_dir}")
        endif()
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
        list(APPEND module_includes "${generated_dir}")
        list(APPEND module_prefixes "${game_prefix}")
    endforeach()
    string(APPEND module_adapter "}\nextern \"C\" unsigned mamerecomp_board_abi(void) { return 1; }\nextern \"C\" int mamerecomp_board_revision(void) { return ${revision_value}; }\nextern \"C\" const ng_func_entry_t* mamerecomp_generated_table(const char* profile_id, size_t* count) {\n")
    foreach(game_prefix IN LISTS module_prefixes)
        string(APPEND module_adapter "    if (std::strcmp(profile_id, \"${game_prefix}\") == 0) { *count = ${game_prefix}_ng_recomp_count; return ${game_prefix}_ng_recomp_table; }\n")
    endforeach()
    string(APPEND module_adapter "    *count = 0; return nullptr;\n}\n")
    set(adapter_path "${CMAKE_CURRENT_BINARY_DIR}/${target_name}_adapter.cpp")
    file(WRITE "${adapter_path}" "${module_adapter}")
    add_library(${target_name} MODULE ${module_sources} "${adapter_path}")
    target_include_directories(${target_name} PRIVATE
        ${module_includes}
        ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/neogeorecomp-m68krecomp/include)
    target_link_libraries(${target_name} PRIVATE m68k_cpu_runtime)
    set_target_properties(${target_name} PROPERTIES
        PREFIX "" SUFFIX ".so" OUTPUT_NAME "${module_name}"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        BUILD_RPATH "$ORIGIN")
    target_compile_options(${target_name} PRIVATE
        $<$<COMPILE_LANGUAGE:C>:$<$<C_COMPILER_ID:GNU,Clang>:-O3>>)
endfunction()

if(MAMERECOMP_GENERATED_DIR)
    file(GLOB legacy_tables "${MAMERECOMP_GENERATED_DIR}/*_recomp_table.c")
    if(legacy_tables)
        list(GET legacy_tables 0 legacy_table)
        get_filename_component(legacy_table_name "${legacy_table}" NAME)
        if(legacy_table_name MATCHES "^(goldnaxeud|shinobi5)_recomp_table\\.c$")
            set(MAMERECOMP_SYSTEM16B_GENERATED_DIRS "${MAMERECOMP_GENERATED_DIR}")
        elseif(legacy_table_name MATCHES "^shdancer_recomp_table\\.c$")
            set(MAMERECOMP_SYSTEM18_GENERATED_DIRS "${MAMERECOMP_GENERATED_DIR}")
        else()
            set(MAMERECOMP_SYSTEM16A_GENERATED_DIRS "${MAMERECOMP_GENERATED_DIR}")
        endif()
    endif()
endif()

add_mamerecomp_board_module(mamerecomp_system16a mamerecomp_system16a 16 "${MAMERECOMP_SYSTEM16A_GENERATED_DIRS}")
add_mamerecomp_board_module(mamerecomp_system16b mamerecomp_system16b 17 "${MAMERECOMP_SYSTEM16B_GENERATED_DIRS}")
add_mamerecomp_board_module(mamerecomp_system18 mamerecomp_system18 18 "${MAMERECOMP_SYSTEM18_GENERATED_DIRS}")
if(MAMERECOMP_SYSTEM16A_GENERATED_DIRS OR MAMERECOMP_SYSTEM16B_GENERATED_DIRS OR MAMERECOMP_SYSTEM18_GENERATED_DIRS)
    target_compile_definitions(mamerecomp PRIVATE MAMERECOMP_HAS_GENERATED_RECOMP=1)
    target_link_libraries(mamerecomp PRIVATE ${CMAKE_DL_LIBS})
endif()

# General-purpose Z80 interpreter from the NeoGeoRecomp dependency set (MIT).
add_library(z80_core STATIC core/third_party/superzazu-z80/z80.c)
target_include_directories(z80_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/superzazu-z80)

# Board-neutral native Z80 execution and interpreter fallback used directly by
# CPS-1; each board retains its own bus, banking, clocking, and device hooks.
add_library(mamerecomp_z80_core STATIC
    core/third_party/z80-aot/sound/z80.cpp
    core/third_party/z80-aot/runtime/recomp_z80.cpp)
target_include_directories(mamerecomp_z80_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/z80-aot/runtime
    ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/z80-aot/sound)

# YMFM's generic FM engine and YM2151 OPM implementation (BSD-3-Clause).
add_library(ymfm_opm STATIC core/third_party/ymfm/ymfm_opm.cpp)
target_include_directories(ymfm_opm SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/ymfm)

# YM3438 OPN implementation from the same YMFM revision as the existing OPM
# implementation. System 18 has two independent YM3438 devices.
add_library(ymfm_opn STATIC
    core/third_party/ymfm/ymfm_opn.cpp
    core/third_party/ymfm/ymfm_adpcm.cpp
    core/third_party/ymfm/ymfm_ssg.cpp
    core/third_party/ymfm/ymfm_misc.cpp)
target_include_directories(ymfm_opn SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/ymfm)

# The Neo Geo board runtime is kept separate from the Sega runners. It is the
# existing neogeorecomp MVS model, extended with the KOF98 cart callbacks.
add_library(neogeorecomp_runtime STATIC
    core/third_party/neogeorecomp-runtime/src/neogeo.c
    core/third_party/neogeorecomp-runtime/src/exec.c
    core/third_party/neogeorecomp-runtime/src/cpu.c
    core/third_party/neogeorecomp-runtime/src/bus.c
    core/third_party/neogeorecomp-runtime/src/lspc.c
    core/third_party/neogeorecomp-runtime/src/video.c
    core/third_party/neogeorecomp-runtime/src/io.c
    core/third_party/neogeorecomp-runtime/src/audio.c
    core/third_party/neogeorecomp-runtime/src/ym2610.cpp
    core/third_party/z80-aot/sound/z80.cpp
    core/third_party/z80-aot/runtime/recomp_z80.cpp
    src/neogeo/kof98_z80_host.cpp
    core/third_party/neogeorecomp-runtime/src/platform.c
    core/third_party/neogeorecomp-runtime/src/romload.c)
target_include_directories(neogeorecomp_runtime
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/neogeorecomp-runtime/include
        ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/neogeorecomp-m68krecomp/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/neogeorecomp-runtime/src
        ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/superzazu-z80
        ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/z80-aot/runtime
        ${CMAKE_CURRENT_SOURCE_DIR}/src/neogeo)
target_link_libraries(neogeorecomp_runtime PUBLIC musashi z80_core ymfm_opn)
find_package(SDL2 CONFIG QUIET)
if(TARGET SDL2::SDL2)
    target_sources(neogeorecomp_runtime PRIVATE core/third_party/neogeorecomp-runtime/src/window_sdl.c)
    target_compile_definitions(neogeorecomp_runtime PRIVATE NG_HAVE_SDL=1)
    target_link_libraries(neogeorecomp_runtime PUBLIC SDL2::SDL2)
endif()

add_executable(kof98_aot src/neogeo/kof98_aot_main.cpp)
target_link_libraries(kof98_aot PRIVATE mamerecomp_neogeo m68krecomp_frontend)

add_executable(kof98recomp src/neogeo/kof98_main.cpp)
target_link_libraries(kof98recomp PRIVATE mamerecomp_neogeo neogeorecomp_runtime)
if(MAMERECOMP_NEOGEO_GENERATED_DIR)
    if(NOT IS_DIRECTORY "${MAMERECOMP_NEOGEO_GENERATED_DIR}")
        message(FATAL_ERROR "Neo Geo AOT directory does not exist: ${MAMERECOMP_NEOGEO_GENERATED_DIR}")
    endif()
    file(GLOB neogeo_aot_sources CONFIGURE_DEPENDS
        "${MAMERECOMP_NEOGEO_GENERATED_DIR}/recomp_*.c")
    if(NOT neogeo_aot_sources)
        message(FATAL_ERROR "No KOF98 AOT C files found in ${MAMERECOMP_NEOGEO_GENERATED_DIR}")
    endif()
    set(neogeo_z80_aot_sources)
    if(MAMERECOMP_NEOGEO_Z80_GENERATED_DIR)
        file(GLOB_RECURSE neogeo_z80_aot_sources CONFIGURE_DEPENDS
            "${MAMERECOMP_NEOGEO_Z80_GENERATED_DIR}/*/z80_*.cpp")
        if(NOT neogeo_z80_aot_sources)
            message(FATAL_ERROR "No generated KOF98 Z80 sources found in ${MAMERECOMP_NEOGEO_Z80_GENERATED_DIR}")
        endif()
    endif()
    add_library(mamerecomp_game_kof98 MODULE
        ${neogeo_aot_sources}
        ${neogeo_z80_aot_sources}
        src/neogeo/kof98_module.cpp)
    target_include_directories(mamerecomp_game_kof98 PRIVATE
        ${MAMERECOMP_NEOGEO_GENERATED_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/neogeorecomp-runtime/include
        ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/z80-aot/runtime)
    set_target_properties(mamerecomp_game_kof98 PROPERTIES
        PREFIX "" SUFFIX ".so"
        OUTPUT_NAME "mamerecomp_game_kof98"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    add_dependencies(kof98recomp mamerecomp_game_kof98)
    target_link_libraries(kof98recomp PRIVATE ${CMAKE_DL_LIBS})
    # Generated code in the game module resolves the shared Neo Geo runtime
    # through the runner executable; do not link a second runtime into the .so.
    set_target_properties(kof98recomp PROPERTIES ENABLE_EXPORTS ON)
    target_compile_definitions(kof98recomp PRIVATE MAMERECOMP_KOF98_HAS_AOT=1)
    if(MAMERECOMP_NEOGEO_Z80_GENERATED_DIR)
        target_compile_definitions(mamerecomp_game_kof98 PRIVATE MAMERECOMP_KOF98_HAS_Z80_AOT=1)
        target_compile_definitions(kof98recomp PRIVATE MAMERECOMP_KOF98_HAS_Z80_AOT=1)
        target_include_directories(mamerecomp_game_kof98 PRIVATE
            "${MAMERECOMP_NEOGEO_Z80_GENERATED_DIR}/bios"
            "${MAMERECOMP_NEOGEO_Z80_GENERATED_DIR}/cart")
    endif()
endif()

target_compile_options(mamerecomp PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:$<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>>
)
