// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Luca Casati
#pragma once
// SynthEdit "ADSR" (ug_adsr, built on ug_envelope_base).
//
// Two timed segments (0 attack -> 1.0, 1 decay -> Sustain) and a release segment (index 2,
// -> 0). Segment 1 is the sustain segment: the envelope holds there while the gate is high.
// Levels move linearly; the output is level * Overall Level.
//
// The original picks a specialised process function for each segment depending on which
// inputs are streaming, and restarts the current segment (recomputing rate and length from
// the current level) whenever an input relevant to it changes state. This class mirrors
// that: callers report input state changes with setInput() at the sample they happen, then
// call process() for the samples in between.

#include <vector>

namespace meow {

    class Adsr {
    public:
        enum Pin { kGate = 0, kAttack, kDecay, kSustain, kRelease, kOverallLevel, kNumInputs };

        Adsr();

        void setSampleRate(float fs) { sampleRate = fs; }

        // An input's state: its static value, or that it is now streaming (then its value is read
        // from the buffers passed to process()).
        void setInput(int pin, float value, bool streaming, const float* const* buffers, int offset);

        // Processes count samples starting at buffer index offset. buffers[pin] are the input
        // buffers (block-relative); out is the output buffer.
        void process(const float* const* buffers, float* out, int offset, int count);

        float level() const { return current; }

        // Output state changes reported at segment starts (block-relative sample, as given to
        // setInput()/process()); cleared by the caller.
        struct StateChange { int time; bool streaming; float value; };
        std::vector<StateChange> changes;

    private:
        // ug_envelope_base, member names after their offsets in the original.
        void startSegment(const float* const* buffers, int offset);  // vtable +0x84, 0x10080ff0
        void nextSegment(const float* const* buffers, int offset);   // vtable +0x88, 0x10081630
        void gateChanged(float gate, const float* const* buffers, int offset);  // 0x100816d0
        int runSegment(const float* const* buffers, float* out, int offset, int count);
        float inputValue(int pin, const float* const* buffers, int offset) const;
        double rateFor(float timeVolts) const;  // 0x10081330 (result left in the FPU)

        enum Mode {
            kIdle,            // 0x10081620: nothing to do
            kHold,            // 0x10081910: constant level * Overall Level
            kHoldLevelStream, // 0x100813f0: current * Overall Level[i]
            kRampStatic,      // 0x10081550: everything static
            kRampLevelStream, // 0x100814a0: Overall Level streaming
            kRampTimeStream,  // 0x10080b50: segment time streaming
        };

        float sampleRate = 44100.0f;
        float staticValue[kNumInputs] = {};
        bool streaming[kNumInputs] = {};

        Mode mode = kIdle;
        short segment;               // +0x9c
        static const short kSegmentCount = 2;    // +0x9a
        static const short kSustainSegment = 1;  // +0x98
        bool gateHigh = false;       // +0xc8
        bool released = true;        // +0xd5
        bool quickRelease = false;   // +0xd6
        float rate = 0;              // +0xa4 (also a "segment has a duration" flag)
        float current = 0;           // +0xa8
        float target = 0;            // +0xd0
        float overall = 0;           // +0xcc, Overall Level when static
        float increment = 0;         // +0xac = overall * rate
        float output = 0;            // +0xb0 = overall * current
        int remaining = 0;           // +0xa0, samples left in the segment
        int holdSamples = 0;         // +0x80, how long a constant output is written before sleeping
    public:
        int blockSize = 128;         // DAT_10199a68, SynthEdit's block length
    private:
    };

}
