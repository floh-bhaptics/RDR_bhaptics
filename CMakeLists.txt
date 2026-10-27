cmake_minimum_required(VERSION 3.20)
project(RDR_bhaptics LANGUAGES CXX)

# RDR_bhaptics.red: a RedHook plugin (https://www.nexusmods.com/reddeadredemption/mods/192) that polls the
# player's state through the game's natives and plays bHaptics events. Independent of RDRVR; works in flat mode too.
#
# Dependencies (git submodules under extern/, see README):
#   extern/RedHookSDK   https://github.com/K3rhos/RedHookSDK   (MIT)  headers + RedHook.lib
#   extern/tact-cpp2    https://github.com/bhaptics/tact-cpp2   bhaptics.h (compile time only) + bhaptics_library.dll (shipped)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")  # static CRT

set(REDHOOK_SDK_DIR "${CMAKE_SOURCE_DIR}/extern/RedHookSDK" CACHE PATH "RedHookSDK checkout")
set(BHAPTICS_SDK_DIR "${CMAKE_SOURCE_DIR}/extern/tact-cpp2/sdk" CACHE PATH "bHaptics tact-cpp2 sdk folder")

if(NOT EXISTS "${REDHOOK_SDK_DIR}/Resources/Includes/RedHook.h")
    message(FATAL_ERROR "RedHook SDK not found at ${REDHOOK_SDK_DIR} (git submodule update --init)")
endif()
if(NOT EXISTS "${BHAPTICS_SDK_DIR}/include/bhaptics/bhaptics.h")
    message(FATAL_ERROR "bHaptics SDK not found at ${BHAPTICS_SDK_DIR} (git submodule update --init)")
endif()

add_library(rdr_bhaptics SHARED
    src/main.cpp
    src/game_state.cpp
    src/haptics.cpp
    src/config.cpp
    src/log.cpp
)
set_target_properties(rdr_bhaptics PROPERTIES OUTPUT_NAME RDR_bhaptics SUFFIX .red)
target_compile_features(rdr_bhaptics PRIVATE cxx_std_20)
target_compile_definitions(rdr_bhaptics PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE
    BHAPTICS_DECLSPEC=)  # we only use the header's declarations for decltype; the DLL is loaded at runtime
# /wd4505: Natives.h defines thousands of unused static functions
target_compile_options(rdr_bhaptics PRIVATE /W4 /permissive- /utf-8 /Zi /wd4505 /wd4100 /wd4244)
target_include_directories(rdr_bhaptics PRIVATE
    src
    "${REDHOOK_SDK_DIR}"                      # Headers/Natives.h etc.
    "${REDHOOK_SDK_DIR}/Resources/Includes"   # RedHook.h
    "${BHAPTICS_SDK_DIR}/include")
target_link_options(rdr_bhaptics PRIVATE /DEBUG /OPT:REF /OPT:ICF)
target_link_libraries(rdr_bhaptics PRIVATE "${REDHOOK_SDK_DIR}/Resources/Libraries/RedHook.lib" shell32)
