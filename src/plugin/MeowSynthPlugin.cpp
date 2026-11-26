// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Luca Casati
#include "MeowSynthPlugin.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace meow {

    namespace {
        const int kOpGetTimeInfo = 7;       // audioMasterGetTime
        const int kTempoValid = 1 << 10;    // kVstTempoValid

        // VstTimeInfo (only the fields read here).
        struct TimeInfo {
            double samplePos, sampleRate, nanoSeconds, ppqPos, tempo, barStartPos, cycleStartPos, cycleEndPos;
            int32_t timeSigNumerator, timeSigDenominator, smpteOffset, smpteFrameRate, samplesToNextClock, flags;
        };

        void copyText(char* dst, const char* src, size_t size) {
            std::strncpy(dst, src, size - 1);
            dst[size - 1] = 0;
        }
    }

    MeowSynthPlugin::MeowSynthPlugin(DamSDK::Api::dispatchFunc hostCallback)
        : DamSDK::Api::AudioBaseExtended(hostCallback, MeowEngine::kNumPrograms, MeowEngine::kNumParams) {
        plugin.inputChannelCount = 0;
        plugin.outputChannelCount = 2;
        setSupportsInPlaceProcessing(true);
        setIsSynthesizer(true);
        setPluginId('meow');
        engine.setProgram(0);
    }

    MeowSynthPlugin::~MeowSynthPlugin() {}

    void MeowSynthPlugin::setParameterValue(int32_t index, float value) { engine.setParameter(index, value); }
    float MeowSynthPlugin::getParameterValue(int32_t index) { return engine.getParameter(index); }
    void MeowSynthPlugin::getParameterName(int32_t index, char* text) { copyText(text, MeowEngine::parameterName(index), 24); }
    void MeowSynthPlugin::getParameterValueString(int32_t index, char* text) { engine.parameterDisplay(index, text, 24); }
    void MeowSynthPlugin::getParameterUnitLabel(int32_t, char* text) { text[0] = 0; }
    bool MeowSynthPlugin::canParameterBeAutomated(int32_t index) {
        return index != MeowEngine::kText1 && index != MeowEngine::kText2;
    }

    int32_t MeowSynthPlugin::getActivePresetIndex() { return engine.getProgram(); }
    void MeowSynthPlugin::loadPresetByIndex(int32_t index) { engine.setProgram(index); }
    void MeowSynthPlugin::setCurrentPresetName(char*) {}
    void MeowSynthPlugin::getCurrentPresetName(char* text) { copyText(text, MeowEngine::programName(engine.getProgram()), 24); }
    bool MeowSynthPlugin::getPresetNameByIndex(int32_t, int32_t index, char* text) {
        if (index < 0 || index >= MeowEngine::kNumPrograms) return false;
        copyText(text, MeowEngine::programName(index), 24);
        return true;
    }

    void MeowSynthPlugin::setSampleRate(float sampleRate) {
        AudioBaseExtended::setSampleRate(sampleRate);
        engine.setSampleRate(sampleRate);
    }

    void MeowSynthPlugin::enableAudioProcessing() { requestMidiSupport(1); }

    void MeowSynthPlugin::updateTempo() {
        // getHostTransportTime() returns int32_t, which would truncate the pointer on 64-bit.
        if (!hostCallback) return;
        intptr_t r = hostCallback(&plugin, kOpGetTimeInfo, 0, kTempoValid, nullptr, 0.0f);
        const TimeInfo* t = reinterpret_cast<const TimeInfo*>(r);
        if (t && (t->flags & kTempoValid) && t->tempo > 0.0) engine.setTempo(t->tempo);
    }

    void MeowSynthPlugin::render(float* left, float* right, int32_t frames) {
        updateTempo();
        engine.process(left, right, frames);
    }

    void MeowSynthPlugin::processAudio(float**, float** outputs, int32_t frames) { render(outputs[0], outputs[1], frames); }

    void MeowSynthPlugin::invokeAudioProcess(float**, float** outputs, int32_t frames) {
        for (int32_t pos = 0; pos < frames; pos += 4096) {
            const int32_t n = std::min<int32_t>(4096, frames - pos);
            render(scratch[0], scratch[1], n);
            for (int32_t i = 0; i < n; i++) {
                outputs[0][pos + i] += scratch[0][i];
                outputs[1][pos + i] += scratch[1][i];
            }
        }
    }

    int32_t MeowSynthPlugin::processEvents(void* events) {
        const DamSDK::Api::DamEventList* list = static_cast<const DamSDK::Api::DamEventList*>(events);
        for (int i = 0; i < list->count; i++) {
            const DamSDK::Api::DamEvent* e = list->events[i];
            if (e->eventType != 1) continue;  // MIDI
            const DamSDK::Api::DamMidiEvent* m = reinterpret_cast<const DamSDK::Api::DamMidiEvent*>(e);
            engine.midi(e->frames, m->midiData[0], m->midiData[1], m->midiData[2]);
        }
        return 1;
    }

    bool MeowSynthPlugin::getPluginName(char* text) { copyText(text, "MeowSynth", 32); return true; }
    bool MeowSynthPlugin::getCompanyName(char* text) { copyText(text, "knobster.org", 64); return true; }
    bool MeowSynthPlugin::getProductName(char* text) { copyText(text, "MeowSynth", 64); return true; }
    int32_t MeowSynthPlugin::getCompanyVersion() { return 1; }
    int32_t MeowSynthPlugin::getPluginCategory() { return 2; }  // kPlugCategSynth
    int32_t MeowSynthPlugin::pluginSupports(char* what) {
        if (!std::strcmp(what, "receiveVstEvents") || !std::strcmp(what, "receiveVstMidiEvent") ||
            !std::strcmp(what, "receiveVstTimeInfo"))
            return 1;
        return -1;
    }

}
