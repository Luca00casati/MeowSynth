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
#include "dsp/CrossFade.h"
#include "dsp/Adsr.h"
#include "dsp/MidiToCv.h"
#include "dsp/Oscillator.h"
#include "dsp/Slider.h"
#include "RenderScenario.h"
#include <limits>
#include <string>
#include <cmath>
#include <cstdlib>
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

// X-Mix: Mix is static while it equals the previous sample (as for the VCA's Volume).
static void checkCrossFade(const Trace& t) {
    CrossFade xf(CrossFade::kEqualPowerSin);
    for (auto* in : t.find("ug_cross_fade")) {
        auto a = t.samples(*in, 0), b = t.samples(*in, 1), mix = t.samples(*in, 2), out = t.samples(*in, 3);
        Result r;
        for (size_t i = 1; i < out.size(); i++) {
            float o;
            if (mix[i] == mix[i - 1]) {
                float gA, gB;
                xf.staticGains(mix[i], gA, gB);
                xf.processStatic(&a[i], &b[i], gA, gB, &o, 1);
            } else {
                xf.processStreaming(&a[i], &b[i], &mix[i], &o, 1);
            }
            compare(r, (long)i, o, out[i]);
        }
        char name[64];
        std::snprintf(name, sizeof(name), "ug_cross_fade #%d", in->n);
        report(name, r);
    }
}

// ADSR: input state changes are derived from the samples (streaming while the value changes,
// static from the first repeated value) and delivered as events at that sample.
static void checkAdsr(const Trace& t) {
    for (auto* in : t.find("ug_adsr")) {
        std::vector<float> pins[Adsr::kNumInputs];
        for (int p = 0; p < Adsr::kNumInputs; p++) pins[p] = t.samples(*in, p);
        auto ref = t.samples(*in, 6);
        const size_t n = ref.size();
        std::vector<float> out(n, std::numeric_limits<float>::quiet_NaN());
        const float* buf[Adsr::kNumInputs];
        for (int p = 0; p < Adsr::kNumInputs; p++) buf[p] = pins[p].data();

        Adsr env;
        bool streaming[Adsr::kNumInputs] = {};
        for (int p = 0; p < Adsr::kNumInputs; p++) env.setInput(p, pins[p][0], false, buf, 0);
        // Process in runs between events, never across a 128-sample block boundary (the
        // original's process calls are split exactly like that, and its accumulators are
        // rounded to float between calls).
        size_t runStart = 1;
        for (size_t i = 1; i <= n; i++) {
            bool event = i == n || i % t.block == 0;
            bool fire[Adsr::kNumInputs] = {};
            bool newStream[Adsr::kNumInputs] = {};
            if (i < n) {
                for (int p = 0; p < Adsr::kNumInputs; p++) {
                    bool changed = pins[p][i] != pins[p][i - 1];
                    bool next = i + 1 < n && pins[p][i + 1] != pins[p][i];
                    bool s2 = p == Adsr::kGate ? false : (changed && next);
                    if (s2 != streaming[p] || (!s2 && changed)) { fire[p] = true; newStream[p] = s2; event = true; }
                }
            }
            if (event) {
                if (i > runStart) env.process(buf, out.data(), (int)runStart, (int)(i - runStart));
                runStart = i;
                for (int p = 0; p < Adsr::kNumInputs; p++)
                    if (fire[p]) { streaming[p] = newStream[p]; env.setInput(p, pins[p][i], newStream[p], buf, (int)i); }
            }
        }
        Result r;
        for (size_t i = 1; i < n; i++)
            if (!std::isnan(out[i])) compare(r, (long)i, out[i], ref[i]);
        if (getenv("DEBUG_ADSR") && r.first >= 0 && atoi(getenv("DEBUG_ADSR")) == in->n)
            for (long i = r.first - 3; i < r.first + 6; i++)
                std::printf("  %ld mine %.9g ref %.9g | gate %g lvl %.9g A %.7g D %.7g S %.7g R %.7g\n", i, out[i], ref[i],
                            pins[0][i], pins[5][i], pins[1][i], pins[2][i], pins[3][i], pins[4][i]);
        char name[64];
        std::snprintf(name, sizeof(name), "ug_adsr #%d", in->n);
        report(name, r);
    }
}

