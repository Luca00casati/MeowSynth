// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Luca Casati
#include "DhFilter.h"
#include "Fpu.h"
#include <cstdint>
#include <cstring>

namespace meow {

#include "DhFilterTables.inc"

    namespace {
        const double kDenormal = 1e-150;   // 0x1000f340
        const int kSilenceLimit = 1024;    // module field +0x40
        const double kHalfPiIndex = 1570.7963267948965;  // 0x10014928: pi/2 / 0.001

        float flushOutput(double v) {
            float f = (float)v;
            uint32_t bits;
            std::memcpy(&bits, &f, 4);
            return (bits & 0x7f800000u) == 0 ? 0.0f : f;
        }

        double lookup(const double* t, double x, int maxIndex) {
            int i = (int)x;  // fist in chop mode: truncation
            double frac = x - (double)i;
            if (i < 0) i = 0;
            if (i > maxIndex) i = maxIndex;
            return (t[i + 1] - t[i]) * frac + t[i];
        }
    }

    void DhFilter::setSampleRate(float fs) {
        sampleRate = fs;
        nyquist = (double)fs * 0.5;
        radPerHz = 6.2831853071795862 / (double)fs;
        hzCap = nyquist * 0.99;
    }

    void DhFilter::computeCoefficients(float freq, float res) {
        ScopedRoundTowardZero chop;
        cachedFreq = freq;
        double hz = (double)freq * 10.0f;
        if (mode == kPitchRes || mode == kPitchQ) {
            double x = (double)freq * 5000.0f + 1000.0f;
            int i = (int)x;
            double frac = x - (double)i;
            if (i >= 7001) i = 7000;
            if (i < 0) i = 0;
            double lo = kDhHz[i] < hzCap ? kDhHz[i] : hzCap;
            double hi = kDhHz[i + 1] < hzCap ? kDhHz[i + 1] : hzCap;
            hz = (hi - lo) * frac + lo;
        }
        cachedRes = res;
        double q = (double)res * 10.0f;
        if (mode == kPitchRes || mode == kHzRes)
            q = lookup(kDhQ, (double)res * 512.0f, 512);
        gain = 1.0;
        if (gainComp && type < 3) {
            static const double* const kGain[8] = { kDhGain1, kDhGain2, kDhGain3, kDhGain4,
                                                    kDhGain5, kDhGain6, kDhGain7, kDhGain8 };
            gain = lookup(kGain[stages - 1], 0.0111731843575419 * q * 512.0, 511);
        }
        rbj(hz, q);
    }

    void DhFilter::rbj(double hz, double q) {
        if (hz < 1.0) hz = 1.0;
        if (nyquist < hz) hz = nyquist;
        if (q < 0.001) q = 0.001;
        double w = hz * radPerHz * 1000.0;
        double s = lookup(kDhSine, w, 4712);
        double c = lookup(kDhSine, kHalfPiIndex + w, 4712);
        double alpha = s / (q + q);
        double a0inv = 1.0 / (alpha + 1.0);
        double cm2 = c * -2.0;
        switch (type) {
            case kLowPass:   b1 = 1.0 - c;  b0 = b2 = b1 * 0.5; break;
            case kHighPass:  b1 = -1.0 - c; b0 = b2 = b1 * -0.5; break;
            case kBandPass1: b1 = 0.0; b0 = s * 0.5; b2 = -b0; break;
            case kBandPass2: b1 = 0.0; b0 = alpha; b2 = -alpha; break;
            case kNotch:     b0 = b2 = 1.0; b1 = cm2; break;
        }
        a1 = cm2;
        a2 = 1.0 - alpha;
        a0inv = a0inv + kDenormal;
        b0 = a0inv * b0;
        b1 = a0inv * b1;
        b2 = a0inv * b2;
        a1 = cm2 * a0inv;
        a2 = a0inv * a2;
    }

