add_subdirectory(debugger)
add_subdirectory(profiles)
add_subdirectory(presentation)
add_subdirectory(system16a)
add_subdirectory(system16b)
add_subdirectory(system18)

add_library(mamerecomp_sega_runtime INTERFACE)
target_link_libraries(mamerecomp_sega_runtime INTERFACE
    mamerecomp_sega_profiles)
