// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Luca Casati
#include "MeowEditor.h"
#include "Resources.h"
#include "damsdk/gui/MouseButtons.h"
#include "damsdk/gui/platform/windows/GDIDrawingContext.h"
#include "damsdk/gui/platform/windows/Window.h"
#include "engine/MeowEngine.h"
#include "plugin/MeowSynthPlugin.h"
#include <shellapi.h>
#include <cmath>

using namespace DamSDK::Gui;
using namespace DamSDK::Gui::Platform::Windows;

namespace meow {

    namespace {
        struct KnobPlace { int resource, param, x, y; };
        const KnobPlace kKnobs[] = {
            { IDB_KNOB_M, MeowEngine::kM, 200, 28 }, { IDB_KNOB_O, MeowEngine::kO, 230, 28 },
            { IDB_KNOB_E, MeowEngine::kE, 260, 28 }, { IDB_KNOB_W, MeowEngine::kW, 290, 28 },
            { IDB_KNOB_VIBAMOUNT, MeowEngine::kVibratoAmount, 200, 91 },
            { IDB_KNOB_VIBATTACK, MeowEngine::kVibratoAttack, 230, 91 },
            { IDB_KNOB_PORTA, MeowEngine::kPortamento, 200, 146 },
            { IDB_KNOB_NOISE, MeowEngine::kNoise, 230, 146 },
            { IDB_KNOB_CHORUS, MeowEngine::kChorus, 260, 146 },
        };
        const int kKnobFrames = 50, kKnobSize = 30;
        const int kNoParameter = -1;
        const char* const kUrl = "http://knobster.org/projects/meowsynth/";

        POINT kOrigin = { 0, 0 };

        void setRect(RECT* r, int x, int y, int w, int h) { r->left = x; r->top = y; r->right = x + w; r->bottom = y + h; }
    }

    // ---- controls ----------------------------------------------------------------------

    SkinKnob::SkinKnob(RECT* r, Controls::ControlListener* l, int param, int frames, int frameHeight, Bitmap* bmp)
        : Knob(r, l, param, frames, frameHeight, bmp, &kOrigin), frames(frames), frameHeight(frameHeight) {}

    void SkinKnob::onDraw(GDIDrawingContext* dc) {
        // SynthEdit snaps the knob to a whole percent, then picks the nearest frame (the frame
        // boundaries measured on the original sit halfway between hundredths).
        float v = value < 0.0f ? 0.0f : value > 1.0f ? 1.0f : value;
        int percent = (int)std::floor(v * 100.0 + 0.5);
        POINT where = { 0, (long)std::floor(percent * (frames - 1) / 100.0 + 0.5) * frameHeight };
        if (bitmap) bitmap->blit(dc, &rect, &where);
        setDirty(false);
    }

    FrameSelector::FrameSelector(RECT* r, Controls::ControlListener* l, int param, int frames, int frameHeight,
                                 float divisor, Bitmap* bmp)
        : Control(r, l, param, bmp), frames(frames), frameHeight(frameHeight), divisor(divisor) {}

    int FrameSelector::index() const {
        int i = (int)std::lround(value * divisor);
        return i < 0 ? 0 : i >= frames ? frames - 1 : i;
    }

    void FrameSelector::onDraw(GDIDrawingContext* dc) {
        POINT where = { 0, (long)index() * frameHeight };
        if (bitmap) bitmap->blit(dc, &rect, &where);
        setDirty(false);
    }

    void FrameSelector::onMouseDown(GDIDrawingContext* dc, POINT*) {
        if (!isEnabled) return;
        uint32_t buttons = dc->getMouseButtons();
        int i = index() + ((buttons & kRButton) ? frames - 1 : 1);
        value = (float)(i % frames) / divisor;
        if (isDirty()) listener->valueChanged(dc, this);
    }

    void LinkButton::onMouseDown(GDIDrawingContext* dc, POINT*) {
        value = 1.0f;
        setDirty(true);
        listener->valueChanged(dc, this);
        while (dc->getMouseButtons() & kLButton) Sleep(10);
        value = 0.0f;
        setDirty(true);
        listener->valueChanged(dc, this);
        ShellExecuteA(nullptr, "open", kUrl, nullptr, nullptr, SW_SHOWNORMAL);
    }

