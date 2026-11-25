// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Luca Casati
#pragma once
// SynthEdit "VCA" (ug_vca): Output = Signal * gain(Volume).
#include <cstdint>

namespace meow {

    class Vca {
    public:
        // Response Curve pin values.
        enum Curve { kDb = 0, kExponential = 1, kLinear = 2, kDbOld = 3 };

        explicit Vca(Curve curve = kExponential);

        // Gain for a static Volume (ug_vca 0x100c28a0).
        float staticGain(float volume) const;

        // Volume streaming (0x100c2a10, curves other than linear).
        void processStreaming(const float* signal, const float* volume, float* out, int count) const;
        // Volume static, signal streaming (0x100c2760).
        void processStaticVolume(const float* signal, float gain, float* out, int count) const;

    private:
        Curve curve;
        const float* table;  // 514 entries, gain for Volume = i / 512
    };

}
