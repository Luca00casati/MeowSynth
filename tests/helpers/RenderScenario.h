// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Luca Casati
// Based on tests/helpers/RenderScenario.h from the Delay Lama recreation (LGPL-2.1).
#pragma once
// Drives a VST 2.x plugin through fixed scenarios and records its output.
// Used both by the reference renderer (tools/render_reference.cpp, which loads the
// original DLL under Wine) and by the audio tests (which use this project's plugin),
// so both see exactly the same calls.
//
// Each scenario picks a program, sets some parameters and plays MIDI. Parameters
// are used to isolate parts of the signal chain (e.g. filter dry, chorus dry, no
// noise) so modules can be checked one at a time.
#include <cstdint>
#include <cstring>
#include <vector>

namespace RenderScenario {

    struct Plugin;
    typedef intptr_t (*HostCallback)(Plugin* plugin, int32_t opcode, int32_t index, intptr_t value, void* ptr, float opt);
    typedef intptr_t (*DispatcherFunc)(Plugin* plugin, int32_t opcode, int32_t index, intptr_t value, void* ptr, float opt);
    typedef void (*ProcessFunc)(Plugin* plugin, float** inputs, float** outputs, int32_t sampleFrames);
    typedef void (*SetParameterFunc)(Plugin* plugin, int32_t index, float value);
    typedef float (*GetParameterFunc)(Plugin* plugin, int32_t index);

    // VST 2.x AEffect layout
    struct Plugin {
        int32_t magic;
        DispatcherFunc dispatcher;
        ProcessFunc process;
        SetParameterFunc setParameter;
        GetParameterFunc getParameter;
        int32_t numPrograms;
        int32_t numParams;
        int32_t numInputs;
        int32_t numOutputs;
        int32_t flags;
        intptr_t reserved1;
        intptr_t reserved2;
        int32_t initialDelay;
        int32_t realQualities;
        int32_t offQualities;
        float ioRatio;
        void* object;
        void* user;
        int32_t uniqueID;
        int32_t version;
        ProcessFunc processReplacing;
    };

    // VstTimeInfo
    struct TimeInfo {
        double samplePos, sampleRate, nanoSeconds, ppqPos, tempo, barStartPos, cycleStartPos, cycleEndPos;
        int32_t timeSigNumerator, timeSigDenominator, smpteOffset, smpteFrameRate, samplesToNextClock, flags;
    };

    const int kSampleRate = 44100;
    const int kBlockSize = 512;
    const double kTempo = 120.0;

    inline TimeInfo& timeInfo() {
        static TimeInfo t;
        return t;
    }

    inline void resetTime() {
        TimeInfo& t = timeInfo();
        std::memset(&t, 0, sizeof(t));
        t.sampleRate = kSampleRate;
        t.tempo = kTempo;
        t.timeSigNumerator = 4;
        t.timeSigDenominator = 4;
        // playing | ppqPos valid | tempo valid | bars valid | time sig valid
        t.flags = 2 | (1 << 9) | (1 << 10) | (1 << 11) | (1 << 13);
    }

    inline void advanceTime(int frames) {
        TimeInfo& t = timeInfo();
        t.samplePos += frames;
        t.ppqPos = t.samplePos / kSampleRate * kTempo / 60.0;
        t.barStartPos = (double)((long long)(t.ppqPos / 4.0)) * 4.0;
    }

    // Host callback: answers the few questions the plugin asks.
    inline intptr_t hostCallback(Plugin*, int32_t opcode, int32_t, intptr_t, void*, float) {
        switch (opcode) {
            case 1:  return 2400;                         // audioMasterVersion
            case 7:  return (intptr_t)&timeInfo();        // audioMasterGetTime
            case 16: return kSampleRate;                  // audioMasterGetSampleRate
            case 17: return kBlockSize;                   // audioMasterGetBlockSize
            default: return 0;
        }
    }

    struct MidiEvent {
        int32_t type;           // 1 = MIDI
        int32_t byteSize;
        int32_t deltaFrames;
        int32_t flags;
        int32_t noteLength;
        int32_t noteOffset;
        char midiData[4];
        char detune;
        char noteOffVelocity;
        char reserved1;
        char reserved2;
    };

    struct Events {
        int32_t numEvents;
        intptr_t reserved;
        void* events[8];
    };

