add_library(mamerecomp_sega_runtime_system18 MODULE board_runtime.cpp)
set_target_properties(mamerecomp_sega_runtime_system18 PROPERTIES
    PREFIX "" OUTPUT_NAME "mamerecomp_system18_runtime"
    LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
target_include_directories(mamerecomp_sega_runtime_system18 PUBLIC ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/core)
target_link_libraries(mamerecomp_sega_runtime_system18 PUBLIC
    mamerecomp_board_system18 mamerecomp_sega_board_common mamerecomp_sega_graphics_decode
    mamerecomp_sega_profiles mamerecomp_chip_sega_i8751)