// Midi to CV: replays the scenario's MIDI and compares Gate, Pitch and Velocity with the voice
// that plays (mono mode).
static void checkMidiToCv(const Trace& t, const char* scenario, int bendRange) {
    const RenderScenario::Scenario* sc = nullptr;
    for (int s = 0; s < RenderScenario::kNumScenarios; s++)
        if (!std::strcmp(RenderScenario::kScenarios[s].name, scenario)) sc = &RenderScenario::kScenarios[s];
    if (!sc) return;
    const TraceInstance* active = nullptr;
    for (auto* in : t.find("ug_midi_to_cv")) {
        auto g = t.samples(*in, 3);
        for (float v : g) if (v != 0.0f) { active = in; break; }
        if (active) break;
    }
    if (!active) return;
    auto port = t.samples(*active, 9);
    const char* names[3] = { "gate", "pitch", "velocity" };
    std::vector<float> ref[3] = { t.samples(*active, 3), t.samples(*active, 4), t.samples(*active, 5) };
    const size_t n = ref[0].size();
    std::vector<float> mine[4];
    for (auto& m : mine) m.assign(n, 0.0f);
    float* outs[4] = { mine[0].data(), mine[1].data(), mine[2].data(), mine[3].data() };

    MidiToCv cv;
    cv.setBendRange(bendRange);
    for (size_t b = 0; b * t.block < n; b++) {
        size_t s0 = b * t.block;
        cv.setPortamentoTime(port[s0]);
        for (int m = 0; m < sc->numMidi; m++) {
            long at = (long)sc->midi[m].block * RenderScenario::kBlockSize + sc->midi[m].offset;
            if (at >= (long)s0 && at < (long)(s0 + t.block))
                cv.midi((int)(at - s0), sc->midi[m].status, sc->midi[m].data1, sc->midi[m].data2);
        }
        cv.process(outs, (int)s0, t.block);
    }
    for (int k = 0; k < 3; k++) {
        Result r;
        for (size_t i = 0; i < n; i++) compare(r, (long)i, mine[k][i], ref[k][i]);
        char name[64];
        std::snprintf(name, sizeof(name), "midi_to_cv #%d %s", active->n, names[k]);
        report(name, r);
        if (getenv("DEBUG_CV") && r.first >= 0 && atoi(getenv("DEBUG_CV")) == k)
            for (long i = r.first - 2; i < r.first + 5; i++)
                std::printf("  %ld mine %.9g ref %.9g\n", i, mine[k][i], ref[k][i]);
    }
}

