add_library(mamerecomp_board_system16a STATIC
    address_space.cpp
    sound_board.cpp
    video.cpp
    video_session.cpp)
target_include_directories(mamerecomp_board_system16a PUBLIC
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/core
    ${PROJECT_SOURCE_DIR}/core/cpus/z80)
target_link_libraries(mamerecomp_board_system16a PUBLIC
    mamerecomp_sega_board_common
    mamerecomp_sega_graphics_decode
    mamerecomp_sega_system16a_video_backend
    mamerecomp_chip_sega_315_5049
    mamerecomp_chip_sega_315_5011
    mamerecomp_chip_sega_315_5012
    mamerecomp_chip_i8255
    mamerecomp_chip_i8243
    mamerecomp_chip_ymfm_timer
    mamerecomp_cpu_upd7751 mamerecomp_z80_core mamerecomp_audio_dac ymfm_opm)
