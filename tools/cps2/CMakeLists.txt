cmake_minimum_required(VERSION 3.20)
project(multiarcaderecomp_cps2_module LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(CPS2RECOMP_SOURCE_DIR "" CACHE PATH "External CPS2Recomp source checkout")
set(CPS2RECOMP_WORK_DIR "" CACHE PATH "Generated per-game work directory")
set(CPS2RECOMP_GAME "sfa3" CACHE STRING "CPS2Recomp game id")
set(CPS2RECOMP_GAME_DIR "${CPS2RECOMP_SOURCE_DIR}/games/${CPS2RECOMP_GAME}" CACHE PATH "Per-game profile and sources")
option(CPS2RECOMP_INCLUDE_GAME_SOURCES "Link the upstream game's hand-written C++ sources into the module" ON)

if(NOT EXISTS "${CPS2RECOMP_SOURCE_DIR}/machine/machine.cpp")
    message(FATAL_ERROR "CPS2RECOMP_SOURCE_DIR must point to the CPS2Recomp checkout")
endif()
if(NOT EXISTS "${CPS2RECOMP_WORK_DIR}/gen/game_info.h")
    message(FATAL_ERROR "CPS2Recomp game metadata is missing; run the CPS2Recomp build first")
endif()
if(NOT EXISTS "${CPS2RECOMP_WORK_DIR}/multiarcaderecomp_runtime/recomp.cpp" OR
   NOT EXISTS "${CPS2RECOMP_WORK_DIR}/multiarcaderecomp_runtime/recomp_z80.cpp")
    message(FATAL_ERROR "CPS2 execution coverage instrumentation is missing")
endif()

find_package(SDL3 REQUIRED CONFIG)
find_package(ZLIB REQUIRED)

file(STRINGS "${CPS2RECOMP_SOURCE_DIR}/VERSION" RECOMP_VERSION LIMIT_COUNT 1)
file(GLOB GEN_SOURCES CONFIGURE_DEPENDS "${CPS2RECOMP_WORK_DIR}/gen/native_*.cpp")
file(GLOB GEN_Z80_SOURCES CONFIGURE_DEPENDS "${CPS2RECOMP_WORK_DIR}/gen/z80_*.cpp")
file(GLOB GAME_SOURCES CONFIGURE_DEPENDS "${CPS2RECOMP_GAME_DIR}/src/*.cpp")
if(CPS2RECOMP_LOCAL_PROFILE_DIR AND EXISTS "${CPS2RECOMP_LOCAL_PROFILE_DIR}/src")
    file(GLOB local_sources CONFIGURE_DEPENDS "${CPS2RECOMP_LOCAL_PROFILE_DIR}/src/*.cpp")
    list(APPEND GAME_SOURCES ${local_sources})
endif()
file(STRINGS "${CPS2RECOMP_GAME_DIR}/game.cfg" GAME_SHARES REGEX "^sources[ \\t]*=")
file(STRINGS "${CPS2RECOMP_GAME_DIR}/game.cfg" GAME_EXCLUDES REGEX "^exclude_sources[ \\t]*=")
if(GAME_EXCLUDES)
    string(REGEX REPLACE "^exclude_sources[ \\t]*=[ \\t]*" "" GAME_EXCLUDES "${GAME_EXCLUDES}")
    separate_arguments(GAME_EXCLUDES)
endif()
if(GAME_SHARES)
    string(REGEX REPLACE "^sources[ \\t]*=[ \\t]*" "" GAME_SHARES "${GAME_SHARES}")
    separate_arguments(GAME_SHARES)
    foreach(other ${GAME_SHARES})
        file(GLOB shared CONFIGURE_DEPENDS "${CPS2RECOMP_GAME_DIR}/../${other}/src/*.cpp")
        foreach(source ${shared})
            get_filename_component(source_name "${source}" NAME)
            list(FIND GAME_EXCLUDES "${other}/${source_name}" exclude_index)
            if(exclude_index EQUAL -1)
                list(APPEND GAME_SOURCES ${source})
            endif()
        endforeach()
        list(APPEND GAME_INCLUDE_DIRS "${CPS2RECOMP_GAME_DIR}/../${other}/src")
    endforeach()
endif()
if(NOT CPS2RECOMP_INCLUDE_GAME_SOURCES)
    set(GAME_SOURCES)
endif()
if(NOT GAME_SOURCES)
    list(APPEND GAME_SOURCES "${CPS2RECOMP_SOURCE_DIR}/runtime/no_game.cpp")
endif()

configure_file(multiarcaderecomp_cps2_game.h.in multiarcaderecomp_cps2_game.h @ONLY)

set(MACHINE_SOURCES
    "${CPS2RECOMP_SOURCE_DIR}/video/video.cpp"
    "${CPS2RECOMP_SOURCE_DIR}/sound/z80.cpp"
    "${CPS2RECOMP_SOURCE_DIR}/sound/qsound.cpp"
    "${CPS2RECOMP_SOURCE_DIR}/sound/driver.cpp"
    "${CPS2RECOMP_SOURCE_DIR}/machine/m68k.cpp"
    "${CPS2RECOMP_SOURCE_DIR}/machine/machine.cpp"
    "${CPS2RECOMP_SOURCE_DIR}/machine/sound_board.cpp"
    "${CPS2RECOMP_SOURCE_DIR}/machine/soundtrack.cpp"
    "${CPS2RECOMP_SOURCE_DIR}/machine/eeprom.cpp"
    "${CPS2RECOMP_SOURCE_DIR}/machine/romset.cpp"
    "${CPS2RECOMP_SOURCE_DIR}/machine/cps2crypt.cpp"
    "${CPS2RECOMP_SOURCE_DIR}/machine/kabuki.cpp"
    "${CPS2RECOMP_WORK_DIR}/multiarcaderecomp_runtime/recomp.cpp"
    "${CPS2RECOMP_WORK_DIR}/multiarcaderecomp_runtime/recomp_z80.cpp")

set(PORT_SOURCES
    "${CPS2RECOMP_SOURCE_DIR}/port/main.cpp"
    "${CPS2RECOMP_SOURCE_DIR}/port/frontend_sdl.cpp"
    "${CPS2RECOMP_SOURCE_DIR}/port/present.cpp"
    "${CPS2RECOMP_SOURCE_DIR}/port/shading.cpp"
    "${CPS2RECOMP_SOURCE_DIR}/port/zoom.cpp"
    "${CPS2RECOMP_SOURCE_DIR}/port/menu.cpp"
    "${CPS2RECOMP_SOURCE_DIR}/port/hud.cpp"
    "${CPS2RECOMP_SOURCE_DIR}/port/touch.cpp"
    "${CPS2RECOMP_SOURCE_DIR}/port/soundtrack.cpp"
    "${CPS2RECOMP_SOURCE_DIR}/port/netplay.cpp"
    "${CPS2RECOMP_SOURCE_DIR}/recomp-common/launcher/src/online.cpp"
    "${CPS2RECOMP_SOURCE_DIR}/recomp-common/launcher/src/run_program.cpp"
    "${CPS2RECOMP_SOURCE_DIR}/port/decoders.cpp"
    "${CPS2RECOMP_SOURCE_DIR}/recomp-common/menu.cpp"
    "${CPS2RECOMP_SOURCE_DIR}/recomp-common/settings.cpp"
    "${CPS2RECOMP_SOURCE_DIR}/runtime/game.cpp"
    "${CPS2RECOMP_SOURCE_DIR}/runtime/board.cpp"
    "${CPS2RECOMP_SOURCE_DIR}/runtime/spare.cpp"
    "${CPS2RECOMP_SOURCE_DIR}/fight/frames.cpp"
    "${CPS2RECOMP_SOURCE_DIR}/fight/training.cpp"
    "${CPS2RECOMP_SOURCE_DIR}/fight/colours.cpp"
    "${CPS2RECOMP_SOURCE_DIR}/fight/colour_editor.cpp"
    "${CPS2RECOMP_SOURCE_DIR}/fight/battle.cpp"
    "${CPS2RECOMP_SOURCE_DIR}/fight/alternative.cpp"
    "${CPS2RECOMP_SOURCE_DIR}/fight/session.cpp"
    "${CPS2RECOMP_SOURCE_DIR}/fight/tag.cpp"
    "${CPS2RECOMP_SOURCE_DIR}/fight/marvel.cpp"
    "${CPS2RECOMP_SOURCE_DIR}/beat/modes.cpp"
    "${CPS2RECOMP_SOURCE_DIR}/beat/model.cpp"
    "${CPS2RECOMP_SOURCE_DIR}/model/model.cpp"
    ${GAME_SOURCES})

set_source_files_properties("${CPS2RECOMP_SOURCE_DIR}/port/main.cpp"
    PROPERTIES COMPILE_DEFINITIONS "main=cps2recomp_${CPS2RECOMP_GAME}_main")
set_source_files_properties("${CPS2RECOMP_SOURCE_DIR}/port/decoders.cpp"
    PROPERTIES COMPILE_OPTIONS "-w")

set(MODULE_TARGET "multiarcaderecomp_cps2_${CPS2RECOMP_GAME}")
add_library(${MODULE_TARGET} SHARED
    ${PORT_SOURCES} ${MACHINE_SOURCES} ${GEN_SOURCES} ${GEN_Z80_SOURCES}
    module_entry.cpp)
target_include_directories(${MODULE_TARGET} PRIVATE
    "${CPS2RECOMP_SOURCE_DIR}/runtime"
    "${CPS2RECOMP_SOURCE_DIR}/port"
    "${CPS2RECOMP_SOURCE_DIR}/machine"
    "${CPS2RECOMP_SOURCE_DIR}/fight"
    "${CPS2RECOMP_SOURCE_DIR}/beat"
    "${CPS2RECOMP_SOURCE_DIR}/model"
    "${CPS2RECOMP_WORK_DIR}/gen"
    "${CPS2RECOMP_GAME_DIR}"
    ${GAME_INCLUDE_DIRS}
    "${CMAKE_CURRENT_BINARY_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(${MODULE_TARGET} PRIVATE SDL3::SDL3 ZLIB::ZLIB)
target_compile_options(${MODULE_TARGET} PRIVATE
    -Wall -Wextra -Wno-unused-parameter -Wno-unused-label -Wno-unused-variable -Wno-parentheses-equality -fsigned-char)
target_compile_definitions(${MODULE_TARGET} PRIVATE RECOMP_VERSION="${RECOMP_VERSION}")
set_target_properties(${MODULE_TARGET} PROPERTIES
    OUTPUT_NAME "multiarcaderecomp_cps2_${CPS2RECOMP_GAME}"
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES)
