project(yabause-desktop)

cmake_minimum_required(VERSION 3.8)

yab_port_start()

# Native desktop (macOS) frontend for the GBAStation core.  Uses SDL2 for the
# window and OpenGL 3.3 (core profile) for both the software renderer output
# and the built-in boot page.  Configure the core itself without an OpenGL
# stack (YAB_WANT_OPENGL=OFF) since the emulator runs its software renderer
# here; see build_macos.sh.
#
# Requirements (macOS):
#   brew install sdl2
#   cmake -DYAB_PORTS=desktop -DYAB_WANT_OPENGL=OFF ...

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig QUIET)
if (PKG_CONFIG_FOUND)
  pkg_check_modules(SDL2 QUIET sdl2)
endif()

if (NOT SDL2_FOUND)
  find_path(SDL2_INCLUDE_DIR SDL2/SDL.h
    HINTS /opt/homebrew/include /usr/local/include $ENV{HOME}/homebrew/include)
  find_library(SDL2_LIBRARY NAMES SDL2
    HINTS /opt/homebrew/lib /usr/local/lib)
  if (NOT SDL2_INCLUDE_DIR OR NOT SDL2_LIBRARY)
    message(FATAL_ERROR "SDL2 not found. Install it with 'brew install sdl2'.")
  endif()
  set(SDL2_INCLUDE_DIRS ${SDL2_INCLUDE_DIR})
  set(SDL2_LIBRARIES ${SDL2_LIBRARY})
endif()

set(PORT_INCLUDE_DIRS
  ${SDL2_INCLUDE_DIRS}
  ${CMAKE_CURRENT_SOURCE_DIR}/..
  ${CMAKE_CURRENT_SOURCE_DIR}/../gbastation
  ${CMAKE_CURRENT_SOURCE_DIR}/../nanovg
  ${CMAKE_CURRENT_BINARY_DIR}/..
)

if (APPLE)
  set(PORT_LIBRARIES ${PORT_LIBRARIES}
    ${SDL2_LIBRARIES}
    "-framework OpenGL" "-framework Cocoa" "-framework IOKit"
    "-framework CoreFoundation")
else()
  set(PORT_LIBRARIES ${PORT_LIBRARIES} ${SDL2_LIBRARIES})
endif()

include_directories(${PORT_INCLUDE_DIRS})

set(yabause_desktop_SOURCES
  main.cpp
  UiGL.cpp
  ../gbastation/GBAStationPlatform.cpp
  ../gbastation/Browser.cpp
  ../gbastation/NvgUi.cpp
  ../gbastation/UiFont.cpp
  ../gbastation/Settings.cpp
)

# Link against the archives produced by the libchdr ExternalProject by name
# rather than file path: the generic-host LIBCHDR_LIBRARIES list references
# archives (flac/crypto) that the bundled libchdr build does not generate.
set(LIBCHDR_BUILD_DIR ${CMAKE_BINARY_DIR}/src/libchdr-prefix/src/libchdr-build)
link_directories(
  ${LIBCHDR_BUILD_DIR}
  ${LIBCHDR_BUILD_DIR}/deps/lzma-24.05
  ${LIBCHDR_BUILD_DIR}/deps/zlib-1.3.1
  ${LIBCHDR_BUILD_DIR}/deps/zstd-1.5.6/build/cmake/lib
)

add_executable(GBAStationYabaSanshiroDesktop ${yabause_desktop_SOURCES})

target_link_libraries(GBAStationYabaSanshiroDesktop
  yabause
  -lchdr-static
  -llzma
  -lz
  -lzstd
  ${YABAUSE_LIBRARIES}
  ${PORT_LIBRARIES}
)

if (TARGET libchdr)
  add_dependencies(GBAStationYabaSanshiroDesktop libchdr)
endif()

yab_port_success(GBAStationYabaSanshiroDesktop)
