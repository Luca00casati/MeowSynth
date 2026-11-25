// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Luca Casati
#include "Delay.h"
#include "Fpu.h"

namespace meow {

    void Delay2::setup(float fs, float seconds) {
        size = (int)((double)fs * seconds);
        if (size < 1) size = 1;
        if ((double)fs * 10.0f < (double)(float)size) size = (int)fs * 10;  // TODO: exact cap
        buffer.assign(size, 0.0f);
        writePos = 0;
    }

    void Delay2::setModulation(float m) {
        ScopedRoundTowardZero chop;
        double f = ((double)reference - m) * (double)(float)size;
        int o = (int)f;
        frac = (float)(f - (double)o);
        offset = o;
        if (o < 1) offset = 1;
        if (size <= o) offset = size;
    }

    void Delay2::process(const float* in, const float* modulation, float* out, int count) {
        const double kFlush = 1.000000045813705e-18;
        for (int i = 0; i < count; i++) {
            setModulation(modulation[i]);
            buffer[writePos] = (float)((double)last * feedback + in[i]);
            int r = (writePos + offset) % size;
            double v = (double)buffer[(r + 1) % size] * frac + (1.0 - (double)frac) * buffer[r];
            last = (float)((v + kFlush) - kFlush);
            out[i] = last;
            writePos = (writePos + 1) % size;
        }
    }

}
