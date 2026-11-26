// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Luca Casati
#pragma once
// The MeowSynth patch, wired like the original SynthEdit graph (docs/patch-graph.txt).
//
// SynthEdit processes in blocks of up to 128 samples. Each connection carries a buffer and the
// static/streaming state changes of its source, at exact sample positions; modules react to
// those (path selection, segment restarts, call splits) like the original. This engine mirrors
// that per block: sliders and the shared LFO / chorus modulation first, then each voice in
// data-flow order, then the sum of the voices.
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace meow {

    class MeowEngine {
    public:
        enum Param {
            kAttack = 0, kChorus, kCutoff, kDecay, kDepth, kDryWet, kE, kEnvMod, kFilterType, kGain, kM,
            kMin, kNoise, kO, kText1, kText2, kPhase, kPortamento, kQ, kRate, kVibratoAmount,
            kVibratoAttack, kW, kDivision, kWaveform2, kBendRange, kMonoMode, kNumParams
        };

        MeowEngine();
        ~MeowEngine();

        void setSampleRate(float fs);
        void setTempo(double bpm);
        static const int kNumPrograms = 16;     // 5 named presets, then "Patch 6".."Patch 16"
        void setProgram(int program);
        int getProgram() const;
        static const char* programName(int program);
        void setParameter(int index, float value);  // normalized, takes effect at the next block
        float getParameter(int index) const;
        static const char* parameterName(int index);
        void parameterDisplay(int index, char* text, int size) const;  // like the original

        // MIDI at a sample offset within the next process() call.
        void midi(int offset, unsigned char status, unsigned char data1, unsigned char data2);

        void process(float* left, float* right, int count);

        // The amplitude envelope (0..1) of the loudest running voice at the end of the last
        // block; the panel's cat animation follows it. Safe to read from another thread.
        float envelopeLevel() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };

}
