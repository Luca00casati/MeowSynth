// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Luca Casati
#pragma once
// SynthEdit "Slider" (ug_slider): a parameter's DSP-side value. A new normalized value p
// sets the target ((max - min) * p + min) * 0.1 (volts / 10), which the output reaches with
// a linear glide over trunc(fs) / 32 samples (4 samples for slider types 4-6 and 8).
#include "MidiToCv.h"  // Smoother

namespace meow {

    class Slider {
    public:
        Slider(float minVolts, float maxVolts, int type = 3) : min(minVolts), max(maxVolts), type(type) {
            smoother.mode = Smoother::kLinear;
        }

        void setSampleRate(float fs) { sampleRate = fs; }

        // Normalized parameter value (0x100bccf0). Returns false if the target did not change.
        bool setNormalized(float p);

        void process(float* out, int count) { smoother.process(out, count); }
        float target() const { return value; }

    private:
        float min, max;
        int type;
        float sampleRate = 44100.0f;
        float value = 0.0f;  // +0x98
        Smoother smoother;
    };

}
