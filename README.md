# brick-emulator (C++ PortMaster port)

C++20 port of [BrickEmuPy](https://github.com/azya52/BrickEmuPy) — an emulator
for LCD handheld games (Brick Game, Tamagotchi, etc.) — targeting PortMaster
handhelds (e.g. Anbernic RG35XX H / RG40XXH, `aarch64`, 640x480).

## Game config (INI)

Games are described by a small INI file (`assets/*.ini`), trimmed down from
the Python `.brick` JSON — only what the C++ port reads:

```ini
[game]
core = SPL03
clock = 690000
rom = ./Apollo18in1B0302.bin
non_crystal_div = 16

[port_pullup]
PA = 0

[port_pkey]
PA = 1
```

`[game] core/clock/rom` are required; `non_crystal_div` and the port tables
are optional. `rom` resolves relative to the working directory first, then to
the config file's folder. Dropped Python leftovers: `buttons`/`hot_keys`
(hardcoded in `src/Frontend_SDL.cpp`), `peripherals`, SVG `face_path` (the
SDL skin is `Apollo18in1display.png`), display tuning, sound ROM,
`disasm_roots`.

## How to Build

Prerequisites (build machine): `cmake`, C++20 compiler, `libsdl2-dev`
(SDL2 is build-time only; on the handheld the firmware SDL2 is used).

```bash
# native build (this machine)
cmake -S . -B build
cmake --build build -j$(nproc)
ls build/brickemu build/brickemu-term build/brickemu-sdl
```

Cross build for the handheld (do this on Ubuntu 20.04 so glibc stays
compatible with old firmwares, min_glibc 2.29):

```bash
sudo apt install -y gcc-aarch64-linux-gnu g++-aarch64-linux-gnu libsdl2-dev
cmake -S . -B build-arm \
  --toolchain cmake/aarch64-linux-gnu.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-arm -j$(nproc)
file build-arm/brickemu-sdl   # must say aarch64
```

Same via Docker on any other machine:

```bash
docker run --rm -it -v "$PWD":/work -w /work ubuntu:20.04 bash -c "
export DEBIAN_FRONTEND=noninteractive &&
apt update && apt install -y cmake build-essential \
  gcc-aarch64-linux-gnu g++-aarch64-linux-gnu libsdl2-dev &&
cmake -S . -B build-arm \
  --toolchain cmake/aarch64-linux-gnu.cmake -DCMAKE_BUILD_TYPE=Release &&
cmake --build build-arm -j\$(nproc)"
```

## \how to run

```bash
# SDL window (needs a display + firmware/system SDL2)
./build/brickemu-sdl --brick port/brickemu/brickemu/Apollo18in1B0302.ini --run

# console / terminal frontend
./build/brickemu-term --brick port/brickemu/brickemu/Apollo18in1B0302.ini --term

# headless CLI: run N CPU steps, optionally traced
./build/brickemu --brick port/brickemu/brickemu/Apollo18in1B0302.ini --steps 1000
./build/brickemu --brick port/brickemu/brickemu/Apollo18in1B0302.ini --steps 200 --trace

# headless frame dump (terminal renderer)
./build/brickemu-term --brick port/brickemu/brickemu/Apollo18in1B0302.ini \
  --script on,start --shot 600
```

Headless SDL frame dump (no window, needs `SDL_VIDEODRIVER=dummy`):

```bash
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
BRICKEMU_DUMPFRAME=/tmp/shot.bmp BRICKEMU_DUMPFRAME_AT=600 \
./build/brickemu-sdl --brick port/brickemu/brickemu/Apollo18in1B0302.ini --run
```

## Flags

The flag `--run` needs a `brickemu-sdl` build and
`--term/--shot/--script/--pf` need a `brickemu-term` build.

| Flag | Argument | Default | Binary | Description |
|---|---|---|---|---|
| `--brick`, `-brick` | `<path>` | `../assets/Apollo18in1B0302.ini` | all | game config file (INI) to load |
| `--steps` | `N` | `0` | `brickemu` | run N CPU steps and exit |
| `--trace` | — | off | `brickemu` | trace each step (`pc/op/A/X/SP/flags`) with `--steps` |
| `--run` | — | off | `brickemu-sdl` | SDL2 realtime frontend (the game window) |
| `--term` | — | off | `brickemu-term` | terminal frontend, interactive play |
| `--shot` | `N` | off | `brickemu-term` | dump text frame after ~N frames and exit (runs `--script` first) |
| `--script` | `a,b,..` | empty | `brickemu-term` | buttons: `on,start,left,right,down,rotate,mute,reset` |
| `--pf` | — | off | `brickemu-term` | playfield mode: 10x17 matrix as `x`/space |
| `--no-border` | — | off | `brickemu-term` | skip the box outline (machine-readable dumps) |
| `--hold-ms` | `N` | `200` | `brickemu-term` | key release delay after last repeat (ms, > 0) |
| `--cell` | `N` | — | `brickemu-term` | square NxN LCD pixels per text cell (exact mode) |
| `--cell-w` | `N` | `16` | `brickemu-term` | cell width override (> 0) |
| `--cell-h` | `N` | `8` | `brickemu-term` | cell height override (> 0) |
| `--ghost` | `F` | `0.08` | `brickemu-sdl` | inactive-segment transparency, 0..1 (also `BRICKEMU_GHOST`) |
| `--png-crop` | `X,Y,W,H` | auto trim | `brickemu-sdl` | override PNG skin auto-trim (e.g. `8,0,1063,1534`) |
| `--png-offset` | `X,Y` | `0,0` | `brickemu-sdl` | canvas offset in px (fix drift) |
| `--png-scale` | `S` | auto | `brickemu-sdl` | uniform scale override (sets both axes) |
| `--png-scale-x` | `S` | auto | `brickemu-sdl` | per-axis scale X |
| `--png-scale-y` | `S` | auto | `brickemu-sdl` | per-axis scale Y |
| `--png-debug` | — | off | `brickemu-sdl` | red border around PNG crop |
| `--help`, `-h` | — | — | all | print usage and exit |


## Controls (SDL)

| Brick button | Keyboard | Gamepad |
|---|---|---|
| Left / Right / Down | `A`/`D`/`S`, arrows | D-pad |
| Rotate | `W` / `Space` / `Up` | A |
| Start/Power | `Enter` | Start |
| On/Off | `O` | Y |
| Mute | `M` | Back/Select |
| Reset | `R` | X |
| Quit | `Esc` | (OS) |



## Status / goals

- [x] SPL03 core runs the Apollo 18-in-1 ROM with trace parity vs Python
      (`docs/PORTING.md`, `testbed/` in the main repo).
- [x] Display-only LCD rasterizer (`include/brickemu/DisplayBuffer.hpp`,
      baked `Apollo18Segments.hpp`): one small upload per frame, only when the
      48-byte LCDRAM changed.
- [x] SDL2 frontend (`src/Frontend_SDL.cpp`): 640x480 window, logical LCD
      scaling, optional PNG skin, keyboard + gamecontroller input, 60 Hz pacing.
- [x] Audio: toneA + noise (CCH type / CEH volume envelope, e.g. explosion SFX)
      + speech via SDL queued audio.
- [x] Terminal frontend (`src/Frontend_Term.cpp`): playable in any ANSI
      terminal, plus `--shot`/`--script` headless dumps.
- [ ] More cores (SPL02, HT943, KS57, …): `src/CoreRegistry.cpp` is SPL03-only.
- [ ] PortMaster release (`port/`): needs the `aarch64` cross build (see below).


## Troubleshooting

- `cmake: comand not found` → `sudo apt install -y cmake build-essential libsdl2-dev`.
- `SdlPngOptions is not a member of brickemu` on old checkouts → update; the PNG options are SDL-only (`#ifdef HAVE_SDL2` in `src/main.cpp`).
- SDL skin not loading → run with `cd` at the game dir; lookup order is `./Apollo18in1display.png`, `assets/…`, `../assets/…`.
- `unsupported core: X` → only `SPL03` is ported (`src/CoreRegistry.cpp`).
- Link/runtime needs system SDL2 only; never bundle `libSDL2.so` into the port.
