// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Luca Casati
#include "CrossFade.h"
#include "Fpu.h"
#include <cmath>

namespace meow {

    namespace {
        // Stores a table value like the original's table setter (FUN_100dc660).
        float flushTiny(float v) {
            return (v < 2.25346663e-31f && v > -2.25346663e-31f) ? 0.0f : v;
        }

        // "Sin Equal Power Curve" and "Equal Power Curve" (FUN_100b3320).
        struct FadeTables {
            float sine[514], sqrtCurve[514];
            FadeTables() {
                const float step = (float)(3.14159265358979323846 / 1024.0);  // 0x10150008
                for (int i = 0; i < 514; i++) {
                    sine[i] = flushTiny((float)std::sin((double)i * step));
                    sqrtCurve[i] = flushTiny(std::sqrt((float)i * 0.001953125f));
                }
            }
        };

        const FadeTables& fadeTables() {
            static const FadeTables t;
            return t;
        }

        float position(float mix) {  // 0 = all Input A, 1 = all Input B
            float f = 0.5f - mix;
            if (!(f > 0.0f)) return 0.0f;
            return f >= 1.0f ? 1.0f : f;
        }
    }

    CrossFade::CrossFade(Law l)
        : law(l), table(l == kEqualPowerSin ? fadeTables().sine : fadeTables().sqrtCurve) {}

    double CrossFade::lookup(float x) const {
        double r;
        {
            ScopedRoundTowardZero chop;  // the original runs this lookup in chop mode
            double fx = (double)x * 512.0;
            if ((int)x < 0) fx = 0.0;
            int i = (int)fx;
            double a = table[i];
            r = a + ((double)table[i + 1] - a) * (fx - (double)i);
        }
        return r;  // callers round it to float (or not) after the rounding mode is restored
    }

    void CrossFade::staticGains(float mix, float& gainA, float& gainB) const {
        float f = position(mix);
        if (law == kLinear) {
            gainB = f;
            gainA = 1.0f - f;
        } else {
            gainB = (float)lookup(f);
            gainA = (float)lookup(1.0f - f);
        }
    }

    void CrossFade::processStreaming(const float* a, const float* b, const float* mix, float* out, int count) const {
        for (int i = 0; i < count; i++) {
            float f = position(mix[i]);
            if (law == kLinear) {
                out[i] = (float)((double)f * b[i] + (double)(1.0f - f) * a[i]);
            } else {
                // Gain B is stored to float and reloaded; gain A is used straight from the FPU.
                float gB = (float)lookup(f);
                double gA = lookup(1.0f - f);
                out[i] = (float)((double)gB * b[i] + gA * a[i]);
            }
        }
    }

    void CrossFade::processStatic(const float* a, const float* b, float gainA, float gainB, float* out, int count) const {
        for (int i = 0; i < count; i++)
            out[i] = (float)((double)a[i] * gainA + (double)b[i] * gainB);
    }

}
