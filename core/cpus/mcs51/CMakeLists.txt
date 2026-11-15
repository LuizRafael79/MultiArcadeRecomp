add_library(multiarcaderecomp_mcs51_core STATIC
    mcs51.cpp
    mcs51_ops.cpp)
target_include_directories(multiarcaderecomp_mcs51_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

