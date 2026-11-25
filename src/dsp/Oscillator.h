// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Luca Casati
#pragma once
// SynthEdit "Oscillator" (ug_oscillator2): band-limited wavetable oscillator.
//
// Waveforms 0 sine, 1 saw, 2 ramp (negated saw), 3 triangle, 4 pulse (not ported), 5 white
// noise, 6 pink noise (not ported). Pitch is 1 V/oct (volts / 10, 5 V = A440) or 1 V/kHz.
// See docs/dsp-notes.md for the tables, the pitch mapping and the process loops.
#include <cstdint>
#include <vector>

namespace meow {

    struct WaveBand {
        uint32_t minInc = 0, maxInc = 0, lowerMax = 0;  // +0x10 / +0x14 / +0x18
        std::vector<float> data;                         // 514 samples (512 + 2 guard)
    };

    using WaveTable = std::vector<WaveBand>;  // band 0 = lowest pitch, most harmonics

    // The shared wavetable sets, built once (FUN_100aeec0).
    struct WaveTables {
        WaveTable sine, saw, sawSmooth, triangle;
        WaveBand silence;
        static const WaveTables& get();
    };

    class Oscillator {
    public:
        enum Waveform { kSine = 0, kSaw, kRamp, kTriangle, kPulse, kWhiteNoise, kPinkNoise };
        enum Input { kPitch = 0, kPulseWidth, kSync, kPhaseMod, kPmDepth, kNumInputs };

        explicit Oscillator(Waveform wf = kSine, bool freqScaleKHz = false, bool smoothPeaks = false);

        void setSampleRate(float fs);

        // Input state changes (static value, or streaming from the buffers given to process()).
        void setInput(int input, float value, bool streaming, const float* const* buffers, int offset);

        void process(const float* const* buffers, float* out, int offset, int count);

        uint32_t pitchToIncrement(float pitch) const;  // FUN_100b0b40

        static uint32_t noiseSeed;  // DAT_1019a3d4, shared by all noise oscillators

    private:
        enum Base { kBaseStatic, kBaseStreamPitch };

        void selectBand(uint32_t inc);                  // FUN_100aeb60
        void selectProcess();                           // FUN_100ae920
        void runBase(const float* const* buffers, float* out, int offset, int count, const WaveBand* band);
        void baseCall(const float* const* buffers, float* out, int offset, int count);
        void runCrossfade(const float* const* buffers, float* out, int offset, int count);  // 0x100acc80
        void runWithSync(const float* const* buffers, float* out, int offset, int count);   // FUN_100ae720
        void runMain(const float* const* buffers, float* out, int offset, int count);
        void applyStaticPhaseMod();                     // FUN_100b0c50
        float inputValue(int input, const float* const* buffers, int offset) const;

        Waveform waveform;
        bool kHz, smooth;
        float sampleRate = 44100.0f;
        int pitchTable[514];

        float staticValue[kNumInputs] = { 0, 0.5f, 0, 0, 0.5f };
        bool streaming[kNumInputs] = {};

        uint32_t phase = 0;        // +0x90
        uint32_t increment = 0;    // +0x94
        int32_t pmOffset = 0;      // +0x104
        const WaveTable* set = nullptr;      // +0xb4
        const WaveBand* oldBand = nullptr;   // +0xa0 (the band being played)
        const WaveBand* newBand = nullptr;   // +0xac (the band being faded to)
        const WaveBand* playing = nullptr;   // +0x110 (the table the base loop reads)
        uint32_t bandMax = 0xffffffffu, bandMin = 0xffffffffu;  // +0x98 / +0x9c
        int crossfade = 0;         // +0xe8, samples left in a band cross-fade
        bool syncLow = true;       // +0x100
        float lastSync = 0;        // +0xd0
        Base base = kBaseStatic;
    };

}
