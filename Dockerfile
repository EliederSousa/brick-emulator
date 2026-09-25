# Cross-build image for the PortMaster port (Ubuntu 20.04 => glibc <= 2.29,
# compatible with old firmwares; runtime SDL2 comes from the firmware).
FROM ubuntu:20.04

ENV DEBIAN_FRONTEND=noninteractive

# - GCC 10 cross: 20.04's default GCC 9 has no <span> (the code is C++20).
#   Symlinks in /usr/local/bin shadow the unversioned GCC 9 names so the
#   existing cmake/aarch64-linux-gnu.cmake toolchain needs no changes.
# - Host libsdl2-dev: only used for native builds; the aarch64 SDL2 below
#   is what the cross build links against.
RUN apt-get update && apt-get install -y --no-install-recommends \
    cmake \
    build-essential \
    gcc-10-aarch64-linux-gnu \
    g++-10-aarch64-linux-gnu \
    libsdl2-dev \
    ca-certificates \
    file \
    wget \
 && rm -rf /var/lib/apt/lists/* \
 && ln -sf /usr/bin/aarch64-linux-gnu-gcc-10 /usr/local/bin/aarch64-linux-gnu-gcc \
 && ln -sf /usr/bin/aarch64-linux-gnu-g++-10 /usr/local/bin/aarch64-linux-gnu-g++ \
 && aarch64-linux-gnu-g++ --version | head -1

# aarch64 SDL2 (link-time only): the cross root (/usr/aarch64-linux-gnu) is
# where cmake/aarch64-linux-gnu.cmake looks, so install there. Backends are
# disabled on purpose -- the firmware's own SDL2 provides them at runtime.
ARG SDL2_VERSION=2.30.0
RUN wget -qO- "https://www.libsdl.org/release/SDL2-${SDL2_VERSION}.tar.gz" \
      | tar -xz -C /tmp \
 && cd "/tmp/SDL2-${SDL2_VERSION}" \
 && ./configure --host=aarch64-linux-gnu --prefix=/usr/aarch64-linux-gnu \
      --disable-video-x11 --disable-video-wayland --disable-video-kmsdrm \
      --disable-alsa --disable-pulseaudio --disable-esd --disable-jack \
 && make -j"$(nproc)" && make install \
 && cd / && rm -rf "/tmp/SDL2-${SDL2_VERSION}" \
 && ls /usr/aarch64-linux-gnu/lib/libSDL2.so

WORKDIR /work
CMD ["bash"]
