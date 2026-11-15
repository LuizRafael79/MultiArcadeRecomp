add_library(multiarcaderecomp_sega_m68k_debugger STATIC m68k_debugger.cpp)
target_include_directories(multiarcaderecomp_sega_m68k_debugger PUBLIC
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/core
    ${PROJECT_SOURCE_DIR}/core/third_party/m68krecomp/include)
target_link_libraries(multiarcaderecomp_sega_m68k_debugger PUBLIC multiarcaderecomp_sega_board_common)
