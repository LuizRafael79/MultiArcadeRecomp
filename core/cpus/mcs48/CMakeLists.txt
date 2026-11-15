add_library(multiarcaderecomp_mcs48 STATIC mcs48.cpp)
target_include_directories(multiarcaderecomp_mcs48 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
add_subdirectory(upd7751)
