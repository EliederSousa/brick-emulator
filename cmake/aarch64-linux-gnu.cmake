# Cross toolchain for PortMaster handhelds (aarch64, e.g. RG35XX H).
#
# PortMaster guidance: build on Ubuntu 20.04 so the binary's glibc baseline
# stays compatible with old firmwares (GLIBC <= 2.29 era).
#
#   sudo apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu
#   # SDL2 dev headers for the BUILD machine (firmware provides runtime SDL2):
#   sudo apt install libsdl2-dev
#   cmake -S portmaster-build -B portmaster-build/build-arm \
#     --toolchain portmaster-build/cmake/aarch64-linux-gnu.cmake
#   cmake --build portmaster-build/build-arm
#
# The firmware's own KMSDRM-patched SDL2 is used at runtime; never bundle a
# foreign libSDL2 into the port.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

# Keep the emulator tiny: optimize for size, strip symbols in release.
add_compile_options(-Os -ffunction-sections -fdata-sections)
add_link_options(-Wl,--gc-sections -s)

set(CMAKE_FIND_ROOT_PATH /usr/aarch64-linux-gnu)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
