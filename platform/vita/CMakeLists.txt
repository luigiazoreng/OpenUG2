cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake"
        CACHE FILEPATH "VitaSDK toolchain")
  else()
    message(FATAL_ERROR "Set VITASDK to your SDK installation")
  endif()
endif()

project(openug2_vita_bringup C CXX)
include("${VITASDK}/share/vita.cmake" REQUIRED)
find_package(SDL2 CONFIG REQUIRED)

if(NOT TARGET SDL2::SDL2-static)
  message(FATAL_ERROR "Install VitaSDK sdl2_vitagl (static SDL2 with vitaGL backend)")
endif()

# Reject a plain GXM/Piglet/PVR SDL installation at configuration, not at boot.
include(CheckCSourceCompiles)
get_target_property(CMAKE_REQUIRED_INCLUDES SDL2::SDL2-static INTERFACE_INCLUDE_DIRECTORIES)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
unset(OPENUG2_SDL_HAS_VITAGL CACHE) # Recheck if the SDK package changed in place.
check_c_source_compiles("
  #include <SDL_config.h>
  #if !defined(SDL_VIDEO_VITA_VGL) || !SDL_VIDEO_VITA_VGL
  #error SDL2 must be built with VIDEO_VITA_VGL
  #endif
  int main(void) { return 0; }
" OPENUG2_SDL_HAS_VITAGL)
unset(CMAKE_REQUIRED_INCLUDES)
if(NOT OPENUG2_SDL_HAS_VITAGL)
  message(FATAL_ERROR "SDL2 lacks vitaGL. Install sdl2_vitagl or build SDL with VIDEO_VITA_VGL=ON")
endif()

option(OPENUG2_VITA_PROFILE "Log bring-up diagnostics once per second" ON)
set(OPENUG2_VITA_TITLEID "OPENUG200" CACHE STRING "Nine-character development title ID")
string(LENGTH "${OPENUG2_VITA_TITLEID}" TITLEID_LENGTH)
if(NOT TITLEID_LENGTH EQUAL 9 OR NOT OPENUG2_VITA_TITLEID MATCHES "^[A-Z0-9]+$")
  message(FATAL_ERROR "OPENUG2_VITA_TITLEID must contain nine uppercase letters/digits")
endif()

add_executable(openug2_vita main.c platform.c renderer_vita.c asset_probe.c asset_job.c)
set_target_properties(openug2_vita PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED YES
  LINKER_LANGUAGE CXX) # vitaGL's GLSL translator is C++; engine sources stay C.
target_compile_options(openug2_vita PRIVATE -Wall -Wextra -Wno-unused-function)
target_compile_definitions(openug2_vita PRIVATE OPENUG2_VITA
  OPENUG2_VITA_PROFILE=$<BOOL:${OPENUG2_VITA_PROFILE}>)
target_include_directories(openug2_vita SYSTEM PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
# SDL's exported target carries its GXM/audio/input/system stubs; these two
# dependencies complete the runtime shader compiler's static-library chain.
target_link_libraries(openug2_vita PRIVATE "-Wl,--start-group" SDL2::SDL2-static
  vitaGL vitashark SceShaccCgExt taihen_stub z m "-Wl,--end-group")

vita_create_self(openug2-vita.self openug2_vita UNSAFE)
vita_create_vpk(openug2-vita.vpk "${OPENUG2_VITA_TITLEID}" openug2-vita.self
  VERSION "00.01" NAME "OpenUG2 Vita Bring-up")
