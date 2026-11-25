// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Luca Casati
// Renders one scenario on the original plugin, then prints 32-bit globals (as int and float).
//   i686-w64-mingw32-g++ -O2 -static -std=c++17 -I tests/helpers tools/peek_globals.cpp -o peek_globals.exe
//   wine peek_globals.exe MeowSynth.dll <scenario> 0x10199a68 0x1019a374 ...
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include "RenderScenario.h"

using namespace RenderScenario;
typedef Plugin* (*PluginMain)(HostCallback);

static void useWindowsFpuDefaults() { unsigned short cw = 0x27f; __asm__ volatile("fldcw %0" : : "m"(cw)); }

int main(int argc, char** argv) {
    useWindowsFpuDefaults();
    if (argc < 4) { std::fprintf(stderr, "usage: %s dll scenario addr...\n", argv[0]); return 1; }
    HMODULE dll = LoadLibraryA(argv[1]);
    Plugin* p = ((PluginMain)GetProcAddress(dll, "main"))(&hostCallback);
    for (int s = 0; s < kNumScenarios; s++)
        if (!std::strcmp(kScenarios[s].name, argv[2])) render(p, kScenarios[s]);
    for (int i = 3; i < argc; i++) {
        uint32_t va = (uint32_t)std::strtoul(argv[i], 0, 16);
        uint32_t v = *(uint32_t*)((char*)dll + (va - 0x10000000));
        float f; std::memcpy(&f, &v, 4);
        std::printf("%08x = %u (%#x) float %.9g\n", va, v, v, f);
    }
    return 0;
}
