add_library(multiarcaderecomp_chip_sega_i8751 STATIC sega_i8751.cpp)
target_include_directories(multiarcaderecomp_chip_sega_i8751 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/core)
target_link_libraries(multiarcaderecomp_chip_sega_i8751 PUBLIC multiarcaderecomp_mcs51_core)
