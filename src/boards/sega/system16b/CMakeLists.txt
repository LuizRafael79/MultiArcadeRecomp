add_library(mamerecomp_board_system16b STATIC
    rom_mapper.cpp
    sound_board.cpp
    video.cpp
    video_session.cpp)
target_include_directories(mamerecomp_board_system16b PUBLIC
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/core
    ${PROJECT_SOURCE_DIR}/core/cpus/z80)
target_link_libraries(mamerecomp_board_system16b PUBLIC
    mamerecomp_sega_board_common
    mamerecomp_sega_graphics_decode
    mamerecomp_sega_system16b_video_backend
    mamerecomp_chip_3155195
    mamerecomp_chip_sega_315_5248
    mamerecomp_chip_sega_315_5250
    mamerecomp_chip_ymfm_timer
    mamerecomp_chip_sega_315_5196
    mamerecomp_chip_sega_315_5197
    mamerecomp_z80_core mamerecomp_upd7759 ymfm_opm)
