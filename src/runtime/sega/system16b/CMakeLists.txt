add_library(multiarcaderecomp_sega_system16b_video_backend INTERFACE)
target_include_directories(multiarcaderecomp_sega_system16b_video_backend INTERFACE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/core
    ${PROJECT_SOURCE_DIR}/core/chips/sega_315_5197)

if(MULTIARCADERECOMP_ENABLE_OPENGL_VIDEO)
    find_package(OpenGL QUIET)
    if(SDL3_FOUND AND OpenGL_FOUND)
        add_library(multiarcaderecomp_sega_system16b_video_gl STATIC system16b_video_gl.cpp)
        target_include_directories(multiarcaderecomp_sega_system16b_video_gl PRIVATE
            ${PROJECT_SOURCE_DIR}/src
            ${PROJECT_SOURCE_DIR}/core
            ${PROJECT_SOURCE_DIR}/core/chips/sega_315_5197)
        target_link_libraries(multiarcaderecomp_sega_system16b_video_gl PRIVATE SDL3::SDL3 OpenGL::GL)
        target_compile_definitions(multiarcaderecomp_sega_system16b_video_gl PRIVATE
            MULTIARCADERECOMP_HAS_OPENGL_VIDEO=1
            MULTIARCADERECOMP_SYSTEM16B_SHADER_PATH="${PROJECT_BINARY_DIR}/system16b_tilemap.comp"
            MULTIARCADERECOMP_SYSTEM16B_SPRITE_MIX_SHADER_PATH="${PROJECT_BINARY_DIR}/system16b_sprite_mix.comp")
        target_compile_definitions(multiarcaderecomp_sega_system16b_video_backend INTERFACE MULTIARCADERECOMP_HAS_OPENGL_VIDEO=1)
        target_compile_definitions(multiarcaderecomp_sega_presentation PRIVATE MULTIARCADERECOMP_HAS_OPENGL_VIDEO=1)
        target_link_libraries(multiarcaderecomp_sega_system16b_video_backend INTERFACE multiarcaderecomp_sega_system16b_video_gl)
        configure_file(system16b_tilemap.comp ${PROJECT_BINARY_DIR}/system16b_tilemap.comp COPYONLY)
        configure_file(system16b_sprite_mix.comp ${PROJECT_BINARY_DIR}/system16b_sprite_mix.comp COPYONLY)
        message(STATUS "MultiArcadeRecomp System16B OpenGL tilemap backend enabled")
    endif()
endif()

add_library(multiarcaderecomp_sega_runtime_system16b MODULE board_runtime.cpp fd1094_musashi_binding.cpp)
set_target_properties(multiarcaderecomp_sega_runtime_system16b PROPERTIES
    PREFIX "" OUTPUT_NAME "multiarcaderecomp_system16b_runtime"
    LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
target_include_directories(multiarcaderecomp_sega_runtime_system16b PUBLIC ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/core)
target_link_libraries(multiarcaderecomp_sega_runtime_system16b PUBLIC
    multiarcaderecomp_board_system16b multiarcaderecomp_sega_board_common multiarcaderecomp_sega_profiles multiarcaderecomp_chip_sega_i8751
    multiarcaderecomp_fd1094 multiarcaderecomp_m68k_musashi)
