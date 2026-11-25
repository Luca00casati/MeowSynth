// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Luca Casati
#include "Vca.h"
#include "Fpu.h"
#include <cmath>

namespace meow {

    namespace {
        const double kCurveK = -3.0;  // 0x101546e0 / 0x101546d8

        // "UVCA Trunc Exp Curve" (built in ug_vca Open, 0x100c36b0).
        struct ExpCurveTable {
            float t[514];
            ExpCurveTable() {
                const float scale = (float)(1.0 / (1.0 - std::exp(kCurveK)));
                for (int i = 0; i < 514; i++) {
                    double e = std::exp((1.0 - i * (double)0.001953125f) * kCurveK);
                    t[i] = (float)(1.0 - (1.0 - e) * scale);
                }
            }
        };

        const float* expCurveTable() {
            static const ExpCurveTable table;
            return table.t;
        }
    }

    Vca::Vca(Curve c) : curve(c), table(expCurveTable()) {}

    float Vca::staticGain(float v) const {
        if (!(v > 0.0f))
            return 0.0f;
        double g;
        switch (curve) {
            case kExponential:
                g = 1.0 - (1.0 / (1.0 - std::exp(kCurveK))) * (1.0 - std::exp((1.0 - (double)v) * kCurveK));
                break;
            case kLinear:
                g = v;
                break;
            default:
                g = 132.0;  // TODO: dB curves, not used by MeowSynth
        }
        if (v < 0.001)
            g = g * v * 1000.0;
        return (float)((g + (double)1e-18f) - (double)1e-18f);
    }

    void Vca::processStreaming(const float* signal, const float* volume, float* out, int count) const {
        ScopedRoundTowardZero chop;  // the original sets the x87 to chop mode in this loop
        for (int i = 0; i < count; i++) {
            float v = volume[i];
            if (v <= 1.0f) {
                float x = v * 512.0f;
                if ((int)v < 0)
                    x = 0.0f;
                int index = (int)x;
                double a = table[index], b = table[index + 1];
                out[i] = (float)((a + (b - a) * (double)(x - (float)index)) * (double)signal[i]);
            } else {
                out[i] = (float)((double)staticGain(v) * signal[i]);
            }
        }
    }

    void Vca::processStaticVolume(const float* signal, float gain, float* out, int count) const {
        for (int i = 0; i < count; i++)
            out[i] = signal[i] * gain;
    }

}
