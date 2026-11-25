// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Luca Casati
// Renders the scenarios from tests/helpers/RenderScenario.h with a VST 2.x plugin DLL
// and writes each one as interleaved stereo float32 samples (<outdir>/<name>.f32).
//
// Used to record tests/fixtures/*.f32 from the original MeowSynth:
//   i686-w64-mingw32-g++ -O2 -static -I tests/helpers tools/render_reference.cpp -o render_reference.exe
//   wine render_reference.exe MeowSynth.dll tests/fixtures [scenario]
//
// Each scenario loads a fresh plugin instance, so they do not influence each other.
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include "RenderScenario.h"

using namespace RenderScenario;
typedef Plugin* (*PluginMain)(HostCallback);

// Real Windows hosts (built with MSVC) run the x87 FPU at 53-bit precision (control word
// 0x27f); MinGW's startup code switches to 64-bit. The original's arithmetic depends on it, so
// match a real host before loading the plugin.
static void useWindowsFpuDefaults() { unsigned short cw = 0x27f; __asm__ volatile("fldcw %0" : : "m"(cw)); }

int main(int argc, char** argv) {
    useWindowsFpuDefaults();
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <plugin.dll> <outdir> [scenario]\n", argv[0]);
        return 1;
    }
    HMODULE dll = LoadLibraryA(argv[1]);
    if (!dll) {
        std::fprintf(stderr, "cannot load %s\n", argv[1]);
        return 1;
    }
    PluginMain entry = (PluginMain)GetProcAddress(dll, "main");
    if (!entry)
        entry = (PluginMain)GetProcAddress(dll, "VSTPluginMain");
    if (!entry) {
        std::fprintf(stderr, "no plugin entry point\n");
        return 1;
    }
    for (int s = 0; s < kNumScenarios; s++) {
        const Scenario& sc = kScenarios[s];
        if (argc > 3 && std::strcmp(argv[3], sc.name) != 0)
            continue;
        Plugin* plugin = entry(&hostCallback);
        if (!plugin) {
            std::fprintf(stderr, "plugin refused to load\n");
            return 1;
        }
        std::vector<float> samples = render(plugin, sc);
        std::string path = std::string(argv[2]) + "/" + sc.name + ".f32";
        FILE* f = std::fopen(path.c_str(), "wb");
        if (!f)
            return 1;
        std::fwrite(&samples[0], sizeof(float), samples.size(), f);
        std::fclose(f);
        std::printf("%-10s %u stereo frames -> %s\n", sc.name, (unsigned)(samples.size() / 2), path.c_str());
    }
    return 0;
}
