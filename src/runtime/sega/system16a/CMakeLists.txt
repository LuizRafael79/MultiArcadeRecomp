add_library(multiarcaderecomp_sega_system16a_video_backend INTERFACE)
target_include_directories(multiarcaderecomp_sega_system16a_video_backend INTERFACE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/core)

if(MULTIARCADERECOMP_ENABLE_OPENGL_VIDEO)
    find_package(OpenGL QUIET)
    if(SDL3_FOUND AND OpenGL_FOUND)
        add_library(multiarcaderecomp_sega_system16a_video_gl STATIC system16a_video_gl.cpp)
        target_include_directories(multiarcaderecomp_sega_system16a_video_gl PRIVATE
            ${PROJECT_SOURCE_DIR}/src
            ${PROJECT_SOURCE_DIR}/core)
        target_link_libraries(multiarcaderecomp_sega_system16a_video_gl PRIVATE SDL3::SDL3 OpenGL::GL)
        target_compile_definitions(multiarcaderecomp_sega_system16a_video_gl PRIVATE
            MULTIARCADERECOMP_HAS_OPENGL_VIDEO=1
            MULTIARCADERECOMP_SYSTEM16A_SHADER_PATH="${PROJECT_BINARY_DIR}/system16a_tilemap.comp")
        target_compile_definitions(multiarcaderecomp_sega_system16a_video_backend INTERFACE MULTIARCADERECOMP_HAS_OPENGL_VIDEO=1)
        target_compile_definitions(multiarcaderecomp_sega_presentation PRIVATE MULTIARCADERECOMP_HAS_OPENGL_VIDEO=1)
        target_link_libraries(multiarcaderecomp_sega_system16a_video_backend INTERFACE multiarcaderecomp_sega_system16a_video_gl)
        configure_file(system16a_tilemap.comp ${PROJECT_BINARY_DIR}/system16a_tilemap.comp COPYONLY)
        message(STATUS "MultiArcadeRecomp System16A OpenGL tilemap backend enabled")
    endif()
endif()

add_library(multiarcaderecomp_sega_runtime_system16a MODULE board_runtime.cpp)
set_target_properties(multiarcaderecomp_sega_runtime_system16a PROPERTIES
    PREFIX "" OUTPUT_NAME "multiarcaderecomp_system16a_runtime"
    LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
target_include_directories(multiarcaderecomp_sega_runtime_system16a PUBLIC ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/core)
target_link_libraries(multiarcaderecomp_sega_runtime_system16a PUBLIC
    multiarcaderecomp_board_system16a multiarcaderecomp_sega_board_common multiarcaderecomp_sega_profiles)
