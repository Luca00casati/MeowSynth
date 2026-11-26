// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Luca Casati
#pragma once
// SynthEdit "Midi to CV" (ug_midi_to_cv), mono mode.
//
// Outputs: Gate, Pitch (1 V/oct as volts/10, 5 V = A440), Velocity, Aftertouch.
// - Note-on: pitch glides (portamento) or jumps to the note; the gate drops (retrigger)
//   and rises 3 samples later; velocity eases to vel/127 with a cubic smoother.
// - Pitch bend glides over fs/60 samples and adds to the pitch.
// - Mono note priority (Last / Low / High): releasing the sounding note falls back to
//   another held note.
// Process calls are split at events exactly like the original, since the glide
// position is advanced per call and rounded to float in between.
#include <vector>

namespace meow {

    // Output smoother (the original's helper object behind each Midi to CV output).
    struct Smoother {
        enum Mode { kStep = 0, kLinear = 1, kCubic = 2 };
        Mode mode = kStep;
        int defaultLength = 1;
        float current = 0, target = 0, value = 0, d1 = 0, d2 = 0, d3 = 0;
        int counter = -1;
        bool done = true;

        // FUN_100636a0. Returns true if the output starts streaming (a glide of more than one
        // sample); a one-sample step reports only its completion.
        bool set(float newTarget, int length);
        // FUN_100635f0 / FUN_10063510. Returns the index of the sample where the output reached
        // its target and became static (the original reports a static state change there), or -1.
        int process(float* out, int count);
    };

    class MidiToCv {
    public:
        enum Output { kGate = 0, kPitch, kVelocity, kAftertouch, kNumOutputs };

        MidiToCv();

        void setSampleRate(float fs) { sampleRate = fs; }
        void setMonoMode(bool on) { mono = on; }
        void setRetrigger(bool on) { retrigger = on; }
        void setBendRange(int semitones) { bendRange = semitones; }
        void setPriority(int p) { priority = p; }  // 0 off, 1 low, 2 high, 3 last
        // Portamento Time input (volts / 10); read when a note starts.
        void setPortamentoTime(float v) { portamentoTime = v; }

        // A MIDI message at sample `time` (relative to the start of the next process call).
        void midi(int time, unsigned char status, unsigned char data1, unsigned char data2);

        // Renders count samples into out[output][offset...].
        void process(float* const* out, int offset, int count);

        // Output state changes (static/streaming, as SynthEdit reports them), with sample times
        // relative to the start of the current block; cleared by the caller.
        struct StateChange { int output; int time; bool streaming; float value; };
        std::vector<StateChange> changes;
        int blockStart = 0;  // absolute clock of the current block (set by the caller)

    private:
        struct Event { long time; int type; float a, b; };
        enum { kEvNoteOn, kEvNoteOff, kEvGateOn, kEvGlide, kEvBend };

        void handleMidi(unsigned char status, unsigned char d1, unsigned char d2);
        void masterNoteOn(int channel, int note, int velocity);
        void masterNoteOff(int channel, int note);
        void voiceNoteOn(int channel, int note, int velocity);
        void voiceGlide(float from, float to);
        void voiceBend(float value);
        void advanceMaster(int count);
        void render(float* const* out, int offset, int count);

        float sampleRate = 44100.0f;
        bool mono = true, retrigger = true;
        int bendRange = 12, priority = 3;
        float portamentoTime = 0;

        long clock = 0;
        std::vector<Event> events;

        // master (note stack), +0xf0 / +0x170 / +0x1f0 / +0x270 in the original
        bool held[128] = {};
        unsigned char heldVelocity[128] = {}, heldChannel[128] = {};
        signed char order[16];
        int orderIndex = 0;
        int currentNote = -1;      // +0x288
        float glideTarget = 0;     // +0x338
        float glideSlope = 0;      // +0x33c
        float glidePos = 0;        // +0x340
        float channelBend[16] = {};

        // voice
        float portTarget = 0, portSlope = 0, portCur = 0;  // +0x32c / +0x330 / +0x334
        float bendCur = 0, bendTarget = 0, bendSlope = 0;  // +0x344 / +0x348 / +0x34c
        float gateLevel = 1.0f;                            // +0x9c
        Smoother gate, velocity, aftertouch;
    };

}
