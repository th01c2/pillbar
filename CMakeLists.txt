cmake_minimum_required(VERSION 3.20)
project(pillbar LANGUAGES C CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(PkgConfig REQUIRED)

pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
pkg_check_modules(CAIRO REQUIRED IMPORTED_TARGET cairo)
pkg_check_modules(PANGO REQUIRED IMPORTED_TARGET pangocairo)
pkg_check_modules(SYSTEMD REQUIRED IMPORTED_TARGET libsystemd)
pkg_check_modules(UDEV REQUIRED IMPORTED_TARGET libudev)
pkg_check_modules(PULSE REQUIRED IMPORTED_TARGET libpulse)
pkg_check_modules(FONTCONFIG REQUIRED IMPORTED_TARGET fontconfig)

find_program(WAYLAND_SCANNER NAMES wayland-scanner REQUIRED)
find_program(PILLBAR_SED NAMES sed REQUIRED)

set(PILLBAR_PROTO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/protocols")
set(PILLBAR_PROTO_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/protocols")
file(MAKE_DIRECTORY "${PILLBAR_PROTO_GEN_DIR}")

# Generates <name>.h and <name>-protocol.c from a protocol XML using wayland-scanner.
function(pillbar_generate_protocol xml_basename out_name)
  set(xml "${PILLBAR_PROTO_DIR}/${xml_basename}")
  set(hdr "${PILLBAR_PROTO_GEN_DIR}/${out_name}.h")
  set(src "${PILLBAR_PROTO_GEN_DIR}/${out_name}-protocol.c")
  add_custom_command(
    OUTPUT "${hdr}" "${src}"
    COMMAND "${WAYLAND_SCANNER}" client-header "${xml}" "${hdr}"
    COMMAND "${WAYLAND_SCANNER}" private-code "${xml}" "${src}"
    # wlr-layer-shell uses a parameter literally named "namespace", which is a
    # C++ keyword; rename it in the generated header so C++ TUs can include it.
    COMMAND "${PILLBAR_SED}" -i "s/\\bnamespace\\b/namespace_/g" "${hdr}"
    DEPENDS "${xml}"
    COMMENT "wayland-scanner: ${xml_basename}"
    VERBATIM)
  set(PILLBAR_PROTO_SOURCES "${PILLBAR_PROTO_SOURCES};${src}" PARENT_SCOPE)
  set(PILLBAR_PROTO_HEADERS "${PILLBAR_PROTO_HEADERS};${hdr}" PARENT_SCOPE)
endfunction()

pillbar_generate_protocol("wlr-layer-shell-unstable-v1.xml" "wlr-layer-shell-unstable-v1-client-protocol")
pillbar_generate_protocol("xdg-output-unstable-v1.xml" "xdg-output-unstable-v1-client-protocol")
pillbar_generate_protocol("fractional-scale-v1.xml" "fractional-scale-v1-client-protocol")
pillbar_generate_protocol("viewporter.xml" "viewporter-client-protocol")
pillbar_generate_protocol("xdg-shell.xml" "xdg-shell-client-protocol")

add_library(pillbar_protocols STATIC ${PILLBAR_PROTO_SOURCES})
target_include_directories(pillbar_protocols PUBLIC "${PILLBAR_PROTO_GEN_DIR}")
target_link_libraries(pillbar_protocols PUBLIC PkgConfig::WAYLAND_CLIENT)
target_compile_options(pillbar_protocols PRIVATE -w)

set(PILLBAR_SOURCES
  src/main.cpp
  src/app/app.cpp
  src/app/logging.cpp
  src/app/config.cpp
  src/app/event_loop.cpp
  src/model/state.cpp
  src/wayland/display.cpp
  src/wayland/shm.cpp
  src/wayland/output.cpp
  src/wayland/layer_surface.cpp
  src/wayland/pointer.cpp
  src/render/glyphs.cpp
  src/render/text.cpp
  src/render/layout.cpp
  src/render/renderer.cpp
  src/sources/json.cpp
  src/sources/sysfs.cpp
  src/sources/dbus.cpp
  src/sources/pulse.cpp
  src/sources/hyprland.cpp
  src/sources/power.cpp
  src/sources/audio.cpp
  src/sources/wifi.cpp
  src/sources/bluetooth.cpp
  src/sources/clock.cpp
  src/sources/stats.cpp
  src/sources/gpu.cpp
  src/sources/login1.cpp
  src/sources/manager.cpp
  src/sources/recorder.cpp
)

add_executable(pillbar ${PILLBAR_SOURCES})

target_include_directories(pillbar PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_compile_options(pillbar PRIVATE -Wall -Wextra -Wpedantic)

target_link_libraries(pillbar PRIVATE
  pillbar_protocols
  PkgConfig::WAYLAND_CLIENT
  PkgConfig::CAIRO
  PkgConfig::PANGO
  PkgConfig::SYSTEMD
  PkgConfig::UDEV
  PkgConfig::PULSE
  PkgConfig::FONTCONFIG
  ${CMAKE_DL_LIBS}
  m
)

install(TARGETS pillbar RUNTIME DESTINATION bin)
