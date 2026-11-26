// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Luca Casati
#pragma once
// DamSDK logs through the host project's Utils::log / Utils::logf; MeowSynth doesn't log.

namespace Utils {
    inline void attachConsole() {}
    inline void log(const char*) {}
    template <class... Args> inline void logf(const char*, Args...) {}
}
