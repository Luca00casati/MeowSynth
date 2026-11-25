// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Luca Casati
// Runs a scenario on the original plugin and traces the internal SynthEdit modules: finds
// every module instance in memory by its vtable, then after each host block records the
// samples of every plug buffer, so each module can be checked on its own.
//
//   i686-w64-mingw32-g++ -O2 -static -std=c++17 -I tests/helpers tools/trace_modules.cpp -o trace_modules.exe
//   wine trace_modules.exe MeowSynth.dll ug_vtables.txt <scenario> <blockSize> <out.trace> [--explore]
//
// ug_vtables.txt: lines "<vtable hex> <class>" (made from the DLL's RTTI).
// Output (.trace): text header (instances and plugs), a line "DATA", then for each host block,
// for each traced plug, blockSize float32 samples.
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include "RenderScenario.h"

using namespace RenderScenario;
typedef Plugin* (*PluginMain)(HostCallback);

struct Plug { uint32_t addr; int index; int dir; };
struct Instance { uint32_t addr; std::string cls; std::vector<Plug> plugs; };

static bool readable(const void* p, size_t n) {
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(p, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) return false;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
    return (const char*)p + n <= (const char*)mbi.BaseAddress + mbi.RegionSize;
}
static uint32_t rd(uint32_t a) { return readable((void*)a, 4) ? *(uint32_t*)a : 0; }

static std::vector<Instance> findInstances(const std::map<uint32_t, std::string>& vt) {
    std::vector<Instance> out;
    SYSTEM_INFO si; GetSystemInfo(&si);
    char* p = (char*)si.lpMinimumApplicationAddress;
    MEMORY_BASIC_INFORMATION mbi;
    while (p < (char*)si.lpMaximumApplicationAddress && VirtualQuery(p, &mbi, sizeof(mbi))) {
        bool ok = mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE &&
                  (mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)) && !(mbi.Protect & PAGE_GUARD);
        if (ok) {
            uint32_t* w = (uint32_t*)mbi.BaseAddress;
            size_t n = mbi.RegionSize / 4;
            for (size_t i = 0; i < n; i++) {
                auto it = vt.find(w[i]);
                if (it == vt.end()) continue;
                uint32_t self = (uint32_t)(w + i);
                uint32_t plugs = rd(self + 0x50), count = rd(self + 0x54);
                if (count > 256 || (count && !readable((void*)plugs, count * 4))) continue;
                Instance inst{ self, it->second, {} };
                for (uint32_t k = 0; k < count; k++) {
                    uint32_t pl = rd(plugs + 4 * k);
                    if (!pl || rd(pl + 4) != self) continue;   // plug's owner must be this module
                    inst.plugs.push_back({ pl, (int)k, (int)rd(pl + 0x10) });
                }
                if (!inst.plugs.empty()) out.push_back(inst);
            }
        }
        p = (char*)mbi.BaseAddress + mbi.RegionSize;
    }
    return out;
}

// A plug's sample buffer. Outputs own a buffer object (plug+8); inputs point at their
// source's buffer-object field, so they need one more indirection (see plug GetValue 0x100dcab0).
static float* plugData(uint32_t plug) {
    uint32_t buf = rd(plug + 0x10) == 1 ? rd(plug + 8) : rd(rd(plug + 8));
    float* d = buf ? (float*)rd(buf + 8) : nullptr;
    return d && readable(d, 128 * 4) ? d : nullptr;
}

// Real Windows hosts (built with MSVC) run the x87 FPU at 53-bit precision (control word
// 0x27f); MinGW's startup code switches to 64-bit. The original's arithmetic depends on it, so
// match a real host before loading the plugin.
static void useWindowsFpuDefaults() { unsigned short cw = 0x27f; __asm__ volatile("fldcw %0" : : "m"(cw)); }

