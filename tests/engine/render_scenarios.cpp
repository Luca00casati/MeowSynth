// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Luca Casati
// Renders the scenarios of tests/helpers/RenderScenario.h with this project's engine and compares
// them with the original's renders in tests/fixtures.
//   g++ -O2 -std=c++17 -frounding-math -ffp-contract=off -I src -I tests/helpers
//       tests/engine/render_scenarios.cpp src/dsp/*.cpp src/engine/*.cpp -o render_scenarios
// (or the CMake target render_scenarios)
//   ./render_scenarios tests/fixtures [scenario|all] [hostBlockSize]
#include "RenderScenario.h"
#include "engine/MeowEngine.h"
#include "dsp/Oscillator.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace RenderScenario;

int main(int argc, char** argv) {
    const char* dir = argc > 1 ? argv[1] : "tests/fixtures";
    int failures = 0;
    for (int s = 0; s < kNumScenarios; s++) {
        const Scenario& sc = kScenarios[s];
        if (argc > 2 && std::strcmp(argv[2], "all") && std::strcmp(argv[2], sc.name)) continue;
        const int hostBlock = argc > 3 ? std::atoi(argv[3]) : kBlockSize;
        // The noise generator is shared by the whole DLL; each reference was rendered in a
        // fresh process, so start every scenario from the initial seed.
        meow::Oscillator::noiseSeed = 0;
        meow::MeowEngine e;
        e.setSampleRate((float)kSampleRate);
        e.setTempo(kTempo);
        e.setBlockSize(hostBlock);
        e.setProgram(sc.program);
        std::vector<float> out;
        std::vector<float> l(hostBlock), r(hostBlock);
        const long total = (long)sc.blocks * kBlockSize;
        for (long pos = 0; pos < total; pos += hostBlock) {
            const int n = (int)std::min<long>(hostBlock, total - pos);
            for (int p = 0; p < sc.numParams; p++) {
                long t = (long)sc.params[p].block * kBlockSize;
                if (t >= pos && t < pos + n) e.setParameter(sc.params[p].index, sc.params[p].value);
            }
            for (int m = 0; m < sc.numMidi; m++) {
                long t = (long)sc.midi[m].block * kBlockSize + sc.midi[m].offset;
                if (t >= pos && t < pos + n) e.midi((int)(t - pos), sc.midi[m].status, sc.midi[m].data1, sc.midi[m].data2);
            }
            e.process(l.data(), r.data(), n);
            for (int i = 0; i < n; i++) { out.push_back(l[i]); out.push_back(r[i]); }
        }
        std::string path = std::string(dir) + "/" + sc.name + ".f32";
        FILE* f = std::fopen(path.c_str(), "rb");
        std::vector<float> ref(out.size());
        size_t got = f ? std::fread(ref.data(), 4, ref.size(), f) : 0;
        if (f) std::fclose(f);
        if (got != out.size()) {  // a missing or short reference is a failure, not a pass
            std::printf("%-8s MISSING reference %s (%zu of %zu samples)\n", sc.name, path.c_str(), got, out.size());
            failures++;
            continue;
        }
        long first = -1, mism = 0;
        double maxAbs = 0;
        for (size_t i = 0; i < std::min(got, out.size()); i++) {
            uint32_t a, b;  // bitwise: -0 and +0 differ too
            std::memcpy(&a, &out[i], 4);
            std::memcpy(&b, &ref[i], 4);
            if (a != b) {
                mism++;
                if (first < 0) first = (long)i / 2;
                maxAbs = std::fmax(maxAbs, std::fabs((double)out[i] - ref[i]));
            }
        }
        std::printf("%-8s %s  mismatching samples %ld of %zu, first frame %ld, max abs diff %.3g\n", sc.name,
                    mism ? "DIFF" : "EXACT", mism, out.size(), first, maxAbs);
        if (mism) {
            failures++;
            if (first >= 0 && argc > 2 && std::strcmp(argv[2], "all"))
                for (long i = first - 2; i < first + 4; i++)
                    std::printf("   frame %ld  L mine %.9g ref %.9g   R mine %.9g ref %.9g\n", i, out[2 * i], ref[2 * i],
                                out[2 * i + 1], ref[2 * i + 1]);
        }
    }
    return failures ? 1 : 0;
}
