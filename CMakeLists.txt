cmake_minimum_required(VERSION 3.16)

project(BrickEmuPortMaster LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(brickemu
    src/Config.cpp
    src/CoreRegistry.cpp
    src/EmulatorRuntime.cpp
    src/Rom.cpp
    src/SPL03.cpp
    src/main.cpp
)

target_include_directories(brickemu PRIVATE include)

# Terminal frontend: zero dependencies (termios + ANSI), always built.
# `brickemu-term --brick <game>.ini --term` for interactive play,
# `--shot N` / `--script ...` for headless frame dumps.
add_executable(brickemu-term
    src/Config.cpp
    src/CoreRegistry.cpp
    src/EmulatorRuntime.cpp
    src/Rom.cpp
    src/SPL03.cpp
    src/Frontend_Term.cpp
    src/main.cpp
)

target_include_directories(brickemu-term PRIVATE include)
target_compile_definitions(brickemu-term PRIVATE HAVE_TERM=1)

# SDL2 frontend (display + input + timing for handhelds). Optional so the
# trace/CLI core keeps building anywhere; on PortMaster firmwares the
# system SDL2 (KMSDRM-patched) is used, never a bundled copy.
find_package(SDL2 QUIET)
if (SDL2_FOUND)
    add_executable(brickemu-sdl
        src/Config.cpp
        src/CoreRegistry.cpp
        src/EmulatorRuntime.cpp
        src/Rom.cpp
        src/SPL03.cpp
        src/Frontend_SDL.cpp
        src/main.cpp
    )
    target_include_directories(brickemu-sdl PRIVATE include ${SDL2_INCLUDE_DIRS})
    target_link_libraries(brickemu-sdl PRIVATE ${SDL2_LIBRARIES})
    target_compile_definitions(brickemu-sdl PRIVATE HAVE_SDL2=1)
    message(STATUS "SDL2 frontend enabled (brickemu-sdl)")
else()
    message(STATUS "SDL2 not found; building CLI core only (brickemu)")
endif()

if (MSVC)
    target_compile_options(brickemu PRIVATE /W4)
else()
    target_compile_options(brickemu PRIVATE -Wall -Wextra -Wpedantic)
endif()
