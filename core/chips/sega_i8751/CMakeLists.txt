add_library(mamerecomp_chip_sega_i8751 STATIC sega_i8751.cpp)
target_include_directories(mamerecomp_chip_sega_i8751 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/core)
target_link_libraries(mamerecomp_chip_sega_i8751 PUBLIC mamerecomp_mcs51_core)
