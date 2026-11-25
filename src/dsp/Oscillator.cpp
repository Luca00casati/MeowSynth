// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Luca Casati
#include "Oscillator.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace meow {

    uint32_t Oscillator::noiseSeed = 0;

    namespace {
        float flushTiny(float v) { return (v < 2.25346663e-31f && v > -2.25346663e-31f) ? 0.0f : v; }

        // FUN_100aeec0: additive band-limited tables (verified with tools/wavetable_check.py).
        WaveTable build(int kind, bool smoothPeaks) {
            std::vector<double> amp, num;
            double scale;
            if (kind == 0) {
                amp = { 1.0 }; num = { 1.0 }; scale = 1.0;
            } else if (kind == 2) {
                for (int k = 1; k <= 256; k++) { amp.push_back(-1.0 / k); num.push_back(k); }
                scale = 0.64;
            } else {
                int i = 0;
                for (int n = 1; n < 256; n += 2, i++) { amp.push_back((i % 2 == 0 ? 1.0 : -1.0) / (n * n)); num.push_back(n); }
                scale = 0.81;
            }
            for (auto& a : amp) a *= scale * 0.5;
            const int n = (int)amp.size();
            const double sqrt2 = std::sqrt(2.0);
            const double w = (double)6.2831854820251465 * 0.001953125;
            const float pi = 3.14159274101257324f;
            WaveTable bands;
            int i = 0;
            double prevMaxF = 0;
            for (;;) {
                int next;
                if (i == n - 1) {
                    next = i + 1;
                } else {
                    next = i;
                    do { next++; } while (!(sqrt2 * num[i] <= num[next]) && next < n - 1);
                }
                const int top = i;
                double maxf = 2147483648.0 / num[top] * std::pow(2.0, -1.0 / 6.0);
                std::vector<double> a(amp.begin(), amp.begin() + top + 1);
                if (smoothPeaks)
                    for (int k = 0; k <= top; k++)
                        a[k] = (std::cos((num[k] - 1.0) * pi / (top + 1)) * 0.46000000834465027 + 0.54000002145767212) * amp[k];
                WaveBand band;
                band.maxInc = (uint32_t)(int64_t)maxf;
                band.data.resize(514);
                for (int s = 0; s < 514; s++) {
                    double ph = s * w, sum = 0.0;
                    for (int k = top; k >= 0; k--) sum += std::sin(ph * num[k]) * a[k];
                    band.data[s] = flushTiny((float)sum);
                }
                bands.insert(bands.begin(), band);
                if (bands.size() > 1) {
                    bands[1].lowerMax = band.maxInc;
                    bands[1].minInc = (uint32_t)(int64_t)(maxf * std::pow(2.0, -1.0 / 12.0));
                }
                (void)prevMaxF;
                i = next;
                if (!(i < n)) break;
            }
            bands[0].lowerMax = 0;
            bands[0].minInc = 0;
            return bands;
        }
    }

    const WaveTables& WaveTables::get() {
        static const WaveTables t = [] {
            WaveTables w;
            w.sine = build(0, true);
            w.sawSmooth = build(2, true);
            w.saw = build(2, false);
            w.triangle = build(5, false);
            w.silence.data.assign(514, 0.0f);
            return w;
        }();
        return t;
    }

    Oscillator::Oscillator(Waveform wf, bool freqScaleKHz, bool smoothPeaks)
        : waveform(wf), kHz(freqScaleKHz), smooth(smoothPeaks) {
        setSampleRate(44100.0f);
    }

    void Oscillator::setSampleRate(float fs) {
        sampleRate = fs;
        // "osc pitch lookup" (FUN_100af590)
        for (int i = 0; i < 514; i++) {
            double x = ((double)i * 0.00019531250291038305 + 0.800000011920929) * 10.0f - 5.0f;
            pitchTable[i] = (int)(std::pow(2.0, x) * 440.0f * 4294967296.0 / fs + 0.5);
        }
        // FUN_100aec50: table set for the waveform, start from band 0 and force a selection.
        const WaveTables& t = WaveTables::get();
        switch (waveform) {
            case kSine: set = &t.sine; break;
            case kSaw: case kRamp: case kPulse: set = smooth ? &t.sawSmooth : &t.saw; break;
            case kTriangle: set = &t.triangle; break;
            default: set = nullptr;
        }
        bandMax = bandMin = 0xffffffffu;
        if (set) {
            oldBand = newBand = playing = &(*set)[0];
            increment = pitchToIncrement(staticValue[kPitch]);
            selectBand(increment);
        }
        selectProcess();
    }

    uint32_t Oscillator::pitchToIncrement(float pitch) const {
        if (kHz) {
            double hz = (double)pitch * 10000.0f;
            float nyquist = sampleRate * 0.5f;
            if (!(hz < nyquist)) hz = nyquist;
            if (!(hz > 0.0)) hz = 0.0;
            return (uint32_t)(int64_t)(hz * 4294967296.0 / sampleRate + 0.5);
        }
        double d = 10.0 * (double)pitch + 1572864.0;  // stored as a double, not rounded to float
        uint64_t bits;
        std::memcpy(&bits, &d, 8);
        uint32_t lo = (uint32_t)bits, hi = (uint32_t)(bits >> 32);
        int octave = (int)(hi - 0x41380000u);
        uint32_t idx = lo >> 23;
        int t0 = pitchTable[idx];
        int64_t prod = (int64_t)(int32_t)(lo & 0x7fffff) * (int64_t)(pitchTable[idx + 1] - t0);
        uint32_t inc = (uint32_t)(((uint64_t)prod >> 23) & 0xffffffffu) + (uint32_t)t0;
        int shift = 8 - octave;
        if (shift >= 0) return shift > 31 ? 0 : inc >> shift;
        shift = -shift;
        if (shift > 8 || (shift && (inc >> (32 - shift)) != 0)) return 0xffffffffu;
        return inc << shift;
    }

    float Oscillator::inputValue(int input, const float* const* buffers, int offset) const {
        return streaming[input] ? buffers[input][offset] : staticValue[input];
    }

    void Oscillator::selectBand(uint32_t inc) {  // FUN_100aeb60
        if (crossfade < 1 && (inc < bandMin || bandMax < inc) && set) {
            int i = (int)set->size() - 1;
            if ((*set)[i].maxInc < inc) {
                newBand = &WaveTables::get().silence;
                bandMin = 0; bandMax = 0xffffffffu;  // TODO: silence band limits
            } else {
                for (; i > 0; i--)
                    if ((*set)[i].lowerMax <= inc) break;
                newBand = &(*set)[i];
                bandMin = newBand->minInc;
                bandMax = newBand->maxInc;
            }
            crossfade = 20;
            selectProcess();
        }
        if (oldBand) playing = oldBand;
    }

    void Oscillator::selectProcess() {  // FUN_100ae920 (the variants MeowSynth needs)
        base = streaming[kPitch] ? kBaseStreamPitch : kBaseStatic;
    }

    void Oscillator::applyStaticPhaseMod() {  // FUN_100b0c50
        double x = (double)staticValue[kPhaseMod] * staticValue[kPmDepth] * 4294967296.0;
        int32_t rounded = (int32_t)(int64_t)std::nearbyint(x);
        int32_t old = pmOffset;
        pmOffset = (int32_t)(int64_t)x;
        phase += (uint32_t)(old - rounded);
    }

    void Oscillator::setInput(int input, float value, bool isStreaming, const float* const* buffers, int offset) {
        staticValue[input] = value;
        streaming[input] = isStreaming;
        if (input == kPhaseMod || input == kPmDepth) {
            if (!streaming[kPhaseMod] && !streaming[kPmDepth]) applyStaticPhaseMod();
        }
        if (input == kPitch && set && !isStreaming) {
            increment = pitchToIncrement(value);
            selectBand(increment);
        }
        if (input == kSync && !isStreaming && ((value <= 0.0f) != syncLow)) {
            // static sync change (FUN_100ae6b0): a rising edge restarts the phase at exactly 0
            syncLow = !syncLow;
            if (!syncLow) {
                float pm = (float)((double)staticValue[kPmDepth] * staticValue[kPhaseMod] * 4294967296.0);
                pmOffset = (int32_t)(int64_t)(double)pm;
                phase = (uint32_t)(0 - pmOffset);
            }
        }
        selectProcess();
        (void)buffers; (void)offset;
    }

    void Oscillator::runBase(const float* const* buffers, float* out, int offset, int count, const WaveBand* band) {
        if (waveform == kWhiteNoise) {  // 0x100ac6f0
            for (int i = 0; i < count; i++) {
                noiseSeed = noiseSeed * 0x19660du + 0x3c6ef35fu;
                uint32_t bits = (noiseSeed & 0x7fffffu) | 0x3f800000u;
                float f;
                std::memcpy(&f, &bits, 4);
                out[offset + i] = f - 1.5f;
            }
            return;
        }
        const float* t = band->data.data();
        const bool negate = waveform == kRamp;
        const float* pitch = buffers[kPitch];
        for (int i = 0; i < count; i++) {
            uint32_t idx = phase >> 23;
            uint32_t bits = (phase & 0x7fffffu) | 0x3f800000u;
            float f;
            std::memcpy(&f, &bits, 4);
            double v = ((double)f - 1.0) * ((double)t[idx + 1] - t[idx]) + t[idx];
            out[offset + i] = (float)(negate ? -v : v);
            phase += base == kBaseStreamPitch ? pitchToIncrement(pitch[offset + i]) : increment;
        }
    }

    void Oscillator::baseCall(const float* const* buffers, float* out, int offset, int count) {
        // The base process function reads the table (+0x110) first; with streaming pitch it
        // then re-picks the band from its first sample, which may start a cross-fade.
        const WaveBand* band = playing;
        if (waveform != kWhiteNoise && base == kBaseStreamPitch)
            selectBand(pitchToIncrement(buffers[kPitch][offset]));
        runBase(buffers, out, offset, count, band);
    }

    void Oscillator::runCrossfade(const float* const* buffers, float* out, int offset, int count) {  // 0x100acc80
        int k = std::min(count, crossfade);
        uint32_t phase0 = phase;
        int32_t pm0 = pmOffset;
        const WaveBand* savedOld = oldBand;      // +0xa0 on entry
        const WaveBand* savedPlaying = playing;  // +0x110 on entry
        float tmp[128];
        float* scratch = tmp;
        std::vector<float> big;
        if (k > 128) { big.resize(k); scratch = big.data(); }
        baseCall(buffers, scratch - offset, offset, k);  // old band into the scratch buffer
        phase = phase0;
        pmOffset = pm0;
        oldBand = newBand;
        playing = newBand;
        baseCall(buffers, out, offset, k);
        // weight of the old band: starts at remaining * 0.05, minus 1/21 per sample (in a register)
        double w = (double)(float)crossfade * 0.05000000074505806;
        for (int i = 0; i < k; i++) {
            w -= 0.0476190485060215;
            out[offset + i] = (float)((1.0 - w) * out[offset + i] + w * scratch[i]);
        }
        crossfade -= k;
        if (count > k) baseCall(buffers, out, offset + k, count - k);
        if (crossfade == 0) {
            float lastPitch = streaming[kPitch] ? buffers[kPitch][offset + count - 1] : staticValue[kPitch];
            selectBand(pitchToIncrement(lastPitch));
        } else {
            // a new fade was started by the remainder: it fades from the pre-fade band
            oldBand = savedOld;
            playing = savedPlaying;
        }
    }

    void Oscillator::runMain(const float* const* buffers, float* out, int offset, int count) {
        if (crossfade > 0)
            runCrossfade(buffers, out, offset, count);
        else
            baseCall(buffers, out, offset, count);
    }

    void Oscillator::runWithSync(const float* const* buffers, float* out, int offset, int count) {  // FUN_100ae720
        const float* sync = buffers[kSync];
        int i = offset, start = offset, end = offset + count;
        for (;;) {
            while (i < end && ((sync[i] <= 0.0f) == syncLow)) i++;
            if (i > start) runMain(buffers, out, start, i - start);
            if (i == end) break;
            start = i;
            syncLow = !syncLow;
            if (!syncLow) {  // rising edge: sub-sample phase reset
                float prev = i > offset ? sync[i - 1] : lastSync;
                float cur = sync[i];
                uint32_t inc = pitchToIncrement(inputValue(kPitch, buffers, i));
                double incD = (double)(int32_t)inc;
                if ((int32_t)inc < 0) incD += 4294967296.0;
                double after = incD * ((double)cur / ((double)cur - prev));
                float pm = (float)((double)inputValue(kPmDepth, buffers, i) * inputValue(kPhaseMod, buffers, i) * 4294967296.0);
                int32_t pmTrunc = (int32_t)(int64_t)(double)pm;
                pmOffset = pmTrunc;
                phase = (uint32_t)((int32_t)(int64_t)after - pmTrunc);
            }
        }
        lastSync = sync[end - 1];
    }

    void Oscillator::process(const float* const* buffers, float* out, int offset, int count) {
        if (streaming[kSync])
            runWithSync(buffers, out, offset, count);
        else
            runMain(buffers, out, offset, count);
    }

}
