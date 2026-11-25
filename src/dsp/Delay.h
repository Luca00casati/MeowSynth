// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Luca Casati
#pragma once
// SynthEdit "Delay2" (ug_delay2), interpolating, with streaming Modulation (MeowSynth's chorus).
// Buffer of trunc(fs * Delay Time) samples; Modulation 1.0 (10 V) = longest delay.
#include <vector>

namespace meow {

    class Delay2 {
    public:
        void setup(float sampleRate, float delaySeconds);  // 0x100799f0
        void setFeedback(float f) { feedback = f; }
        // 0x10079160: interpolated output, modulation streaming, feedback static.
        void process(const float* in, const float* modulation, float* out, int count);

    private:
        void setModulation(float m);  // 0x1007a7f0 (runs in chop mode)

        std::vector<float> buffer;    // +0xa8
        int size = 1;                 // +0xa4
        int writePos = 0;             // +0xa0
        int offset = 1;               // +0xbc
        float frac = 0.0f;            // +0x9c
        float reference = 1.0f;       // +0x94
        float feedback = 0.0f;        // +0xc0
        float last = 0.0f;            // +0xd0
    };

}
