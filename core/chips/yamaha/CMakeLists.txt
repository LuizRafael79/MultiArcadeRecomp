# Shared Yamaha FM cores. Board sound maps and mixing stay in each board
# adapter; these targets contain one implementation per chip family.
set(YMFM_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/ymfm)

add_library(ymfm_opm STATIC ${YMFM_ROOT}/ymfm_opm.cpp)
target_include_directories(ymfm_opm SYSTEM PUBLIC ${YMFM_ROOT})
set_target_properties(ymfm_opm PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(ymfm_opl STATIC
    ${YMFM_ROOT}/ymfm_opl.cpp
    ${YMFM_ROOT}/ymfm_pcm.cpp
    ${YMFM_ROOT}/ymfm_adpcm.cpp)
target_include_directories(ymfm_opl SYSTEM PUBLIC ${YMFM_ROOT})
set_target_properties(ymfm_opl PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(ymfm_opn STATIC
    ${YMFM_ROOT}/ymfm_opn.cpp
    ${YMFM_ROOT}/ymfm_adpcm.cpp
    ${YMFM_ROOT}/ymfm_ssg.cpp
    ${YMFM_ROOT}/ymfm_misc.cpp)
target_include_directories(ymfm_opn SYSTEM PUBLIC ${YMFM_ROOT})
set_target_properties(ymfm_opn PROPERTIES POSITION_INDEPENDENT_CODE ON)