// Oscillators: waveform / scale / smoothing from the patch (passed per instance by checking
// which configuration reproduces the first block), inputs replayed like the ADSR's.
static void checkOscillators(const Trace& t) {
    struct Config { Oscillator::Waveform wf; bool khz, smooth; const char* name; };
    const Config configs[] = {
        { Oscillator::kRamp, false, false, "ramp" }, { Oscillator::kWhiteNoise, false, false, "noise" },
        { Oscillator::kSine, true, true, "sine kHz" }, { Oscillator::kSine, false, true, "sine" },
    };
    // trace plug -> Oscillator input
    const int plugOf[Oscillator::kNumInputs] = { 0, 1, 4, 5, 7 };
    for (auto* in : t.find("ug_oscillator2")) {
        std::vector<float> pins[Oscillator::kNumInputs];
        for (int k = 0; k < Oscillator::kNumInputs; k++) pins[k] = t.samples(*in, plugOf[k]);
        auto ref = t.samples(*in, 3);
        const size_t n = ref.size();
        const float* buf[Oscillator::kNumInputs];
        for (int k = 0; k < Oscillator::kNumInputs; k++) buf[k] = pins[k].data();
        Result best; const char* bestName = "?";
        for (auto& cfg : configs) {
            if (cfg.wf == Oscillator::kWhiteNoise) continue;  // shared generator, checked separately
            Oscillator osc(cfg.wf, cfg.khz, cfg.smooth);
            std::vector<float> out(n, 0.0f);
            bool streaming[Oscillator::kNumInputs] = {};
            for (int k = 0; k < Oscillator::kNumInputs; k++) osc.setInput(k, pins[k][0], false, buf, 0);
            size_t runStart = 0;
            for (size_t i = 1; i <= n; i++) {
                bool event = i == n || i % t.block == 0;
                bool fire[Oscillator::kNumInputs] = {}, ns[Oscillator::kNumInputs] = {};
                if (i < n)
                    for (int k = 0; k < Oscillator::kNumInputs; k++) {
                        bool changed = pins[k][i] != pins[k][i - 1];
                        bool next = i + 1 < n && pins[k][i + 1] != pins[k][i];
                        bool s2 = k == Oscillator::kSync ? false : (changed && next);
                        if (s2 != streaming[k] || (!s2 && changed)) { fire[k] = true; ns[k] = s2; event = true; }
                    }
                if (event) {
                    if (i > runStart) osc.process(buf, out.data(), (int)runStart, (int)(i - runStart));
                    runStart = i;
                    for (int k = 0; k < Oscillator::kNumInputs; k++)
                        if (fire[k]) { streaming[k] = ns[k]; osc.setInput(k, pins[k][i], ns[k], buf, (int)i); }
                }
            }
            Result r;
            size_t from = getenv("OSC_FROM") ? (size_t)atol(getenv("OSC_FROM")) : t.block * 2;
            for (size_t i = from; i < n; i++) compare(r, (long)i, out[i], ref[i]);
            if (best.checked == 0 || r.mismatches < best.mismatches) { best = r; bestName = cfg.name; }
            if (getenv("OSC_BLOCKS") && atoi(getenv("OSC_BLOCKS")) == in->n) {
                std::string line;
                for (size_t b = 0; b * t.block < n; b++) {
                    int bad = 0;
                    for (int k = 0; k < t.block; k++) bad += out[b * t.block + k] != ref[b * t.block + k];
                    line += bad == 0 ? '.' : (bad < 20 ? 'x' : 'X');
                }
                std::printf("  [%s] %s\n", cfg.name, line.c_str());
            }
            if (getenv("DEBUG_OSC") && atoi(getenv("DEBUG_OSC")) == in->n && r.first >= 0) {
                std::printf("  [%s]\n", cfg.name);
                for (long i = r.first - 2; i < r.first + 4; i++)
                    std::printf("  %ld mine %.9g ref %.9g pitch %.9g sync %g\n", i, out[i], ref[i], pins[0][i], pins[2][i]);
            }
        }
        char name[64];
        std::snprintf(name, sizeof(name), "osc #%d (%s)", in->n, bestName);
        report(name, best);
    }
}

// Dry/Wet slider in the voice scenario: preset 0 sets it to 1.0, then the scenario to 0.0,
// both before the first block.
static void checkDryWetSlider(const Trace& t) {
    for (auto* in : t.find("ug_slider")) {
        auto ref = t.samples(*in, 6);
        if (ref.empty() || ref[0] == ref[1]) continue;  // the one that glides
        Slider sl(-5.0f, 5.0f);
        sl.setNormalized(1.0f);
        sl.setNormalized(0.0f);
        std::vector<float> out(ref.size());
        for (size_t b = 0; b * t.block < out.size(); b++) sl.process(&out[b * t.block], t.block);
        Result r;
        for (size_t i = 0; i < out.size(); i++) compare(r, (long)i, out[i], ref[i]);
        char name[64];
        std::snprintf(name, sizeof(name), "slider #%d", in->n);
        report(name, r);
    }
}

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: %s trace [scenario [bendRange]]\n", argv[0]); return 1; }
    Trace t;
    if (!t.load(argv[1])) { std::fprintf(stderr, "cannot read %s\n", argv[1]); return 1; }
    std::printf("%zu instances, %d blocks of %d\n", t.inst.size(), t.blocks, t.block);
    checkVca(t);
    checkCrossFade(t);
    checkAdsr(t);
    checkOscillators(t);
    checkDryWetSlider(t);
    if (argc > 2) checkMidiToCv(t, argv[2], argc > 3 ? atoi(argv[3]) : 12);
    return 0;
}
