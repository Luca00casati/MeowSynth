// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Luca Casati
#pragma once
// DH_MultiFilter2 (David Haupt's SynthEdit module, v1.0.13), as used by MeowSynth.
//
// A cascade of 1-8 Direct Form I biquads with RBJ-cookbook coefficients, computed from lookup
// tables (src/dsp/DhFilterTables.inc) every 4 samples. Input modes take the cutoff as Hz
// (volts * 10) or as pitch (1 V/oct, 5 V = 440 Hz) and the resonance as Q or as
// a 0-10 V "Res" control. See docs/dsp-notes.md.

namespace meow {

    class DhFilter {
    public:
        enum Type { kLowPass = 0, kHighPass, kBandPass1, kBandPass2, kNotch };
        enum InputMode { kHzQ = 0, kPitchRes, kHzRes, kPitchQ };

        void setSampleRate(float fs);
        void setType(Type t) { type = t; }
        void setGainCompensation(bool on) { gainComp = on; }
        void setStages(int n) { stages = n < 1 ? 1 : (n > 8 ? 8 : n); }
        void setInputMode(InputMode m) { mode = m; }

        // An input pin changed state (static <-> streaming, or a new static value): 0x10002640.
        // The original then starts a new process call, so call process() separately for the
        // samples before and after.
        void inputStateChanged(int pin);

        // in/out: audio; freq/res: the Freq and Q|Res inputs (per sample).
        void process(const float* in, const float* freq, const float* res, float* out, int count);

    private:
        void computeCoefficients(float freq, float res);       // FUN_10002210 (chop mode)
        void rbj(double hz, double q);                         // FUN_100023e0
        void run(const float* in, const float* freq, const float* res, float* out, int count);  // 0x10001c10

        float sampleRate = 44100.0f;
        double nyquist = 22050.0;       // +0x1a8
        double radPerHz = 0.0;          // +0x198 = 2 pi / fs
        double hzCap = 21829.5;         // 0.99 * fs / 2
        int type = kLowPass, stages = 1, mode = kPitchRes;
        bool gainComp = true;

        double x1[8] = {}, x2[8] = {}, y1[8] = {}, y2[8] = {};
        double b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0, gain = 1.0;
        float cachedFreq = 0, cachedRes = 0;  // +0x1c8 / +0x1cc
        unsigned phase = 0;                   // +0x1c0, masked with 3
        int silent = 0;                       // +0x68: consecutive zero outputs
        bool sleeping = true;                 // process function 0x100020d0
    };

}
