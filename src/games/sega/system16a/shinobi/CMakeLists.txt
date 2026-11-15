target_sources(${MAMERECOMP_GAME_MODULE_TARGET} PRIVATE ${CMAKE_CURRENT_LIST_DIR}/shinobi_config.cpp)
target_include_directories(${MAMERECOMP_GAME_MODULE_TARGET} PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/core/third_party/imgui)
