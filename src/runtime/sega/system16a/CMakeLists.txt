add_library(mamerecomp_sega_system16a_video_backend INTERFACE)
target_include_directories(mamerecomp_sega_system16a_video_backend INTERFACE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/core)

if(MAMERECOMP_ENABLE_OPENGL_VIDEO)
    find_package(OpenGL QUIET)
    if(SDL3_FOUND AND OpenGL_FOUND)
        add_library(mamerecomp_sega_system16a_video_gl STATIC system16a_video_gl.cpp)
        target_include_directories(mamerecomp_sega_system16a_video_gl PRIVATE
            ${PROJECT_SOURCE_DIR}/src
            ${PROJECT_SOURCE_DIR}/core)
        target_link_libraries(mamerecomp_sega_system16a_video_gl PRIVATE SDL3::SDL3 OpenGL::GL)
        target_compile_definitions(mamerecomp_sega_system16a_video_gl PRIVATE
            MAMERECOMP_HAS_OPENGL_VIDEO=1
            MAMERECOMP_SYSTEM16A_SHADER_PATH="${PROJECT_BINARY_DIR}/system16a_tilemap.comp")
        target_compile_definitions(mamerecomp_sega_system16a_video_backend INTERFACE MAMERECOMP_HAS_OPENGL_VIDEO=1)
        target_compile_definitions(mamerecomp_sega_presentation PRIVATE MAMERECOMP_HAS_OPENGL_VIDEO=1)
        target_link_libraries(mamerecomp_sega_system16a_video_backend INTERFACE mamerecomp_sega_system16a_video_gl)
        configure_file(system16a_tilemap.comp ${PROJECT_BINARY_DIR}/system16a_tilemap.comp COPYONLY)
        message(STATUS "MameRecomp System16A OpenGL tilemap backend enabled")
    endif()
endif()

add_library(mamerecomp_sega_runtime_system16a MODULE board_runtime.cpp)
set_target_properties(mamerecomp_sega_runtime_system16a PROPERTIES
    PREFIX "" OUTPUT_NAME "mamerecomp_system16a_runtime"
    LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
target_include_directories(mamerecomp_sega_runtime_system16a PUBLIC ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/core)
target_link_libraries(mamerecomp_sega_runtime_system16a PUBLIC
    mamerecomp_board_system16a mamerecomp_sega_board_common mamerecomp_sega_profiles)
