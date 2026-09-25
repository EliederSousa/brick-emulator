#include "brickemu/Config.hpp"
#include "brickemu/EmulatorRuntime.hpp"
#ifdef HAVE_SDL2
#include "brickemu/Frontend_SDL.hpp"
#endif
#ifdef HAVE_TERM
#include "brickemu/Frontend_Term.hpp"
#endif

#include <filesystem>
#include <iostream>
#include <charconv>
#include <string_view>

namespace {

void printUsage(std::string_view program) {
    std::cerr << "Usage: " << program << " --brick <path-to-game-config> [--steps N] [--trace] [--run] [--term] [--shot N] [--script a,b,..]\n"
              << "  --run: SDL2 real-time frontend (only in brickemu-sdl builds)\n"
              << "  --term: terminal frontend, interactive (only in brickemu-term builds)\n"
              << "  --pf: playfield mode: 10x17 matrix as single 'x'/' ' (with --term/--shot/--script)\n"
              << "  --no-border: skip the ╔═╗║╚═╝ outline (machine-readable dumps)\n"
              << "  --hold-ms N: key release delay after last repeat (default 200)\n"
              << "  --shot N: dump text frame after ~N frames and exit (with --script: run it first)\n"
              << "  --script: comma list on,start,left,right,down,rotate,mute,reset\n"
              << "  --cell N: square NxN LCD pixels per text cell (exact mode;\n"
              << "            default geometry is 16x8 to match terminal aspect)\n"
              << "  --cell-w N / --cell-h N: cell width/height override\n"
              << "  --ghost F: inactive segment transparency 0..1 (default 0.08, also BRICKEMU_GHOST)\n"
              << "  --png-crop X,Y,W,H: override auto PNG trim (e.g. 8,0,1063,1534)\n"
              << "  --png-offset X,Y: canvas offset in px (fix drift)\n"
              << "  --png-scale S: uniform scale override (fix proportion)\n"
              << "  --png-scale-x S / --png-scale-y S: per-axis scale\n"
              << "  --png-debug: red border around PNG crop\n";
}

} // namespace

