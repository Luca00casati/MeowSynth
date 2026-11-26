// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Luca Casati
// Opens a VST 2.x plugin's editor in a window and saves a screenshot (24-bit BMP), optionally after
// setting parameters and playing a note: snap_editor.exe plugin.dll out.bmp [program] [index=value ...]
// [play=blocks] [release=blocks] (C4 at velocity 100 for that many 512-sample blocks, then released)
//   i686-w64-mingw32-g++ -O2 -static -I tests/helpers tools/snap_editor.cpp -o snap_editor.exe -lgdi32
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "RenderScenario.h"

using namespace RenderScenario;
typedef Plugin* (*PluginMain)(HostCallback);

static void pump(int ms) {
    DWORD end = GetTickCount() + ms;
    MSG msg;
    while (GetTickCount() < end) {
        while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
        Sleep(10);
    }
}

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: %s plugin.dll out.bmp [program] [i=v ...]\n", argv[0]); return 1; }
    HMODULE dll = LoadLibraryA(argv[1]);
    PluginMain entry = dll ? (PluginMain)GetProcAddress(dll, "VSTPluginMain") : 0;
    if (!entry && dll) entry = (PluginMain)GetProcAddress(dll, "main");
    if (!entry) { std::fprintf(stderr, "cannot load\n"); return 1; }
    Plugin* p = entry(&hostCallback);
    p->dispatcher(p, 0, 0, 0, 0, 0.0f);
    p->dispatcher(p, 10, 0, 0, 0, 44100.0f);
    p->dispatcher(p, 11, 0, 512, 0, 0.0f);
    if (argc > 3) p->dispatcher(p, 2, 0, std::atoi(argv[3]), 0, 0.0f);
    for (int a = 4; a < argc; a++) {
        int i; float v;
        if (std::sscanf(argv[a], "%d=%f", &i, &v) == 2) p->setParameter(p, i, v);
    }
    p->dispatcher(p, 12, 0, 1, 0, 0.0f);
    short* rect = 0;  // ERect: top, left, bottom, right
    p->dispatcher(p, 13, 0, 0, &rect, 0.0f);
    int w = rect ? rect[3] - rect[1] : 400, h = rect ? rect[2] - rect[0] : 300;
    std::printf("editor rect %d x %d\n", w, h);
    WNDCLASSA wc = {};
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(0);
    wc.lpszClassName = "snap";
    RegisterClassA(&wc);
    RECT r = { 0, 0, w, h };
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowA("snap", "snap", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, r.right - r.left, r.bottom - r.top, 0, 0, wc.hInstance, 0);
    p->dispatcher(p, 14, 0, 0, hwnd, 0.0f);
    for (int k = 0; k < 10; k++) { p->dispatcher(p, 19, 0, 0, 0, 0.0f); pump(50); }
    int play = 0, release = 0;
    for (int a = 4; a < argc; a++) {
        std::sscanf(argv[a], "play=%d", &play);
        std::sscanf(argv[a], "release=%d", &release);
    }
    if (play > 0) {
        static float l[512], r[512];
        float* outs[2] = { l, r };
        auto send = [&](unsigned char s, unsigned char d1, unsigned char d2) {
            MidiEvent e = {};
            e.type = 1; e.byteSize = sizeof(MidiEvent);
            e.midiData[0] = (char)s; e.midiData[1] = (char)d1; e.midiData[2] = (char)d2;
            Events ev = {};
            ev.numEvents = 1; ev.events[0] = &e;
            p->dispatcher(p, 25, 0, 0, &ev, 0.0f);
        };
        send(0x90, 60, 100);
        for (int b = 0; b < play + release; b++) {
            if (b == play && release > 0) send(0x80, 60, 0);
            p->processReplacing(p, 0, outs, 512);
            advanceTime(512);
        }
        for (int k = 0; k < 10; k++) { p->dispatcher(p, 19, 0, 0, 0, 0.0f); pump(50); }
    }
    HDC src = GetDC(hwnd);
    HDC mem = CreateCompatibleDC(src);
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = h; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 24;
    void* bits = 0;
    HBITMAP bmp = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, 0, 0);
    SelectObject(mem, bmp);
    PrintWindow(hwnd, mem, PW_CLIENTONLY);
    BitBlt(mem, 0, 0, w, h, src, 0, 0, SRCCOPY);
    GdiFlush();
    int stride = (w * 3 + 3) & ~3;
    BITMAPFILEHEADER fh = {};
    fh.bfType = 0x4d42; fh.bfOffBits = sizeof(fh) + sizeof(bi.bmiHeader); fh.bfSize = fh.bfOffBits + stride * h;
    FILE* f = std::fopen(argv[2], "wb");
    std::fwrite(&fh, sizeof(fh), 1, f); std::fwrite(&bi.bmiHeader, sizeof(bi.bmiHeader), 1, f); std::fwrite(bits, stride, h, f);
    std::fclose(f);
    p->dispatcher(p, 15, 0, 0, 0, 0.0f);
    return 0;
}
