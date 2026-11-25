// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Luca Casati
// Loads a VST 2.x plugin DLL and prints its interface: I/O, parameters, programs.
//   i686-w64-mingw32-g++ -O2 -static -I tests/helpers tools/probe_plugin.cpp -o probe_plugin.exe
//   wine probe_plugin.exe MeowSynth.dll
#include <windows.h>
#include <cstdio>
#include "RenderScenario.h"

using RenderScenario::Plugin;
typedef Plugin* (*PluginMain)(RenderScenario::HostCallback);

static intptr_t disp(Plugin* p, int op, int idx = 0, intptr_t val = 0, void* ptr = 0, float opt = 0) {
    return p->dispatcher(p, op, idx, val, ptr, opt);
}

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: %s <plugin.dll>\n", argv[0]); return 1; }
    HMODULE dll = LoadLibraryA(argv[1]);
    if (!dll) { std::fprintf(stderr, "cannot load %s (%lu)\n", argv[1], GetLastError()); return 1; }
    PluginMain entry = (PluginMain)GetProcAddress(dll, "main");
    if (!entry) entry = (PluginMain)GetProcAddress(dll, "VSTPluginMain");
    Plugin* p = entry ? entry(&RenderScenario::hostCallback) : 0;
    if (!p) { std::fprintf(stderr, "plugin refused to load\n"); return 1; }
    disp(p, 0);                                        // effOpen
    char s[256];
    std::printf("magic=%08x uniqueID=%08x version=%d flags=%#x in=%d out=%d params=%d programs=%d delay=%d\n",
                p->magic, p->uniqueID, p->version, p->flags, p->numInputs, p->numOutputs,
                p->numParams, p->numPrograms, p->initialDelay);
    s[0] = 0; disp(p, 47, 0, 0, s); std::printf("effect name: '%s'\n", s);   // effGetEffectName
    s[0] = 0; disp(p, 48, 0, 0, s); std::printf("vendor: '%s'\n", s);        // effGetVendorString
    std::printf("category=%d isSynth=%d\n", (int)disp(p, 35), (p->flags & 0x100) != 0);
    for (int i = 0; i < p->numParams; i++) {
        char name[256] = "", disp_[256] = "", label[256] = "";
        disp(p, 8, i, 0, name); disp(p, 7, i, 0, disp_); disp(p, 6, i, 0, label);
        std::printf("param %2d '%s' = %.6f ('%s' %s)\n", i, name, p->getParameter(p, i), disp_, label);
    }
    for (int i = 0; i < p->numPrograms; i++) {
        s[0] = 0; disp(p, 29, i, -1, s);               // effGetProgramNameIndexed
        std::printf("program %3d '%s'\n", i, s);
    }
    s[0] = 0; disp(p, 5, 0, 0, s);                     // effGetProgramName
    std::printf("current program %d '%s'\n", (int)disp(p, 3), s);
    disp(p, 1);                                        // effClose
    return 0;
}