    // Parameter indices of the original (alphabetical, as SynthEdit lists them).
    enum Param {
        kAttack = 0, kChorus, kCutoff, kDecay, kDepth, kDryWet, kE, kEnvMod, kFilterType, kGain, kM,
        kMin, kNoise, kO, kText1, kText2, kPhase, kPortamento, kQ, kRate, kVibratoAmount,
        kVibratoAttack, kW, kDivision, kWaveform2, kWaveform3, kWaveform4
    };

    struct ScheduledMidi { int block; int offset; unsigned char status, data1, data2; };
    struct ScheduledParam { int block; int index; float value; };

    struct Scenario {
        const char* name;
        int program;
        int blocks;
        const ScheduledParam* params; int numParams;
        const ScheduledMidi* midi; int numMidi;
    };

#define RS_COUNT(a) (int)(sizeof(a) / sizeof(a[0]))

    // Isolate the voice: filter dry, chorus dry, no noise, no vibrato.
#define RS_DRY_CHAIN \
        { 0, kDryWet, 0.0f }, { 0, kChorus, 0.0f }, { 0, kNoise, 0.0f }, { 0, kVibratoAmount, 0.0f }

    const ScheduledParam kVoiceParams[] = { RS_DRY_CHAIN };
    const ScheduledMidi kVoiceMidi[] = {
        {  2, 100, 0x90, 57, 100 },   // A3
        { 40,  37, 0x80, 57,   0 },
        { 60,   0, 0x90, 69,  50 },   // A4, softer
        { 85, 211, 0x80, 69,   0 },
    };

    const ScheduledParam kLegatoParams[] = { RS_DRY_CHAIN, { 0, kPortamento, 0.5f } };
    const ScheduledMidi kLegatoMidi[] = {
        {  2,   0, 0x90, 60, 100 },   // C4
        { 25, 256, 0x90, 67, 100 },   // legato G4 -> portamento glide
        { 45,  17, 0x80, 67,   0 },   // back to C4
        { 55,   0, 0xE0,  0,  96 },   // pitch bend up
        { 65, 300, 0xE0,  0,  64 },   // centre
        { 80,  50, 0x80, 60,   0 },
    };

    const ScheduledParam kFilterParams[] = {
        { 0, kChorus, 0.0f }, { 0, kNoise, 0.0f }, { 0, kVibratoAmount, 0.0f }, { 0, kDryWet, 1.0f },
        { 0, kCutoff, 0.3f }, { 0, kQ, 0.5f },
        { 30, kCutoff, 0.6f }, { 50, kQ, 0.9f }, { 70, kFilterType, 0.5f }, { 90, kFilterType, 1.0f },
    };
    const ScheduledMidi kFilterMidi[] = {
        {  2,   0, 0x90, 48, 110 },
        { 60,   0, 0x90, 55, 110 },
        { 110,  0, 0x80, 55,   0 },
        { 110,  0, 0x80, 48,   0 },
    };

    const ScheduledParam kVibratoParams[] = {
        { 0, kDryWet, 0.0f }, { 0, kChorus, 0.0f }, { 0, kNoise, 0.0f },
        { 0, kVibratoAmount, 0.7f }, { 0, kVibratoAttack, 0.3f },
    };
    const ScheduledMidi kVibratoMidi[] = {
        {  2,   0, 0x90, 64, 100 },
        { 40,   0, 0xB0,  1, 127 },   // mod wheel (vibrato amount)
        { 100,  0, 0x80, 64,   0 },
    };

    const ScheduledParam kChorusParams[] = {
        { 0, kDryWet, 0.0f }, { 0, kNoise, 0.0f }, { 0, kVibratoAmount, 0.0f }, { 0, kChorus, 1.0f },
    };
    const ScheduledMidi kChorusMidi[] = {
        {  2,   0, 0x90, 60, 100 },
        { 100,  0, 0x80, 60,   0 },
    };

    const ScheduledMidi kPresetMidi[] = {
        {  2,   0, 0x90, 60, 100 },
        { 30,   0, 0x90, 64, 100 },
        { 60,   0, 0x80, 64,   0 },
        { 70,   0, 0x80, 60,   0 },
    };

