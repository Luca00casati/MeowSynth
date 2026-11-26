// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Luca Casati
#include <windows.h>
#include "damsdk/gui/platform/windows/GDIDrawingContext.h"

// DamSDK loads the panel bitmaps and registers its window class with this module's handle.
extern "C" BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) DamSDK::Gui::Platform::Windows::g_hInstance = instance;
    return TRUE;
}
