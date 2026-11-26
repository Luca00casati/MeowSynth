// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Luca Casati
#include "MeowSynthPlugin.h"

extern "C" __declspec(dllexport) DamSDK::Api::DamPlugin* VSTPluginMain(DamSDK::Api::dispatchFunc hostCallback) {
    if (!hostCallback || !hostCallback(nullptr, DamSDK::Api::hostGetApiVersion, 0, 0, nullptr, 0.0f))
        return nullptr;
    meow::MeowSynthPlugin* p = new meow::MeowSynthPlugin(hostCallback);
    return &p->plugin;
}

// Old hosts look for "main" (exported by name through exports.def).
extern "C" DamSDK::Api::DamPlugin* MeowMain(DamSDK::Api::dispatchFunc hostCallback) { return VSTPluginMain(hostCallback); }
