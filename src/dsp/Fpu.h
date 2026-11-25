// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Luca Casati
#pragma once
// Floating-point environment helpers.
//
// The original runs on the x87 FPU at 53-bit precision, so intermediate results behave
// like double and are rounded to float when stored. Some SynthEdit process loops switch
// the FPU to round-toward-zero ("chop") for fast float->int conversion; inside them every
// operation, including the final store to float, rounds toward zero. Doing the same
// arithmetic in double with the SSE rounding mode set to toward-zero reproduces it.
#include <cfenv>

namespace meow {

    class ScopedRoundTowardZero {
    public:
        ScopedRoundTowardZero() : previous(std::fegetround()) { std::fesetround(FE_TOWARDZERO); }
        ~ScopedRoundTowardZero() { std::fesetround(previous); }
        ScopedRoundTowardZero(const ScopedRoundTowardZero&) = delete;
        ScopedRoundTowardZero& operator=(const ScopedRoundTowardZero&) = delete;

    private:
        int previous;
    };

}
