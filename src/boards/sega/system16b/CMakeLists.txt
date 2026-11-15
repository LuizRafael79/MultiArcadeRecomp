add_library(multiarcaderecomp_board_system16b STATIC
    rom_mapper.cpp
    sound_board.cpp
    video.cpp
    video_session.cpp)
target_include_directories(multiarcaderecomp_board_system16b PUBLIC
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/core
    ${PROJECT_SOURCE_DIR}/core/cpus/z80)
target_link_libraries(multiarcaderecomp_board_system16b PUBLIC
    multiarcaderecomp_sega_board_common
    multiarcaderecomp_sega_graphics_decode
    multiarcaderecomp_sega_system16b_video_backend
    multiarcaderecomp_chip_3155195
    multiarcaderecomp_chip_sega_315_5248
    multiarcaderecomp_chip_sega_315_5250
    multiarcaderecomp_chip_ymfm_timer
    multiarcaderecomp_chip_sega_315_5196
    multiarcaderecomp_chip_sega_315_5197
    multiarcaderecomp_z80_core multiarcaderecomp_upd7759 ymfm_opm)
