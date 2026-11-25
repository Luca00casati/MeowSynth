// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Luca Casati
#pragma once
// SynthEdit "X-Mix" (ug_cross_fade): Mix +5 V = Input A, -5 V = Input B.

namespace meow {

    class CrossFade {
    public:
        // Fade Law pin values.
        enum Law { kLinear = 0, kEqualPowerSin = 1, kEqualPowerSqrt = 2 };

        explicit CrossFade(Law law = kEqualPowerSin);

        // Gains for a static Mix (0x100b2dc0): gainA applies to Input A, gainB to Input B.
        void staticGains(float mix, float& gainA, float& gainB) const;

        // Mix streaming (0x10076e10 linear, 0x10076d10 table laws).
        void processStreaming(const float* a, const float* b, const float* mix, float* out, int count) const;
        // Mix static (0x10076c30).
        void processStatic(const float* a, const float* b, float gainA, float gainB, float* out, int count) const;

    private:
        double lookup(float x) const;  // FUN_100dc760: interpolated table read, x in [0, 1]

        Law law;
        const float* table;
    };

}