    const Scenario kScenarios[] = {
        { "voice",     0, 100, kVoiceParams,   RS_COUNT(kVoiceParams),   kVoiceMidi,   RS_COUNT(kVoiceMidi) },
        { "legato",    0, 100, kLegatoParams,  RS_COUNT(kLegatoParams),  kLegatoMidi,  RS_COUNT(kLegatoMidi) },
        { "filter",    0, 130, kFilterParams,  RS_COUNT(kFilterParams),  kFilterMidi,  RS_COUNT(kFilterMidi) },
        { "vibrato",   0, 110, kVibratoParams, RS_COUNT(kVibratoParams), kVibratoMidi, RS_COUNT(kVibratoMidi) },
        { "chorus",    0, 110, kChorusParams,  RS_COUNT(kChorusParams),  kChorusMidi,  RS_COUNT(kChorusMidi) },
        { "preset0",   0, 100, 0, 0, kPresetMidi, RS_COUNT(kPresetMidi) },
        { "preset1",   1, 100, 0, 0, kPresetMidi, RS_COUNT(kPresetMidi) },
        { "preset2",   2, 100, 0, 0, kPresetMidi, RS_COUNT(kPresetMidi) },
        { "preset3",   3, 100, 0, 0, kPresetMidi, RS_COUNT(kPresetMidi) },
        { "preset4",   4, 100, 0, 0, kPresetMidi, RS_COUNT(kPresetMidi) },
        { "preset5",   5, 100, 0, 0, kPresetMidi, RS_COUNT(kPresetMidi) },
    };
    const int kNumScenarios = RS_COUNT(kScenarios);

    // Opens the plugin, plays one scenario and returns interleaved stereo samples.
    inline std::vector<float> render(Plugin* plugin, const Scenario& sc) {
        std::vector<float> out;
        resetTime();
        plugin->dispatcher(plugin, 0, 0, 0, 0, 0.0f);                     // effOpen
        plugin->dispatcher(plugin, 10, 0, 0, 0, (float)kSampleRate);      // effSetSampleRate
        plugin->dispatcher(plugin, 11, 0, kBlockSize, 0, 0.0f);           // effSetBlockSize
        plugin->dispatcher(plugin, 2, 0, sc.program, 0, 0.0f);            // effSetProgram
        plugin->dispatcher(plugin, 12, 0, 1, 0, 0.0f);                    // effMainsChanged (on)
        plugin->dispatcher(plugin, 71, 0, 0, 0, 0.0f);                    // effStartProcess

        std::vector<float> left(kBlockSize), right(kBlockSize);
        float* outputs[2] = { &left[0], &right[0] };
        MidiEvent midi[8];

        for (int block = 0; block < sc.blocks; block++) {
            for (int p = 0; p < sc.numParams; p++)
                if (sc.params[p].block == block)
                    plugin->setParameter(plugin, sc.params[p].index, sc.params[p].value);

            Events events;
            std::memset(&events, 0, sizeof(events));
            for (int m = 0; m < sc.numMidi && events.numEvents < 8; m++) {
                if (sc.midi[m].block != block)
                    continue;
                MidiEvent& e = midi[events.numEvents];
                std::memset(&e, 0, sizeof(e));
                e.type = 1;
                e.byteSize = sizeof(MidiEvent);
                e.deltaFrames = sc.midi[m].offset;
                e.midiData[0] = (char)sc.midi[m].status;
                e.midiData[1] = (char)sc.midi[m].data1;
                e.midiData[2] = (char)sc.midi[m].data2;
                events.events[events.numEvents++] = &e;
            }
            if (events.numEvents)
                plugin->dispatcher(plugin, 25, 0, 0, &events, 0.0f);      // effProcessEvents

            std::fill(left.begin(), left.end(), 0.0f);
            std::fill(right.begin(), right.end(), 0.0f);
            plugin->processReplacing(plugin, 0, outputs, kBlockSize);
            advanceTime(kBlockSize);
            for (int i = 0; i < kBlockSize; i++) {
                out.push_back(left[i]);
                out.push_back(right[i]);
            }
        }

        plugin->dispatcher(plugin, 72, 0, 0, 0, 0.0f);                    // effStopProcess
        plugin->dispatcher(plugin, 12, 0, 0, 0, 0.0f);                    // effMainsChanged (off)
        plugin->dispatcher(plugin, 1, 0, 0, 0, 0.0f);                     // effClose
        return out;
    }
}
