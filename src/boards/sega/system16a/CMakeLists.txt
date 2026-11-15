add_library(multiarcaderecomp_board_system16a STATIC
    address_space.cpp
    sound_board.cpp
    video.cpp
    video_session.cpp)
target_include_directories(multiarcaderecomp_board_system16a PUBLIC
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/core
    ${PROJECT_SOURCE_DIR}/core/cpus/z80)
target_link_libraries(multiarcaderecomp_board_system16a PUBLIC
    multiarcaderecomp_sega_board_common
    multiarcaderecomp_sega_graphics_decode
    multiarcaderecomp_sega_system16a_video_backend
    multiarcaderecomp_chip_sega_315_5049
    multiarcaderecomp_chip_sega_315_5011
    multiarcaderecomp_chip_sega_315_5012
    multiarcaderecomp_chip_i8255
    multiarcaderecomp_chip_i8243
    multiarcaderecomp_chip_ymfm_timer
    multiarcaderecomp_cpu_upd7751 multiarcaderecomp_z80_core multiarcaderecomp_audio_dac ymfm_opm)
