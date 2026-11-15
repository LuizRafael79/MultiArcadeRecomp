add_library(multiarcaderecomp_sega_runner STATIC runner.cpp command.cpp rom_commands.cpp board_runtime_factory.cpp
    generated_board_module.cpp module_location.cpp ../board_registry.cpp)
target_include_directories(multiarcaderecomp_sega_runner PUBLIC
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/core
    ${PROJECT_SOURCE_DIR}/core/cpus/z80
    ${PROJECT_SOURCE_DIR}/core/third_party/z80-aot/runtime
    ${PROJECT_SOURCE_DIR}/core/third_party/m68krecomp/include)
target_compile_definitions(multiarcaderecomp_sega_runner PUBLIC MULTIARCADERECOMP_HAS_GENERATED_RECOMP=1)
if(MULTIARCADERECOMP_ENABLE_OPENGL_VIDEO AND SDL3_FOUND)
    find_package(OpenGL QUIET)
    if(OpenGL_FOUND)
        target_compile_definitions(multiarcaderecomp_sega_runner PRIVATE MULTIARCADERECOMP_SEGA_OPENGL_VIDEO_AVAILABLE=1)
    endif()
endif()
target_compile_definitions(multiarcaderecomp_sega_runner PRIVATE
    MULTIARCADERECOMP_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
    MULTIARCADERECOMP_BINARY_DIR="${PROJECT_BINARY_DIR}"
    MULTIARCADERECOMP_PYTHON_EXECUTABLE="${Python3_EXECUTABLE}")
target_link_libraries(multiarcaderecomp_sega_runner PUBLIC
    multiarcaderecomp_sega_runtime
    multiarcaderecomp_sega_m68k_debugger
    multiarcaderecomp_sega_presentation
    multiarcaderecomp_sega_board_common
    multiarcaderecomp_m68k_recomp_runtime
    multiarcaderecomp_m68k_musashi
    m68krecomp_frontend
    multiarcaderecomp_z80_core
    m68k_cpu_runtime)
if(MULTIARCADERECOMP_ENABLE_WINDOW AND SDL3_FOUND)
    target_compile_definitions(multiarcaderecomp_sega_runner PRIVATE MULTIARCADERECOMP_HAS_SDL3_PRESENTER=1)
    target_link_libraries(multiarcaderecomp_sega_runner PRIVATE SDL3::SDL3)
endif()
