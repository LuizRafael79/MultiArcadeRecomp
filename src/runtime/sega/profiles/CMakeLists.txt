file(GLOB_RECURSE MAMERECOMP_SEGA_ROM_MANIFESTS CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/src/romsets/sega/*.toml")
set(MAMERECOMP_SEGA_PROFILES_CPP
    "${CMAKE_CURRENT_BINARY_DIR}/generated/system_profiles.cpp")
add_custom_command(
    OUTPUT "${MAMERECOMP_SEGA_PROFILES_CPP}"
    COMMAND "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tools/generators/romsets/emit_system_profiles.py"
        --manifest-dir "${PROJECT_SOURCE_DIR}/src/romsets/sega"
        --output "${MAMERECOMP_SEGA_PROFILES_CPP}"
    DEPENDS
        "${PROJECT_SOURCE_DIR}/tools/generators/romsets/emit_system_profiles.py"
        "${PROJECT_SOURCE_DIR}/tools/generators/romsets/manifest_io.py"
        ${MAMERECOMP_SEGA_ROM_MANIFESTS}
    VERBATIM)

add_library(mamerecomp_sega_profiles STATIC
    sega_profiles.cpp
    ${MAMERECOMP_SEGA_PROFILES_CPP})
add_custom_target(mamerecomp_sega_profiles_codegen DEPENDS "${MAMERECOMP_SEGA_PROFILES_CPP}")
add_dependencies(mamerecomp_sega_profiles mamerecomp_sega_profiles_codegen)
target_include_directories(mamerecomp_sega_profiles PUBLIC
    ${PROJECT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/core/roms)
target_link_libraries(mamerecomp_sega_profiles PUBLIC
    LibArchive::LibArchive ZLIB::ZLIB mamerecomp_rom_hash)
