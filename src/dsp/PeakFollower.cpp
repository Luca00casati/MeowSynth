// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Luca Casati
#include "PeakFollower.h"
#include <cmath>

namespace meow {

    float PeakFollower::coefficient(float t) const {
        double v = t <= 0.001 ? 0.001 : (double)t;
        return (float)std::exp(-100.0 / (v * sampleRate));
    }

    void PeakFollower::process(const float* in, float* out, int count) {
        const double kFlush = 1.000000045813705e-18;  // 1e-18f
        for (int i = 0; i < count; i++) {
            double x = in[i];
            if (x < 0.0) x = -x;
            double c = x <= (double)level ? decay : attack;
            level = (float)(((double)level - x) * c + x);
            level = (float)(((double)level + kFlush) - kFlush);
            out[i] = level;
        }
    }

}
