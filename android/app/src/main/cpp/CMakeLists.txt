cmake_minimum_required(VERSION 3.22)
project(ArmyMenRTSAndroid C CXX)
add_library(armymen_rts_android SHARED bootstrap.cpp)
target_compile_features(armymen_rts_android PRIVATE cxx_std_17)
target_link_libraries(armymen_rts_android PRIVATE android log vulkan)

# Build the lifted ARM64 translation units independently while the DDraw HLE
# and Windows service layer are being adapted. They are deliberately not linked
# into the probe APK until the guest can enter and render safely.
option(AMRTS_COMPILE_GUEST "Syntax-check all lifted Army Men RTS code" OFF)
if(AMRTS_COMPILE_GUEST)
    get_filename_component(AMRTS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)
    set(GEN_DIR "${AMRTS_ROOT}/src/recomp/gen")
    file(GLOB GEN_SOURCES CONFIGURE_DEPENDS "${GEN_DIR}/recomp_[0-9][0-9][0-9][0-9].c")
    if(NOT GEN_SOURCES)
        message(FATAL_ERROR "Generate the local guest C first")
    endif()
    add_library(amrts_guest OBJECT ${GEN_SOURCES})
    target_include_directories(amrts_guest PRIVATE "${AMRTS_ROOT}/../pcrecomp/runtime/recomp32" "${GEN_DIR}")
    target_compile_options(amrts_guest PRIVATE -O0 -w -fwrapv -fno-strict-aliasing -include "${CMAKE_CURRENT_SOURCE_DIR}/guest_clock.h")
endif()
