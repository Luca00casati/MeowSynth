// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Luca Casati
#pragma once
// The VST 2.x plugin: DamSDK's AudioBaseExtended around the MeowSynth engine. It presents
// itself like the original (unique ID 'meow', 27 parameters, 16 programs, stereo synth).
#include "damsdk/api/AudioBaseExtended.h"
#include "engine/MeowEngine.h"

namespace meow {

    class MeowSynthPlugin : public DamSDK::Api::AudioBaseExtended {
    public:
        explicit MeowSynthPlugin(DamSDK::Api::dispatchFunc hostCallback);
        ~MeowSynthPlugin() override;

        void setParameterValue(int32_t index, float value) override;
        float getParameterValue(int32_t index) override;
        void getParameterName(int32_t index, char* text) override;
        void getParameterValueString(int32_t index, char* text) override;
        void getParameterUnitLabel(int32_t index, char* text) override;
        bool canParameterBeAutomated(int32_t index) override;

        int32_t getActivePresetIndex() override;
        void loadPresetByIndex(int32_t index) override;
        void setCurrentPresetName(char* name) override;
        void getCurrentPresetName(char* text) override;
        bool getPresetNameByIndex(int32_t category, int32_t index, char* text) override;

        void setSampleRate(float sampleRate) override;
        void setMaxFramesPerProcess(int32_t blockSize) override;  // effSetBlockSize
        void enableAudioProcessing() override;
        void invokeAudioProcess(float** inputs, float** outputs, int32_t frames) override;  // accumulating
        void processAudio(float** inputs, float** outputs, int32_t frames) override;        // replacing
        int32_t processEvents(void* events) override;

        bool getPluginName(char* text) override;
        bool getCompanyName(char* text) override;
        bool getProductName(char* text) override;
        int32_t getCompanyVersion() override;
        int32_t getPluginCategory() override;
        int32_t pluginSupports(char* what) override;

        // The cat animation frame (0..11): the amplitude envelope in volts, as SynthEdit's
        // Volts2GuiInt passes it to the panel.
        int catFrame() const;

    private:
        void render(float* left, float* right, int32_t frames);
        void updateTempo();

        MeowEngine engine;
        float scratch[2][4096];
    };

}
