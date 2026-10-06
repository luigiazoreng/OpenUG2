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
option(OPENUG2_VITA_GPU_PROBES "Run production shader and synthetic GPU probes" OFF)
option(OPENUG2_VITA_CAR_VIEWER "Run the complete parked stock 240SX viewer" OFF)
if(OPENUG2_VITA_CAR_VIEWER)
  set(OPENUG2_VITA_GPU_PROBES ON)
endif()
set(OPENUG2_VITAGL_LIBRARY "" CACHE FILEPATH "Optional pinned vitaGL archive built with SAFE_UNIFORMS=1")
set(OPENUG2_VITA_TITLEID "OPENUG200" CACHE STRING "Nine-character development title ID")
string(LENGTH "${OPENUG2_VITA_TITLEID}" TITLEID_LENGTH)
if(NOT TITLEID_LENGTH EQUAL 9 OR NOT OPENUG2_VITA_TITLEID MATCHES "^[A-Z0-9]+$")
  message(FATAL_ERROR "OPENUG2_VITA_TITLEID must contain nine uppercase letters/digits")
endif()

if(OPENUG2_VITA_CAR_VIEWER)
  add_executable(openug2_vita viewer_main.c startup_guard.c platform.c renderer_vita.c car_scene.c car_viewer.c ../../src/resource.c)
  target_compile_definitions(openug2_vita PRIVATE OPENUG2_RESOURCE_BUFFERED OPENUG2_VITA_CAR_SHADER=1)
else()
  add_executable(openug2_vita main.c platform.c renderer_vita.c asset_probe.c asset_job.c)
endif()
set_target_properties(openug2_vita PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED YES
  LINKER_LANGUAGE CXX) # vitaGL's GLSL translator is C++; engine sources stay C.
target_compile_options(openug2_vita PRIVATE -Wall -Wextra -Werror -Wno-unused-function)
target_compile_definitions(openug2_vita PRIVATE OPENUG2_VITA
  OPENUG2_VITA_PROFILE=$<BOOL:${OPENUG2_VITA_PROFILE}>
  OPENUG2_VITA_GPU_PROBES=$<BOOL:${OPENUG2_VITA_GPU_PROBES}>)
if(OPENUG2_VITA_GPU_PROBES)
  if(NOT OPENUG2_VITAGL_LIBRARY)
    message(FATAL_ERROR "GPU tests require OPENUG2_VITAGL_LIBRARY built with SAFE_UNIFORMS=1; see docs/VITA_GPU_PROBES.md")
  endif()
  target_sources(openug2_vita PRIVATE gpu_probe.c ../../src/render.c ../../src/render_uniform.c)
  target_compile_definitions(openug2_vita PRIVATE N2_GLES OPENUG2_GL_HEADER="vitaGL.h")
endif()
if(OPENUG2_VITA_CAR_VIEWER)
  execute_process(COMMAND git rev-parse HEAD WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE OPENUG2_BUILD_REVISION OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE REVISION_STATUS)
  if(NOT REVISION_STATUS EQUAL 0 OR NOT EXISTS "${OPENUG2_VITAGL_LIBRARY}")
    message(FATAL_ERROR "Car viewer requires a Git checkout and an existing pinned vitaGL archive")
  endif()
  # Hash the actual C translation units and local headers, the build recipe and
  # dependency archive. Reconfigure when any of those inputs changes.
  get_target_property(OPENUG2_BUILD_SOURCES openug2_vita SOURCES)
  file(GLOB OPENUG2_BUILD_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/*.h")
  set(OPENUG2_BUILD_INPUTS ${OPENUG2_BUILD_SOURCES} ${OPENUG2_BUILD_HEADERS}
    "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt" "${CMAKE_CURRENT_SOURCE_DIR}/../../Makefile")
  list(SORT OPENUG2_BUILD_INPUTS)
  set(OPENUG2_BUILD_FINGERPRINT "${CMAKE_C_FLAGS}|${CMAKE_C_FLAGS_RELEASE}|${OPENUG2_VITA_PROFILE}|${OPENUG2_VITA_TITLEID}\n")
  foreach(INPUT IN LISTS OPENUG2_BUILD_INPUTS)
    get_filename_component(INPUT "${INPUT}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    file(SHA256 "${INPUT}" INPUT_SHA256)
    file(RELATIVE_PATH INPUT_NAME "${CMAKE_CURRENT_SOURCE_DIR}/../.." "${INPUT}")
    string(APPEND OPENUG2_BUILD_FINGERPRINT "${INPUT_NAME}:${INPUT_SHA256}\n")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${INPUT}")
  endforeach()
  file(SHA256 "${OPENUG2_VITAGL_LIBRARY}" OPENUG2_VITAGL_SHA256)
  string(APPEND OPENUG2_BUILD_FINGERPRINT "vitaGL:${OPENUG2_VITAGL_SHA256}\n")
  string(SHA256 OPENUG2_BUILD_SHA256 "${OPENUG2_BUILD_FINGERPRINT}")
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/source-fingerprint.txt" "${OPENUG2_BUILD_FINGERPRINT}")
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/build-identity.txt" "revision=${OPENUG2_BUILD_REVISION}\nsource_sha256=${OPENUG2_BUILD_SHA256}\nvitaGL_sha256=${OPENUG2_VITAGL_SHA256}\n")
  target_compile_definitions(openug2_vita PRIVATE OPENUG2_BUILD_REVISION="${OPENUG2_BUILD_REVISION}"
    OPENUG2_BUILD_SHA256="${OPENUG2_BUILD_SHA256}")
endif()
if(OPENUG2_VITAGL_LIBRARY)
  if(NOT EXISTS "${OPENUG2_VITAGL_LIBRARY}")
    message(FATAL_ERROR "OPENUG2_VITAGL_LIBRARY archive does not exist")
  endif()
  get_filename_component(OPENUG2_VITAGL_DIRECTORY "${OPENUG2_VITAGL_LIBRARY}" DIRECTORY)
  get_filename_component(OPENUG2_VITAGL_FILENAME "${OPENUG2_VITAGL_LIBRARY}" NAME)
  if(NOT OPENUG2_VITAGL_FILENAME STREQUAL "libvitaGL.a")
    message(FATAL_ERROR "OPENUG2_VITAGL_LIBRARY must name libvitaGL.a so SDL resolves the same archive")
  endif()
  # SDL's imported target also links -lvitaGL. Resolve both references through
  # the same archive, without changing the installed SDK package.
  target_link_directories(openug2_vita BEFORE PRIVATE "${OPENUG2_VITAGL_DIRECTORY}")
endif()
target_include_directories(openug2_vita SYSTEM PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
# SDL's exported target carries its GXM/audio/input/system stubs; these two
# dependencies complete the runtime shader compiler's static-library chain.
target_link_libraries(openug2_vita PRIVATE "-Wl,--start-group" SDL2::SDL2-static
  vitaGL vitashark SceShaccCgExt taihen_stub z m "-Wl,--end-group")

vita_create_self(openug2-vita.self openug2_vita UNSAFE)
if(OPENUG2_VITA_CAR_VIEWER)
  set(OPENUG2_VITA_NAME "OpenUG2 240SX Test")
else()
  set(OPENUG2_VITA_NAME "OpenUG2 Vita Bring-up")
endif()
vita_create_vpk(openug2-vita.vpk "${OPENUG2_VITA_TITLEID}" openug2-vita.self
  VERSION "00.01" NAME "${OPENUG2_VITA_NAME}")
