file(GLOB_RECURSE MULTIARCADERECOMP_SEGA_ROM_MANIFESTS CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/src/romsets/sega/*.toml")
set(MULTIARCADERECOMP_SEGA_PROFILES_CPP
    "${CMAKE_CURRENT_BINARY_DIR}/generated/system_profiles.cpp")
add_custom_command(
    OUTPUT "${MULTIARCADERECOMP_SEGA_PROFILES_CPP}"
    COMMAND "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tools/generators/romsets/emit_system_profiles.py"
        --manifest-dir "${PROJECT_SOURCE_DIR}/src/romsets/sega"
        --output "${MULTIARCADERECOMP_SEGA_PROFILES_CPP}"
    DEPENDS
        "${PROJECT_SOURCE_DIR}/tools/generators/romsets/emit_system_profiles.py"
        "${PROJECT_SOURCE_DIR}/tools/generators/romsets/manifest_io.py"
        ${MULTIARCADERECOMP_SEGA_ROM_MANIFESTS}
    VERBATIM)

add_library(multiarcaderecomp_sega_profiles STATIC
    sega_profiles.cpp
    ${MULTIARCADERECOMP_SEGA_PROFILES_CPP})
add_custom_target(multiarcaderecomp_sega_profiles_codegen DEPENDS "${MULTIARCADERECOMP_SEGA_PROFILES_CPP}")
add_dependencies(multiarcaderecomp_sega_profiles multiarcaderecomp_sega_profiles_codegen)
target_include_directories(multiarcaderecomp_sega_profiles PUBLIC
    ${PROJECT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/core/roms)
target_link_libraries(multiarcaderecomp_sega_profiles PUBLIC
    LibArchive::LibArchive ZLIB::ZLIB multiarcaderecomp_rom_hash)
