// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Luca Casati
// Runs the original plugin for a few blocks (so the oscillator wavetables get built),
// then dumps the shared wavetable sets and the constants used to build them.
//   i686-w64-mingw32-g++ -O2 -static -I tests/helpers tools/dump_wavetables.cpp -o dump_wavetables.exe
//   wine dump_wavetables.exe MeowSynth.dll wavetables.bin
// Output: for each set: u32 tag, u32 bandCount; per band: u32 size, u32 minInc, u32 maxInc,
// u32 f18, then size float32 samples.
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include "RenderScenario.h"

using namespace RenderScenario;
typedef Plugin* (*PluginMain)(HostCallback);

static const uint32_t kImageBase = 0x10000000;
static const uint32_t kSets[] = { 0x1019a3fc, 0x1019a410, 0x1019a424, 0x1019a438 };  // sine, saw (smooth), saw, triangle
static const uint32_t kDoubles[] = { 0x1013e810, 0x1013e838, 0x101424f8, 0x10142a78, 0x10146510, 0x1014e3a8,
                                     0x10152a10, 0x10152a20, 0x10152a28, 0x10152a30, 0x10152a38, 0x10152a40 };
static const uint32_t kFloats[] = { 0x10152a14, 0x10194720, 0x10157c38, 0x10157c40, 0x1013e7f4 };

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: %s <plugin.dll> <out.bin>\n", argv[0]); return 1; }
    HMODULE dll = LoadLibraryA(argv[1]);
    if (!dll) { std::fprintf(stderr, "cannot load\n"); return 1; }
    char* base = (char*)dll;
    std::printf("loaded at %p (preferred %08x)\n", dll, kImageBase);
    auto at = [&](uint32_t va) { return base + (va - kImageBase); };

    PluginMain entry = (PluginMain)GetProcAddress(dll, "main");
    Plugin* p = entry(&hostCallback);
    render(p, kScenarios[0]);  // builds the tables

    for (uint32_t va : kDoubles) std::printf("double %08x = %.17g\n", va, *(double*)at(va));
    for (uint32_t va : kFloats)  std::printf("float  %08x = %.9g (%08x)\n", va, *(float*)at(va), *(uint32_t*)at(va));

    FILE* f = std::fopen(argv[2], "wb");
    for (uint32_t va : kSets) {
        char* set = at(va);
        char* entries = *(char**)(set + 4);
        uint32_t count = *(uint32_t*)(set + 8);
        std::fwrite(&va, 4, 1, f);
        std::fwrite(&count, 4, 1, f);
        std::printf("set %08x: %u bands\n", va, count);
        for (uint32_t i = 0; i < count; i++) {
            char* e = entries + i * 0x20;
            uint32_t size = *(uint32_t*)(e + 4);
            float* data = *(float**)(e + 0xc);
            uint32_t hdr[4] = { size, *(uint32_t*)(e + 0x10), *(uint32_t*)(e + 0x14), *(uint32_t*)(e + 0x18) };
            std::fwrite(hdr, 4, 4, f);
            std::fwrite(data, 4, size, f);
            std::printf("  band %2u size %u inc [%08x, %08x] f18 %08x f1c %08x\n", i, size, hdr[1], hdr[2], hdr[3], *(uint32_t*)(e + 0x1c));
        }
    }
    std::fclose(f);
    return 0;
}
