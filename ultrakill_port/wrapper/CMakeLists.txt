cmake_minimum_required(VERSION 3.13)
project(ultrakill_shim C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT DEFINED TARGET_ARCH)
  set(TARGET_ARCH "arm64")
endif()

if(TARGET_ARCH STREQUAL "arm64")
  add_compile_options(-march=armv8-a+fp+simd -mtune=cortex-a53)
elseif(TARGET_ARCH STREQUAL "armv7")
  add_compile_options(-march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard)
  add_link_options(-latomic)
endif()

include_directories(
  ${CMAKE_SOURCE_DIR}/include
  ${CMAKE_SOURCE_DIR}/bridge
  ${CMAKE_SOURCE_DIR}/jni
  ${CMAKE_SOURCE_DIR}/gl)

# --------------------------------------------------------------- SDL2
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)
include_directories(${SDL2_INCLUDE_DIRS})

# --------------------------------------------------------------- liblog
add_library(log_stub SHARED stubs/android_log_stub.c)
set_target_properties(log_stub PROPERTIES
  OUTPUT_NAME "log"                # produz liblog.so
  SOVERSION   "1"
  VERSION     "1.0.0")

# --------------------------------------------------------------- libandroid
# Os stubs de AAsset/ALooper/AMemory/AInputQueue vivem nos mesmos .so
# no NDK. Agrupamos.
add_library(android_stub SHARED
  stubs/android_asset_stub.c
  stubs/android_nativeactivity_stub.c
  stubs/android_input_stub.c)
set_target_properties(android_stub PROPERTIES OUTPUT_NAME "android")

# --------------------------------------------------------------- libOpenSLES
add_library(opensles_stub SHARED stubs/opensles_stub.c)
set_target_properties(opensles_stub PROPERTIES OUTPUT_NAME "OpenSLES")

# --------------------------------------------------------------- libmediandk
add_library(mediandk_stub SHARED stubs/mediandk_stub.c)
set_target_properties(mediandk_stub PROPERTIES OUTPUT_NAME "mediandk")

# --------------------------------------------------------------- libEGL.so (shim)
# Este alvo NAO linka com EGL nenhum - ele eh o EGL. O nome de saida
# eh exatamente "libEGL.so" (sem SOVERSION), para que o NEEDED do
# libunity.so caia nele. Ao rodar, dlopen('libEGL.so.1') abre o driver real.
add_library(egl_shim SHARED gl/bridge_egl_shim.c)
set_target_properties(egl_shim PROPERTIES
  OUTPUT_NAME "EGL"
  PREFIX      "lib")
# Sem SOVERSION de proposito: evita criar symlink libEGL.so.1 -> .so
# que colidira com o driver real.

# --------------------------------------------------------------- bridge
add_executable(bridge
  bridge/bridge_main.c
  bridge/bridge_input.c
  jni/bridge_jni.c
  gl/bridge_gl.c
  gl/bridge_shader_stub.c)
target_link_libraries(bridge
  PRIVATE
    ${SDL2_LIBRARIES}
    android_stub
    dl m pthread)
target_link_directories(bridge PRIVATE ${SDL2_LIBRARY_DIRS})
# -rdynamic (export-dynamic): faz shim_get_native_window visivel via
# dlsym(RTLD_DEFAULT,...) a partir do bridge_egl_shim carregado depois.
set_target_properties(bridge PROPERTIES
  OUTPUT_NAME "ultrakill"
  LINK_FLAGS "-Wl,--export-dynamic")

# --------------------------------------------------------------- install layout
install(TARGETS log_stub android_stub opensles_stub mediandk_stub egl_shim
        LIBRARY DESTINATION "${CMAKE_SOURCE_DIR}/../libs/${TARGET_ARCH}")
install(TARGETS bridge RUNTIME
        DESTINATION "${CMAKE_SOURCE_DIR}/../gamedata/bin")

# --------------------------------------------------------------- tests
# input_probe: mini-ELF standalone que exercita AInputQueue + bridge_input
# sem depender de libunity.so. NAO roda em `cmake --install`; use
#   ./build.sh <arch> && ./build-<arch>/input_probe --headless --skip-real-loop
# para CI, ou com `--dur 5` numa devbox com gamepad.
option(ULTRAKILL_BUILD_TESTS "build input_probe test" ON)
if(ULTRAKILL_BUILD_TESTS)
  add_executable(input_probe
    tests/input_probe.c
    stubs/android_input_stub.c
    stubs/android_nativeactivity_stub.c
    bridge/bridge_input.c
    gl/bridge_gl.c)
  target_link_libraries(input_probe
    PRIVATE ${SDL2_LIBRARIES} dl m pthread)
  target_link_directories(input_probe PRIVATE ${SDL2_LIBRARY_DIRS})
  set_target_properties(input_probe PROPERTIES
    # -rdynamic: p/ os checks de dlsym(RTLD_DEFAULT) no proprio teste
    LINK_FLAGS "-Wl,--export-dynamic")

  # symbol_probe: CLI que inspeciona um libunity.so e lista simbolos
  # candidatos para hook. NAO precisa de SDL/EGL - so do stub.
  add_executable(symbol_probe
    tests/symbol_probe.c
    gl/bridge_shader_stub.c)
  target_link_libraries(symbol_probe PRIVATE dl m pthread)
  set_target_properties(symbol_probe PROPERTIES
    LINK_FLAGS "-Wl,--export-dynamic")
endif()
