// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Luca Casati
#pragma once
// The panel, laid out like the original (positions measured on its editor; see docs/gui-notes.md):
// nine knobs, the LFO speed / pitch-bend range / poly selectors, the singing cat and the web link.
#include "damsdk/api/EditorBase.h"
#include "damsdk/gui/controls/control.h"
#include "damsdk/gui/controls/Knob.h"
#include "damsdk/gui/platform/windows/Bitmap.h"
#include <vector>

namespace meow {

    class MeowSynthPlugin;

    // A knob from a vertical strip of frames, picking the frame like SynthEdit: the value is
    // rounded to a whole percent, then to the nearest frame (DamSDK's Knob truncates instead).
    class SkinKnob : public DamSDK::Gui::Controls::Knob {
    public:
        SkinKnob(RECT* r, DamSDK::Gui::Controls::ControlListener* l, int param, int frames, int frameHeight,
                 DamSDK::Gui::Platform::Windows::Bitmap* bmp);
        void onDraw(DamSDK::Gui::Platform::Windows::GDIDrawingContext* dc) override;
    private:
        int frames, frameHeight;
    };

    // An image with one frame per list entry; a click shows the next entry, a right-click the
    // previous one. value = index / divisor (the host parameter's normalization).
    class FrameSelector : public DamSDK::Gui::Controls::Control {
    public:
        FrameSelector(RECT* r, DamSDK::Gui::Controls::ControlListener* l, int param, int frames, int frameHeight,
                      float divisor, DamSDK::Gui::Platform::Windows::Bitmap* bmp);
        void onDraw(DamSDK::Gui::Platform::Windows::GDIDrawingContext* dc) override;
        void onMouseDown(DamSDK::Gui::Platform::Windows::GDIDrawingContext* dc, POINT* where) override;
        int index() const;
    protected:
        int frames, frameHeight;
        float divisor;
    };

    // Display-only animation (the cat): value = frame / (frames - 1).
    class Animation : public FrameSelector {
    public:
        using FrameSelector::FrameSelector;
        void onMouseDown(DamSDK::Gui::Platform::Windows::GDIDrawingContext*, POINT*) override {}
    };

    // The knobster.org link: frame 1 while pressed, opens the page on release.
    class LinkButton : public FrameSelector {
    public:
        using FrameSelector::FrameSelector;
        void onMouseDown(DamSDK::Gui::Platform::Windows::GDIDrawingContext* dc, POINT* where) override;
    };

    class MeowEditor : public DamSDK::Api::EditorBase, public DamSDK::Gui::Controls::ControlListener {
    public:
        explicit MeowEditor(MeowSynthPlugin* plugin);
        ~MeowEditor() override;
        int32_t open(HWND parent) override;
        void close() override;
        void onIdle() override;
        void valueChanged(DamSDK::Gui::Platform::Windows::GDIDrawingContext* dc,
                          DamSDK::Gui::Controls::Control* control) override;
        void parameterChanged(int index, float value);  // from the host or a program change

    private:
        MeowSynthPlugin* plugin;
        DamSDK::Gui::Platform::Windows::Bitmap* background = nullptr;
        std::vector<DamSDK::Gui::Platform::Windows::Bitmap*> bitmaps;
        std::vector<DamSDK::Gui::Controls::Control*> controls;
        Animation* cat = nullptr;
        int program = -1;
    };

}