    // ---- editor ------------------------------------------------------------------------

    MeowEditor::MeowEditor(MeowSynthPlugin* p) : EditorBase(p), plugin(p) {
        background = new Bitmap(IDB_BACKGROUND);
        // DamSDK's Rect names its fields left, top, right, bottom but the host reads VST's ERect
        // (top, left, bottom, right), so the names are off: "right" is the height.
        rect.top = 0;
        rect.left = 0;
        rect.right = (int16_t)background->height;
        rect.bottom = (int16_t)background->width;
        DamSDK::Api::GLOBAL_KNOB_MODE = 2;  // SynthEdit knobs follow vertical mouse moves
    }

    MeowEditor::~MeowEditor() {
        close();
        if (background) background->unregisterBitmap();
    }

    int32_t MeowEditor::open(HWND parent) {
        EditorBase::open(parent);
        RECT size = { 0, 0, background->width, background->height };
        window = new Window(&size, parent, this);
        window->setBackgroundBitmap(background);

        auto add = [&](Controls::Control* c) {
            if (c->parameterId >= 0) c->setValue(plugin->getParameterValue(c->parameterId));
            window->registerControl(c);
            controls.push_back(c);
        };
        auto load = [&](int id) { Bitmap* b = new Bitmap(id); bitmaps.push_back(b); return b; };
        RECT r;
        for (const KnobPlace& k : kKnobs) {
            setRect(&r, k.x, k.y, kKnobSize, kKnobSize);
            add(new SkinKnob(&r, this, k.param, kKnobFrames, kKnobSize, load(k.resource)));
        }
        setRect(&r, 266, 96, 50, 20);  // LFO speed: 10 divisions
        add(new FrameSelector(&r, this, MeowEngine::kDivision, 10, 20, 9.0f, load(IDB_LFO)));
        setRect(&r, 294, 151, 22, 20);  // pitch-bend range: frames 0..11
        add(new FrameSelector(&r, this, MeowEngine::kBendRange, 12, 20, 12.0f, load(IDB_PITCHRANGE)));
        setRect(&r, 199, 195, 122, 20);  // POLYCAT: frame 0 = mono (Mono Mode "On")
        add(new FrameSelector(&r, this, MeowEngine::kMonoMode, 2, 20, 1.0f, load(IDB_POLYCAT)));
        setRect(&r, 15, 188, 167, 30);
        add(new LinkButton(&r, this, kNoParameter, 2, 30, 1.0f, load(IDB_URL)));
        setRect(&r, 18, 20, 160, 120);
        cat = new Animation(&r, this, kNoParameter, 12, 120, 11.0f, load(IDB_CAT));
        add(cat);
        program = plugin->getActivePresetIndex();
        return 1;
    }

    void MeowEditor::close() {
        delete window;  // releases the controls
        window = nullptr;
        controls.clear();
        cat = nullptr;
        for (Bitmap* b : bitmaps) b->unregisterBitmap();
        bitmaps.clear();
    }

    void MeowEditor::valueChanged(GDIDrawingContext* dc, Controls::Control* control) {
        if (control->parameterId >= 0) plugin->automateHostParameter(control->parameterId, control->getValue());
        control->update(dc);
    }

    void MeowEditor::parameterChanged(int index, float value) {
        for (Controls::Control* c : controls)
            if (c->parameterId == index && c->getValue() != value) c->setValue(value);
    }

    void MeowEditor::onIdle() {
        if (window) {
            if (cat) {
                float v = (float)plugin->catFrame() / 11.0f;
                if (cat->getValue() != v) cat->setValue(v);
            }
            int p = plugin->getActivePresetIndex();
            if (p != program) {  // a program change moves every control
                program = p;
                for (Controls::Control* c : controls)
                    if (c->parameterId >= 0) c->setValue(plugin->getParameterValue(c->parameterId));
            }
        }
        EditorBase::onIdle();
    }

}
