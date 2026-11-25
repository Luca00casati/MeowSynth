// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Luca Casati
// Renders a scenario on the original plugin, then saves memory ranges of a loaded module (for
// example the embedded SEMs, which SynthEdit writes out and loads at a relocated base).
//   i686-w64-mingw32-g++ -O2 -static -std=c++17 -I tests/helpers tools/dump_module_memory.cpp -o dump_module_memory.exe
//   wine dump_module_memory.exe MeowSynth.dll <scenario> <module name> <outdir> <rva>:<bytes> ...
// Each range is written to <outdir>/<rva>.bin.
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "RenderScenario.h"

using namespace RenderScenario;
typedef Plugin* (*PluginMain)(HostCallback);

static void useWindowsFpuDefaults() { unsigned short cw = 0x27f; __asm__ volatile("fldcw %0" : : "m"(cw)); }

int main(int argc, char** argv) {
    useWindowsFpuDefaults();
    if (argc < 6) { std::fprintf(stderr, "usage: %s dll scenario module outdir rva:bytes...\n", argv[0]); return 1; }
    HMODULE dll = LoadLibraryA(argv[1]);
    Plugin* p = ((PluginMain)GetProcAddress(dll, "main"))(&hostCallback);
    for (int s = 0; s < kNumScenarios; s++)
        if (!std::strcmp(kScenarios[s].name, argv[2])) render(p, kScenarios[s]);
    HMODULE mod = GetModuleHandleA(argv[3]);
    if (!mod) { std::fprintf(stderr, "module %s not loaded\n", argv[3]); return 1; }
    std::printf("%s at %p\n", argv[3], mod);
    for (int i = 5; i < argc; i++) {
        unsigned rva = 0, bytes = 0;
        std::sscanf(argv[i], "%x:%x", &rva, &bytes);
        std::string path = std::string(argv[4]) + "/" + std::to_string(rva) + ".bin";
        FILE* f = std::fopen(path.c_str(), "wb");
        std::fwrite((char*)mod + rva, 1, bytes, f);
        std::fclose(f);
        std::printf("  %#x (%u bytes) -> %s\n", rva, bytes, path.c_str());
    }
    return 0;
}
