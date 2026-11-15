add_library(mamerecomp_sega_system16b_video_backend INTERFACE)
target_include_directories(mamerecomp_sega_system16b_video_backend INTERFACE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/core
    ${PROJECT_SOURCE_DIR}/core/chips/sega_315_5197)

if(MAMERECOMP_ENABLE_OPENGL_VIDEO)
    find_package(OpenGL QUIET)
    if(SDL3_FOUND AND OpenGL_FOUND)
        add_library(mamerecomp_sega_system16b_video_gl STATIC system16b_video_gl.cpp)
        target_include_directories(mamerecomp_sega_system16b_video_gl PRIVATE
            ${PROJECT_SOURCE_DIR}/src
            ${PROJECT_SOURCE_DIR}/core
            ${PROJECT_SOURCE_DIR}/core/chips/sega_315_5197)
        target_link_libraries(mamerecomp_sega_system16b_video_gl PRIVATE SDL3::SDL3 OpenGL::GL)
        target_compile_definitions(mamerecomp_sega_system16b_video_gl PRIVATE
            MAMERECOMP_HAS_OPENGL_VIDEO=1
            MAMERECOMP_SYSTEM16B_SHADER_PATH="${PROJECT_BINARY_DIR}/system16b_tilemap.comp"
            MAMERECOMP_SYSTEM16B_SPRITE_MIX_SHADER_PATH="${PROJECT_BINARY_DIR}/system16b_sprite_mix.comp")
        target_compile_definitions(mamerecomp_sega_system16b_video_backend INTERFACE MAMERECOMP_HAS_OPENGL_VIDEO=1)
        target_compile_definitions(mamerecomp_sega_presentation PRIVATE MAMERECOMP_HAS_OPENGL_VIDEO=1)
        target_link_libraries(mamerecomp_sega_system16b_video_backend INTERFACE mamerecomp_sega_system16b_video_gl)
        configure_file(system16b_tilemap.comp ${PROJECT_BINARY_DIR}/system16b_tilemap.comp COPYONLY)
        configure_file(system16b_sprite_mix.comp ${PROJECT_BINARY_DIR}/system16b_sprite_mix.comp COPYONLY)
        message(STATUS "MameRecomp System16B OpenGL tilemap backend enabled")
    endif()
endif()

add_library(mamerecomp_sega_runtime_system16b MODULE board_runtime.cpp fd1094_musashi_binding.cpp)
set_target_properties(mamerecomp_sega_runtime_system16b PROPERTIES
    PREFIX "" OUTPUT_NAME "mamerecomp_system16b_runtime"
    LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
target_include_directories(mamerecomp_sega_runtime_system16b PUBLIC ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/core)
target_link_libraries(mamerecomp_sega_runtime_system16b PUBLIC
    mamerecomp_board_system16b mamerecomp_sega_board_common mamerecomp_sega_profiles mamerecomp_chip_sega_i8751
    mamerecomp_fd1094 mamerecomp_m68k_musashi)