    void DhFilter::run(const float* in, const float* freq, const float* res, float* out, int count) {
        static const int kCheck[4] = { 0, 3, 2, 1 };
        int i = 0;
        // groups of 4 (0x10001c10, unrolled), only when at least 4 samples remain
        if (count - 1 > 2) {
            int groups = (unsigned)count >> 2;
            for (int g = 0; g < groups; g++, i += 4) {
                int k = kCheck[phase];
                if (freq[i + k] != cachedFreq || res[i + k] != cachedRes) computeCoefficients(freq[i + k], res[i + k]);
                double v0 = in[i], v1 = in[i + 1], v2 = in[i + 2], v3 = in[i + 3];
                for (int s = 0; s < stages; s++) {
                    double f0 = (v0 * b0 + x2[s] * b2) + x1[s] * b1;
                    double f1 = (v1 * b0 + b2 * x1[s]) + v0 * b1;
                    double f2 = (v2 * b0 + v0 * b2) + v1 * b1;
                    double f3 = (v3 * b0 + v1 * b2) + v2 * b1;
                    x1[s] = v3;
                    x2[s] = v2;
                    double o0 = ((f0 - a1 * y1[s]) - y2[s] * a2) + kDenormal;
                    double o1 = (f1 - o0 * a1) - y1[s] * a2;
                    double o2 = (f2 - o1 * a1) - o0 * a2;
                    double o3 = (f3 - o2 * a1) - o1 * a2;
                    y2[s] = o2;
                    y1[s] = o3;
                    v0 = o0; v1 = o1; v2 = o2; v3 = o3;
                }
                double v[4] = { v0, v1, v2, v3 };
                for (int k2 = 0; k2 < 4; k2++) {
                    out[i + k2] = flushOutput(v[k2] * gain);
                    if (k2 == 3) silent = out[i + 3] == 0.0f ? silent + 1 : 0;
                }
                phase = (phase + 4) & 3;
            }
        }
        for (; i < count; i++) {
            if (phase == 0 && (freq[i] != cachedFreq || res[i] != cachedRes)) computeCoefficients(freq[i], res[i]);
            double x = in[i], y = x;
            for (int s = 0; s < stages; s++) {
                y = (((x * b0 + b2 * x2[s] + x1[s] * b1) - y1[s] * a1) - y2[s] * a2) + kDenormal;
                x2[s] = x1[s];
                x1[s] = x;
                y2[s] = y1[s];
                y1[s] = y;
                x = y;
            }
            out[i] = flushOutput(y * gain);
            silent = out[i] == 0.0f ? silent + 1 : 0;
            phase = (phase + 1) & 3;
        }
        if (silent > kSilenceLimit) sleeping = true;
    }

    void DhFilter::inputStateChanged(int pin, bool audioStreaming) {
        suspended = zeroing = false;
        sleeping = true;  // the handler first selects the sleep process
        switch (pin) {
            case 1: case 2:  // Freq, Q|Res: back to normal processing
                sleeping = false;
                break;
            case 3: case 5: case 7:  // type, stages, input mode: clear the state
                for (int s = 0; s < 8; s++) x1[s] = x2[s] = y1[s] = y2[s] = 0.0;
                cachedFreq = cachedRes = 0.0f;
                break;
            case 4:  // gain compensation
                cachedFreq = cachedRes = 0.0f;
                break;
            default:  // audio input: sleep until a non-zero sample, which resets the state
                break;
        }
        if (!audioStreaming) {  // the audio input is static: write zeros, then stop
            zeroCountdown = kSilenceLimit;
            zeroing = true;
        }
    }

    void DhFilter::process(const float* in, const float* freq, const float* res, float* out, int count) {
        int i = 0;
        if (suspended) {  // the host no longer calls the module: its output buffer keeps zeros
            for (; i < count; i++) out[i] = 0.0f;
            return;
        }
        if (zeroing) {  // 0x10002aa0
            for (; i < count; i++) out[i] = 0.0f;
            zeroCountdown -= count;
            if (zeroCountdown < 1) suspended = true;
            return;
        }
        if (sleeping) {  // 0x100020d0: pass silence; wake on the first non-zero input
            while (i < count && in[i] == 0.0f) {
                out[i++] = 0.0f;
                silent++;
            }
            if (i == count) return;
            for (int s = 0; s < 8; s++) x1[s] = x2[s] = y1[s] = y2[s] = 0.0;
            silent = 0;
            sleeping = false;
        }
        run(in + i, freq + i, res + i, out + i, count - i);
    }

}
