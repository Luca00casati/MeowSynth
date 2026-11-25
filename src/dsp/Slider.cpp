// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Luca Casati
#include "Slider.h"

namespace meow {

    bool Slider::setNormalized(float p) {
        float v = (float)((((double)max - min) * p + min) * 0.10000000149011612);
        if (v == value)
            return false;
        value = v;
        int length;
        if (type == 8 || (type > 3 && type < 7)) {
            length = 4;
        } else {
            int fs = (int)sampleRate;
            length = (fs + ((fs >> 31) & 0x1f)) >> 5;  // fs / 32, rounded toward zero
        }
        smoother.set(v, length);
        return true;
    }

}
