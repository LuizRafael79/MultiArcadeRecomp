add_library(multiarcaderecomp_sega_board_common STATIC
    common/sega_address_space.cpp
    common/video_output.cpp)
target_include_directories(multiarcaderecomp_sega_board_common PUBLIC ${PROJECT_SOURCE_DIR}/src)

add_library(multiarcaderecomp_sega_graphics_decode STATIC common/graphics_decode.cpp)
target_include_directories(multiarcaderecomp_sega_graphics_decode PUBLIC ${PROJECT_SOURCE_DIR}/src)

add_subdirectory(system16a)
add_subdirectory(system16b)
add_subdirectory(system18)


