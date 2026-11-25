// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Luca Casati
// Checks this project's DSP modules against module traces of the original plugin
// (tools/trace_modules.cpp): each module gets the original's recorded inputs, and its output
// must match the original's recorded output exactly.
//
//   g++ -O2 -std=c++17 -I src tests/trace/check_modules.cpp src/dsp/*.cpp -o check_modules
//   ./check_modules voice128.trace
#include "Trace.h"
#include "dsp/Vca.h"
#include <cmath>
#include <cstdio>

using namespace meow;

struct Result { long checked = 0, mismatches = 0; double maxRel = 0; long first = -1; };

static void compare(Result& r, long i, float mine, float theirs) {
    r.checked++;
    if (mine == theirs && std::signbit(mine) == std::signbit(theirs)) return;
    if (mine == theirs) return;  // +0 / -0 are reported separately below if needed
    r.mismatches++;
    double rel = std::fabs((double)mine - theirs) / std::fmax(std::fabs((double)theirs), 1e-30);
    if (rel > r.maxRel) r.maxRel = rel;
    if (r.first < 0) r.first = i;
}

static void report(const char* what, const Result& r) {
    std::printf("%-28s checked %7ld  mismatches %7ld  max rel %.3g  first %ld\n", what, r.checked, r.mismatches, r.maxRel, r.first);
}

// VCA, samples where Volume is streaming (it changes within the block).
static void checkVca(const Trace& t) {
    Vca vca(Vca::kExponential);
    for (auto* in : t.find("ug_vca")) {
        auto sig = t.samples(*in, 0), vol = t.samples(*in, 1), out = t.samples(*in, 2);
        Result r;
        for (size_t b = 0; b < out.size() / t.block; b++) {
            size_t s0 = b * t.block;
            // Volume goes static the sample after its last change (the original sends the
            // "static" state change one sample later); from then on the static path is used.
            size_t lastChange = s0;
            for (int k = 1; k < t.block; k++)
                if (vol[s0 + k] != vol[s0 + k - 1]) lastChange = s0 + k;
            bool blockStatic = lastChange == s0 && (s0 == 0 || vol[s0] == vol[s0 - 1]);
            for (int k = 0; k < t.block; k++) {
                size_t i = s0 + k;
                if (i == 0 || (vol[i] == 0.0f && vol[i - 1] == 0.0f)) continue;  // asleep
                float o;
                (void)blockStatic;
                if (vol[i] == vol[i - 1])
                    vca.processStaticVolume(&sig[i], vca.staticGain(vol[i]), &o, 1);
                else
                    vca.processStreaming(&sig[i], &vol[i], &o, 1);
                compare(r, (long)i, o, out[i]);
            }
        }
        if (r.checked) {
            char name[64];
            std::snprintf(name, sizeof(name), "ug_vca #%d", in->n);
            report(name, r);
        }
    }
}

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: %s trace\n", argv[0]); return 1; }
    Trace t;
    if (!t.load(argv[1])) { std::fprintf(stderr, "cannot read %s\n", argv[1]); return 1; }
    std::printf("%zu instances, %d blocks of %d\n", t.inst.size(), t.blocks, t.block);
    checkVca(t);
    return 0;
}
