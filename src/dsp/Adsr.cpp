// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Luca Casati
#include "Adsr.h"
#include <algorithm>
#include <cmath>

namespace meow {

    namespace {
        // Segment -> input holding its time (+0x108) and its target level (+0x128, -1 = constant).
        const int kTimePin[3] = { Adsr::kAttack, Adsr::kDecay, Adsr::kRelease };
        const int kTargetPin[3] = { -1, Adsr::kSustain, -1 };
        const float kConstantTarget[3] = { 1.0f, 0.0f, 0.0f };
    }

    Adsr::Adsr() : segment(2) {}

    float Adsr::rateFor(float v) const {
        // 1 / (2^((v * 10 * 12000 * 0.1 - 8000) / 1200) * fs), evaluated like the x87 code.
        double x = ((double)v * 10.0f * 12000.0f * 0.1f - 8000.0f) * 0.0008333333333333334;
        return (float)(1.0 / (std::pow(2.0, x) * sampleRate));
    }

    float Adsr::inputValue(int pin, const float* const* buffers, int offset) const {
        return streaming[pin] ? buffers[pin][offset] : staticValue[pin];
    }

    void Adsr::startSegment(const float* const* buffers, int offset) {
        bool targetStreaming = false;
        if (segment == kSegmentCount) {
            target = 0.0f;
        } else if (kTargetPin[segment] < 0) {
            target = kConstantTarget[segment];
        } else {
            target = inputValue(kTargetPin[segment], buffers, offset);
            targetStreaming = streaming[kTargetPin[segment]];
        }

        if (rate == 0.0f) {  // no duration: hold
            if (streaming[kOverallLevel] && segment == kSustainSegment) {
                mode = kHoldLevelStream;
            } else {
                overall = inputValue(kOverallLevel, buffers, offset);
                current = target;
                mode = kHold;
                holdSamples = 128;  // TODO: DAT_10199a68
            }
            return;
        }

        const int timePin = kTimePin[segment < kSegmentCount ? segment : kSegmentCount];
        bool computeLength = true;
        if (streaming[kOverallLevel] || streaming[timePin]) {
            if (!streaming[timePin]) {
                mode = kRampLevelStream;
                rate = rateFor(staticValue[timePin]);
            } else {
                mode = kRampTimeStream;
                computeLength = false;
            }
        } else {
            mode = kRampStatic;
            rate = rateFor(staticValue[timePin]);
        }
        if (quickRelease) {
            rate = current * 0.06666667f;
            mode = kRampTimeStream;
            target = 0.0f;
            computeLength = true;
        }
        if (!computeLength)
            return;

        rate = (float)std::copysign((double)rate, (double)(target - current));
        // _finite() check (FUN_100f9897) then a truncating conversion (_ftol).
        double samples = (double)(target - current) / (double)rate;
        remaining = std::isfinite(samples) ? (int)samples : (int)0x80000000;
        if (remaining < 2)
            remaining = 1;
        overall = inputValue(kOverallLevel, buffers, offset);
        increment = (float)((double)overall * rate);
        output = (float)((double)overall * current);
    }

    void Adsr::nextSegment(const float* const* buffers, int offset) {
        rate = 0.0f;
        if (segment < kSegmentCount) {
            bool stay = false;
            if (segment == kSustainSegment) {
                float g = inputValue(kGate, buffers, offset);
                stay = g > 0.0f && !quickRelease;
            }
            if (!stay) {
                segment++;
                rate = 1.0f;
            }
        } else {
            quickRelease = false;
        }
        startSegment(buffers, offset);
    }

    void Adsr::gateChanged(float g, const float* const* buffers, int offset) {
        bool high = g > 0.0f;
        if (high == gateHigh)
            return;
        gateHigh = high;
        if (!high) {
            if (!released) {
                released = true;
                if (segment != kSustainSegment || kSegmentCount <= kSustainSegment)
                    segment = kSegmentCount - 1;
                nextSegment(buffers, offset);
            }
        } else if (released) {
            quickRelease = false;
            released = false;
            segment = -1;
            nextSegment(buffers, offset);
        }
    }

    void Adsr::setInput(int pin, float value, bool isStreaming, const float* const* buffers, int offset) {
        staticValue[pin] = value;
        streaming[pin] = isStreaming;
        if (pin == kGate) {
            if (!isStreaming)
                gateChanged(value, buffers, offset);
            return;
        }
        if (rate == 0.0f && kSegmentCount <= segment)
            return;  // finished
        int seg = segment < kSegmentCount ? segment : kSegmentCount;
        if (pin == kOverallLevel || pin == kTimePin[seg] || (seg < kSegmentCount && pin == kTargetPin[seg]))
            startSegment(buffers, offset);
    }

    int Adsr::runSegment(const float* const* buffers, float* out, int offset, int count) {
        const float* levelIn = buffers[kOverallLevel];
        switch (mode) {
            case kIdle:
                return count;

            case kHold: {
                float v = overall * current;
                for (int i = 0; i < count; i++) out[offset + i] = v;
                holdSamples -= count;
                if (holdSamples < 1) mode = kIdle;
                return count;
            }

            case kHoldLevelStream:
                for (int i = 0; i < count; i++) out[offset + i] = levelIn[offset + i] * current;
                return count;

            case kRampStatic: {  // 0x10081550
                int k = std::min(remaining, count);
                remaining -= k;
                if (k > 0) {
                    current = (float)((double)rate * (double)(float)k + current);
                    double acc = output;
                    for (int i = 0; i < k; i++) {
                        acc += increment;
                        out[offset + i] = (float)acc;
                    }
                    output = (float)acc;
                }
                if (remaining == 0 && count != 0) {
                    if (k != 0) {
                        current = target;
                        out[offset + k - 1] = (float)((double)target * overall);
                        return k;
                    }
                    nextSegment(buffers, offset);
                }
                return k;
            }

            case kRampLevelStream: {  // 0x100814a0
                int k = std::min(remaining, count);
                if (k == 0) {
                    if (count != 0) nextSegment(buffers, offset);
                    return 0;
                }
                remaining -= k;
                for (int i = 0; i < k; i++) {
                    double s = (double)rate + current;
                    current = (float)s;
                    out[offset + i] = (float)(s * levelIn[offset + i]);
                }
                if (remaining == 0) {
                    current = target;
                    out[offset + k - 1] = (float)((double)target * levelIn[offset + k - 1]);
                }
                return k;
            }

            case kRampTimeStream: {  // 0x10080b50
                const float* timeIn = buffers[kTimePin[segment < kSegmentCount ? segment : kSegmentCount]];
                bool down = (double)target - current <= 0.0;
                for (int i = 0; i < count; i++) {
                    float r = quickRelease ? -rate : rateFor(timeIn[offset + i]);  // TODO quick release
                    if (down) {
                        current = (float)((double)current - (quickRelease ? -r : r));
                        if (current < target) goto crossed;
                    } else {
                        current = (float)((double)r + current);
                        if (target <= current) goto crossed;
                    }
                    out[offset + i] = levelIn[offset + i] * current;
                    continue;
                crossed:
                    current = target;
                    out[offset + i] = target * levelIn[offset + i];
                    nextSegment(buffers, offset + i);
                    return i;
                }
                return count;
            }
        }
        return count;
    }

    void Adsr::process(const float* const* buffers, float* out, int offset, int count) {
        while (count > 0) {
            int k = runSegment(buffers, out, offset, count);
            offset += k;
            count -= k;
        }
    }

}
