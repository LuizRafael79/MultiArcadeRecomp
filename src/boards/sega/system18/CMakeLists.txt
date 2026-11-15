add_library(mamerecomp_board_system18 STATIC
    system18_board.cpp
    system18_bus.cpp
    system18_sound.cpp
    video_session.cpp)
target_include_directories(mamerecomp_board_system18 PUBLIC
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/core
    ${PROJECT_SOURCE_DIR}/core/cpus/z80)
target_link_libraries(mamerecomp_board_system18 PUBLIC
    mamerecomp_sega_board_common
    mamerecomp_chip_3155195
    mamerecomp_chip_3155296
    mamerecomp_chip_3155313
    mamerecomp_chip_sega_315_5197
    mamerecomp_chip_sega_315_5196
    mamerecomp_chip_rf5c68
    mamerecomp_chip_sega_psg
    mamerecomp_chip_ymfm_timer
    mamerecomp_z80_core
    ymfm_opn)