int main(int argc, char** argv) {
    std::filesystem::path brickPath = "../assets/Apollo18in1B0302.ini";
    std::uint32_t steps = 0;
    bool trace = false;
    bool run = false;
    bool term = false;
    bool shot = false;
    int shotFrame = -1;
    int cellW = 16;
    int cellH = 8;
    bool playfield = false;
    int holdMs = 200;
    bool noBorder = false;
    std::string script;
#ifdef HAVE_SDL2
    brickemu::SdlPngOptions pngOpt;
#endif

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if ((arg == "--brick" || arg == "-brick") && i + 1 < argc) {
            brickPath = argv[++i];
        } else if (arg == "--steps" && i + 1 < argc) {
            const std::string_view value(argv[++i]);
            const auto* begin = value.data();
            const auto* end = begin + value.size();
            if (std::from_chars(begin, end, steps).ec != std::errc{}) {
                std::cerr << "Invalid --steps value\n";
                return 2;
            }
        } else if (arg == "--trace") {
            trace = true;
        } else if (arg == "--run") {
            run = true;
        } else if (arg == "--term") {
            term = true;
        } else if (arg == "--shot" && i + 1 < argc) {
            const std::string_view value(argv[++i]);
            const auto* begin = value.data();
            const auto* end = begin + value.size();
            if (std::from_chars(begin, end, shotFrame).ec != std::errc{}) {
                std::cerr << "Invalid --shot value\n";
                return 2;
            }
            shot = true;
        } else if (arg == "--script" && i + 1 < argc) {
            script = argv[++i];
        } else if (arg == "--pf") {
            playfield = true;
        } else if (arg == "--no-border") {
            noBorder = true;
        } else if (arg == "--hold-ms" && i + 1 < argc) {
            const std::string_view value(argv[++i]);
            const auto* begin = value.data();
            const auto* end = begin + value.size();
            if (std::from_chars(begin, end, holdMs).ec != std::errc{} ||
                holdMs <= 0) {
                std::cerr << "Invalid --hold-ms value\n";
                return 2;
            }
        } else if (arg == "--cell" && i + 1 < argc) {
            int cell = 0;
            const std::string_view value(argv[++i]);
            const auto* begin = value.data();
            const auto* end = begin + value.size();
            if (std::from_chars(begin, end, cell).ec != std::errc{} ||
                cell <= 0) {
                std::cerr << "Invalid --cell value\n";
                return 2;
            }
            cellW = cellH = cell; // square LCD blocks (exact mode)
        } else if (arg == "--cell-w" && i + 1 < argc) {
            const std::string_view value(argv[++i]);
            const auto* begin = value.data();
            const auto* end = begin + value.size();
            if (std::from_chars(begin, end, cellW).ec != std::errc{} ||
                cellW <= 0) {
                std::cerr << "Invalid --cell-w value\n";
                return 2;
            }
        } else if (arg == "--cell-h" && i + 1 < argc) {
            const std::string_view value(argv[++i]);
            const auto* begin = value.data();
            const auto* end = begin + value.size();
            if (std::from_chars(begin, end, cellH).ec != std::errc{} ||
                cellH <= 0) {
                std::cerr << "Invalid --cell-h value\n";
                return 2;
            }
#ifdef HAVE_SDL2
        } else if (arg == "--ghost" && i + 1 < argc) {
            float v = 0; std::string s(argv[++i]);
            try { v = std::stof(s); } catch(...) { std::cerr << "Invalid --ghost value\n"; return 2; }
            if (v < 0 || v > 1) { std::cerr << "Invalid --ghost value\n"; return 2; }
            pngOpt.ghost = v;
        } else if (arg == "--png-crop" && i + 1 < argc) {
            std::string s(argv[++i]); for(char &c:s) if(c==',') c=' ';
            int x,y,w,h; if (std::sscanf(s.c_str(), "%d %d %d %d", &x,&y,&w,&h)!=4) { std::cerr << "Invalid --png-crop X,Y,W,H\n"; return 2; }
            pngOpt.cropX=x; pngOpt.cropY=y; pngOpt.cropW=w; pngOpt.cropH=h;
        } else if (arg == "--png-offset" && i + 1 < argc) {
            std::string s(argv[++i]); for(char &c:s) if(c==',') c=' ';
            float x,y; if (std::sscanf(s.c_str(), "%f %f", &x,&y)!=2) { std::cerr << "Invalid --png-offset X,Y\n"; return 2; }
            pngOpt.offsetX=x; pngOpt.offsetY=y;
        } else if ((arg == "--png-scale" || arg == "--png-scale-x") && i + 1 < argc) {
            float v = std::stof(std::string(argv[++i])); pngOpt.scaleX = v; if (arg=="--png-scale") pngOpt.scaleY=v;
        } else if (arg == "--png-scale-y" && i + 1 < argc) {
            pngOpt.scaleY = std::stof(std::string(argv[++i]));
        } else if (arg == "--png-debug") {
            pngOpt.debug = true;
#endif
        } else if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown argument: " << arg << '\n';
            printUsage(argv[0]);
            return 2;
        }
    }

    brickemu::ConfigError error;
    auto config = brickemu::ConfigLoader::load(brickPath, &error);
    if (!config) {
        std::cerr << error.message << '\n';
        return 1;
    }

#ifdef HAVE_SDL2
    if (run) {
        brickemu::setSdlPngOptions(pngOpt);
        return brickemu::runSdl(*config);
    }
#else
    if (run) {
        std::cerr << "--run needs an SDL2 build (brickemu-sdl)\n";
        return 2;
    }
#endif
#ifdef HAVE_TERM
    if (term || shot || !script.empty() || playfield) {
        brickemu::TermOptions options;
        options.interactive = (term || playfield) && !shot && script.empty();
        options.cellW = cellW;
        options.cellH = cellH;
        options.playfield = playfield;
        options.holdMs = holdMs;
        options.noBorder = noBorder;
        if (!term && !shot && !script.empty()) {
            options.interactive = false; // --script alone: run it, dump, exit
        }
        options.shotFrame = shotFrame;
        options.script = script;
        return brickemu::runTerm(*config, options);
    }
#else
    if (term || shot || !script.empty() || playfield) {
        std::cerr << "--term/--shot/--script/--pf need a terminal build (brickemu-term)\n";
        return 2;
    }
#endif

    brickemu::EmulatorRuntime runtime(std::move(*config));
    if (!runtime.initialize()) {
        return 3;
    }
    runtime.runSteps(steps, trace);
    if (steps > 0) {
        const auto& cpu = runtime.config();
        std::cout << "C++ step " << steps << ": done\n";
    }
    return 0;
}
