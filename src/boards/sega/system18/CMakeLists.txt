add_library(multiarcaderecomp_board_system18 STATIC
    system18_board.cpp
    system18_bus.cpp
    system18_sound.cpp
    video_session.cpp)
target_include_directories(multiarcaderecomp_board_system18 PUBLIC
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/core
    ${PROJECT_SOURCE_DIR}/core/cpus/z80)
target_link_libraries(multiarcaderecomp_board_system18 PUBLIC
    multiarcaderecomp_sega_board_common
    multiarcaderecomp_chip_3155195
    multiarcaderecomp_chip_3155296
    multiarcaderecomp_chip_3155313
    multiarcaderecomp_chip_sega_315_5197
    multiarcaderecomp_chip_sega_315_5196
    multiarcaderecomp_chip_rf5c68
    multiarcaderecomp_chip_sega_psg
    multiarcaderecomp_chip_ymfm_timer
    multiarcaderecomp_z80_core
    ymfm_opn)
