// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Luca Casati
#pragma once
// SynthEdit "Peak Follower" (ug_peak_det): one-pole envelope of |input| with separate attack and
// decay coefficients exp(-100 / (max(t, 0.001) * fs)).

namespace meow {

    class PeakFollower {
    public:
        void setSampleRate(float fs) { sampleRate = fs; }
        void setAttack(float t) { attack = coefficient(t); }   // 0x100b7100
        void setDecay(float t) { decay = coefficient(t); }
        void process(const float* in, float* out, int count);  // 0x100b6d80 (static times)

    private:
        float coefficient(float t) const;

        float sampleRate = 44100.0f;
        float level = 0.0f;   // +0x90
        float attack = 0.0f;  // +0xa4
        float decay = 0.0f;   // +0xa8
    };

}