int main(int argc, char** argv) {
    useWindowsFpuDefaults();
    if (argc < 6) { std::fprintf(stderr, "usage: %s dll vtables scenario blockSize out [--explore]\n", argv[0]); return 1; }
    std::map<uint32_t, std::string> vt;
    FILE* vf = std::fopen(argv[2], "r");
    char name[256]; unsigned addr;
    while (vf && std::fscanf(vf, "%x %255s", &addr, name) == 2) vt[addr] = name;
    const Scenario* sc = nullptr;
    for (int s = 0; s < kNumScenarios; s++) if (!std::strcmp(kScenarios[s].name, argv[3])) sc = &kScenarios[s];
    int B = std::atoi(argv[4]);
    bool explore = argc > 6 && !std::strcmp(argv[6], "--explore");
    if (!sc || B <= 0) { std::fprintf(stderr, "bad scenario/block size\n"); return 1; }

    HMODULE dll = LoadLibraryA(argv[1]);
    Plugin* p = ((PluginMain)GetProcAddress(dll, "main"))(&hostCallback);
    resetTime();
    p->dispatcher(p, 0, 0, 0, 0, 0.0f);
    p->dispatcher(p, 10, 0, 0, 0, (float)kSampleRate);
    p->dispatcher(p, 11, 0, B, 0, 0.0f);
    p->dispatcher(p, 2, 0, sc->program, 0, 0.0f);
    p->dispatcher(p, 12, 0, 1, 0, 0.0f);
    p->dispatcher(p, 71, 0, 0, 0, 0.0f);

    std::vector<float> left(B), right(B);
    float* outputs[2] = { left.data(), right.data() };
    std::vector<Instance> inst;
    FILE* f = std::fopen(argv[5], "wb");
    std::vector<float> mix;
    const int total = sc->blocks * kBlockSize;
    MidiEvent midi[8];

    for (int pos = 0; pos < total; pos += B) {
        for (int i = 0; i < sc->numParams; i++)
            if (sc->params[i].block * kBlockSize >= pos && sc->params[i].block * kBlockSize < pos + B)
                p->setParameter(p, sc->params[i].index, sc->params[i].value);
        Events ev; std::memset(&ev, 0, sizeof(ev));
        for (int m = 0; m < sc->numMidi && ev.numEvents < 8; m++) {
            int t = sc->midi[m].block * kBlockSize + sc->midi[m].offset;
            if (t < pos || t >= pos + B) continue;
            MidiEvent& e = midi[ev.numEvents]; std::memset(&e, 0, sizeof(e));
            e.type = 1; e.byteSize = sizeof(MidiEvent); e.deltaFrames = t - pos;
            e.midiData[0] = (char)sc->midi[m].status; e.midiData[1] = (char)sc->midi[m].data1; e.midiData[2] = (char)sc->midi[m].data2;
            ev.events[ev.numEvents++] = &e;
        }
        if (ev.numEvents) p->dispatcher(p, 25, 0, 0, &ev, 0.0f);
        std::fill(left.begin(), left.end(), 0.0f); std::fill(right.begin(), right.end(), 0.0f);
        p->processReplacing(p, 0, outputs, B);
        advanceTime(B);
        for (int i = 0; i < B; i++) { mix.push_back(left[i]); mix.push_back(right[i]); }

        if (inst.empty()) {
            inst = findInstances(vt);
            for (size_t i = 0; i < inst.size(); i++) {
                std::fprintf(f, "INST %zu %08x %s", i, inst[i].addr, inst[i].cls.c_str());
                for (auto& pl : inst[i].plugs)
                    std::fprintf(f, " %d:%s:%08x", pl.index, pl.dir == 1 ? "out" : "in", (uint32_t)plugData(pl.addr));
                std::fprintf(f, "\n");
            }
            std::fprintf(f, "DATA %d\n", B);
        }
        if (explore && pos < 8 * B) {
            for (auto& in : inst) {
                if (in.cls != "ug_oscillator2" && in.cls != "ug_container") continue;
                std::printf("pos %d %s %08x clock=%d cont=%08x blockStart=%d +48=%d\n", pos, in.cls.c_str(), in.addr,
                            (int)rd(in.addr + 0x34), rd(in.addr + 0x38), (int)rd(rd(in.addr + 0x38) + 0x4c), (int)rd(rd(in.addr + 0x38) + 0x48));
                for (auto& pl : in.plugs) {
                    float* d = plugData(pl.addr);
                    std::printf("   plug %d dir %d buf %p:", pl.index, pl.dir, d);
                    for (int k = 0; d && k < 6; k++) std::printf(" %.5g", d[k]);
                    std::printf("\n");
                }
            }
        }
        // Record every plug buffer's first B samples. With B = 128 (SynthEdit's internal
        // block) each host block is exactly one internal block, so this is that block's data.
        for (auto& in : inst)
            for (auto& pl : in.plugs) {
                float* d = plugData(pl.addr);
                for (int k = 0; k < B; k++) {
                    float v = d && k < 128 ? d[k] : 0.0f;
                    std::fwrite(&v, 4, 1, f);
                }
            }
    }
    std::fclose(f);
    std::string mixPath = std::string(argv[5]) + ".mix.f32";
    FILE* m = std::fopen(mixPath.c_str(), "wb");
    std::fwrite(mix.data(), 4, mix.size(), m);
    std::fclose(m);
    std::printf("%zu instances traced\n", inst.size());
    return 0;
}
