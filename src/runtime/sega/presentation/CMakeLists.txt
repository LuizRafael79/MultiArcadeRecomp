add_library(mamerecomp_sega_presentation STATIC
    sega_config.cpp
    sega_audio_output.cpp
)
target_include_directories(mamerecomp_sega_presentation PUBLIC
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/core)

find_path(MPG123_INCLUDE_DIR mpg123.h)
find_library(MPG123_LIBRARY NAMES mpg123)
if(MPG123_INCLUDE_DIR AND MPG123_LIBRARY)
    target_include_directories(mamerecomp_sega_presentation PRIVATE ${MPG123_INCLUDE_DIR})
    target_link_libraries(mamerecomp_sega_presentation PRIVATE ${MPG123_LIBRARY})
    target_compile_definitions(mamerecomp_sega_presentation PRIVATE MAMERECOMP_HAS_MPG123=1)
endif()

if(MAMERECOMP_ENABLE_WINDOW AND SDL3_FOUND)
    target_sources(mamerecomp_sega_presentation PRIVATE sega_presenter.cpp)
    target_include_directories(mamerecomp_sega_presentation PRIVATE
        ${PROJECT_SOURCE_DIR}/core/third_party/imgui
        ${PROJECT_SOURCE_DIR}/core/third_party/imgui/backends)
    target_link_libraries(mamerecomp_sega_presentation PRIVATE SDL3::SDL3)
    target_compile_definitions(mamerecomp_sega_presentation PRIVATE MAMERECOMP_HAS_SDL3_AUDIO=1)
else()
    target_sources(mamerecomp_sega_presentation PRIVATE sega_presenter_stub.cpp)
endif()
