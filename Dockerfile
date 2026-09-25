FROM ubuntu:20.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    cmake \
    build-essential \
    gcc-aarch64-linux-gnu \
    g++-aarch64-linux-gnu \
    libsdl2-dev \
    ca-certificates \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /work
CMD ["bash"]
