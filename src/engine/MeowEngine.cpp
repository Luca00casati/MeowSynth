// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Luca Casati
#include "MeowEngine.h"
#include "dsp/Adsr.h"
#include "dsp/CrossFade.h"
#include "dsp/Delay.h"
#include "dsp/DhFilter.h"
#include "dsp/MidiToCv.h"
#include "dsp/Oscillator.h"
#include "dsp/PeakFollower.h"
#include "dsp/Slider.h"
#include "dsp/Vca.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <cstdio>
#include <cstdlib>

namespace meow {

    namespace {

        const int kBlock = 128;  // SynthEdit's internal block size
        const int kVoices = 6;

        // ---- connections -----------------------------------------------------------------

        struct Event { int time; bool streaming; float value; };

        // A connection: one block of samples plus the source's state changes during the block.
        struct Port {
            float buf[kBlock] = {};
            std::vector<Event> events;
            bool streaming = false, startStreaming = false;  // state now / at the block start
            float value = 0.0f, startValue = 0.0f;

            void begin() {
                events.clear();
                startStreaming = streaming;
                startValue = value;
            }
            void report(int t, bool s, float v) {
                if (s == streaming && (s || v == value)) return;
                streaming = s;
                value = v;
                events.push_back({ t, s, v });
            }
            void setConstant(float v) {
                std::fill(buf, buf + kBlock, v);
                value = startValue = v;
                streaming = startStreaming = false;
            }
        };

        struct PinEvent { int time; int pin; Event ev; };

        // Runs `run(from, to)` over the block, split at the input events, calling `onEvent`
        // at each one (after the samples before it are processed).
        void splitBlock(const std::vector<Port*>& pins, const std::function<void(int, int)>& run,
                        const std::function<void(const PinEvent&)>& onEvent,
                        const std::function<bool(int, const Event&)>& delivered = nullptr) {
            std::vector<PinEvent> all;
            for (int p = 0; p < (int)pins.size(); p++)
                if (pins[p])
                    for (auto& e : pins[p]->events)
                        if (!delivered || delivered(p, e)) all.push_back({ e.time, p, e });
            std::stable_sort(all.begin(), all.end(), [](const PinEvent& a, const PinEvent& b) {
                return a.time != b.time ? a.time < b.time : a.pin < b.pin;
            });
            int t = 0;
            for (auto& e : all) {
                if (e.time > t) { run(t, e.time); t = e.time; }
                onEvent(e);
            }
            if (t < kBlock) run(t, kBlock);
        }

        // Tracks the state of an input while replaying its events.
        struct InputState {
            bool streaming = false;
            float value = 0.0f;
            void start(const Port* p) { streaming = p->startStreaming; value = p->startValue; }
            void apply(const Event& e) { streaming = e.streaming; value = e.value; }
        };

        // ---- nodes ------------------------------------------------------------------------

        struct SliderNode {
            Slider slider;
            Port out;
            std::vector<std::pair<int, float>> pending;  // (time in block, normalized value)

            SliderNode(float lo, float hi, int type) : slider(lo, hi, type) {}

            void block() {
                out.begin();
                std::sort(pending.begin(), pending.end(),
                          [](const std::pair<int, float>& a, const std::pair<int, float>& b) { return a.first < b.first; });
                int t = 0;
                for (auto& p : pending) {
                    if (p.first > t) { run(t, p.first); t = p.first; }
                    if (slider.setNormalized(p.second) && slider.isGliding()) out.report(t, true, 0.0f);
                }
                pending.clear();
                run(t, kBlock);
            }
            void run(int from, int to) {
                int d = slider.process(out.buf + from, to - from);
                if (d >= 0) out.report(from + d, false, slider.target());
            }
        };

        struct MidiNode {
            MidiToCv cv;
            Port gate, pitch, velocity, aftertouch;
            Port* portamento = nullptr;
            std::vector<std::pair<int, unsigned>> pending;  // (time, packed message)

            void block(long clock) {
                gate.begin(); pitch.begin(); velocity.begin(); aftertouch.begin();
                cv.setPortamentoTime(portamento ? portamento->buf[0] : 0.0f);
                for (auto& m : pending)
                    cv.midi(m.first, (unsigned char)(m.second & 0xff), (unsigned char)((m.second >> 8) & 0xff),
                            (unsigned char)((m.second >> 16) & 0xff));
                pending.clear();
                cv.blockStart = (int)clock;
                cv.changes.clear();
                float* outs[4] = { gate.buf, pitch.buf, velocity.buf, aftertouch.buf };
                cv.process(outs, 0, kBlock);
                Port* ports[4] = { &gate, &pitch, &velocity, &aftertouch };
                std::stable_sort(cv.changes.begin(), cv.changes.end(),
                                 [](const MidiToCv::StateChange& a, const MidiToCv::StateChange& b) { return a.time < b.time; });
                for (auto& c : cv.changes) ports[c.output]->report(c.time, c.streaming, c.value);
            }
        };

        struct AdsrNode {
            Adsr adsr;
            Port* in[Adsr::kNumInputs] = {};
            Port out;
            bool opened = false;

            void block() {
                out.begin();
                const float* buf[Adsr::kNumInputs];
                for (int p = 0; p < Adsr::kNumInputs; p++) buf[p] = in[p]->buf;
                adsr.changes.clear();
                if (!opened) {
                    for (int p = 0; p < Adsr::kNumInputs; p++)
                        adsr.setInput(p, in[p]->startValue, p == Adsr::kGate ? false : in[p]->startStreaming, buf, 0);
                    opened = true;
                }
                std::vector<Port*> pins(in, in + Adsr::kNumInputs);
                splitBlock(pins, [&](int a, int b) { adsr.process(buf, out.buf, a, b - a); },
                           [&](const PinEvent& e) {
                               adsr.setInput(e.pin, e.ev.value, e.pin == Adsr::kGate ? false : e.ev.streaming, buf, e.time);
                           });
                for (auto& c : adsr.changes) out.report(c.time, c.streaming, c.value);
            }
        };

        struct VcaNode {
            Vca vca{ Vca::kExponential };
            Port* sig = nullptr;
            Port* vol = nullptr;
            Port out;
            InputState volState, sigState;
            bool opened = false;
            float gain = 0.0f, constant = 0.0f;
            bool asleep = false;

            // Output state (0x100c2b00): static when Volume is static and either the gain is 0 or
            // the signal is static too; then a constant is written once and the module sleeps.
            void change(int t) {
                asleep = false;
                if (!volState.streaming) {
                    gain = vca.staticGain(volState.value);
                    if (gain == 0.0f || !sigState.streaming) {
                        constant = sig->buf[t] * gain;
                        asleep = true;
                        out.report(t, false, constant);
                        return;
                    }
                }
                out.report(t, true, 0.0f);
            }
            void block() {
                out.begin();
                volState.start(vol);
                sigState.start(sig);
                if (!opened) { change(0); opened = true; }
                splitBlock({ sig, vol },
                           [&](int a, int b) {
                               if (asleep) std::fill(out.buf + a, out.buf + b, constant);
                               else if (volState.streaming) vca.processStreaming(sig->buf + a, vol->buf + a, out.buf + a, b - a);
                               else vca.processStaticVolume(sig->buf + a, gain, out.buf + a, b - a);
                           },
                           [&](const PinEvent& e) {
                               (e.pin == 1 ? volState : sigState).apply(e.ev);
                               change(e.time);
                           });
            }
        };

        struct XMixNode {
            CrossFade xf{ CrossFade::kEqualPowerSin };
            Port* a = nullptr;
            Port* b = nullptr;
            Port* mix = nullptr;
            Port out;
            InputState sa, sb, mixState;
            bool opened = false;

            // Output state (0x10077070): streaming if the mix streams, else the "most streaming"
            // of the two inputs; a static output carries the mixed static value.
            void report(int t) {
                bool streaming = mixState.streaming || sa.streaming || sb.streaming;
                float v = 0.0f;
                if (!streaming) {
                    float gA, gB;
                    xf.staticGains(mixState.value, gA, gB);
                    xf.processStatic(&sa.value, &sb.value, gA, gB, &v, 1);
                }
                out.report(t, streaming, v);
            }
            void block() {
                out.begin();
                sa.start(a); sb.start(b); mixState.start(mix);
                if (!opened) { report(0); opened = true; }
                splitBlock({ a, b, mix },
                           [&](int from, int to) {
                               if (mixState.streaming) {
                                   xf.processStreaming(a->buf + from, b->buf + from, mix->buf + from, out.buf + from, to - from);
                               } else {
                                   float gA, gB;
                                   xf.staticGains(mixState.value, gA, gB);
                                   xf.processStatic(a->buf + from, b->buf + from, gA, gB, out.buf + from, to - from);
                               }
                           },
                           [&](const PinEvent& e) {
                               (e.pin == 0 ? sa : e.pin == 1 ? sb : mixState).apply(e.ev);
                               report(e.time);
                           });
            }
        };

        struct OscNode {
            Oscillator osc;
            Port* in[Oscillator::kNumInputs] = {};
            Port out;
            bool opened = false;
            explicit OscNode(Oscillator::Waveform wf, bool khz = false, bool smooth = false) : osc(wf, khz, smooth) {}

            void block() {
                out.begin();
                const float* buf[Oscillator::kNumInputs];
                for (int p = 0; p < Oscillator::kNumInputs; p++) buf[p] = in[p]->buf;
                if (!opened) {
                    for (int p = 0; p < Oscillator::kNumInputs; p++)
                        osc.setInput(p, in[p]->startValue, p == Oscillator::kSync ? false : in[p]->startStreaming, buf, 0);
                    opened = true;
                }
                std::vector<Port*> pins(in, in + Oscillator::kNumInputs);
                splitBlock(pins, [&](int a, int b) { osc.process(buf, out.buf, a, b - a); },
                           [&](const PinEvent& e) {
                               osc.setInput(e.pin, e.ev.value, e.pin == Oscillator::kSync ? false : e.ev.streaming, buf, e.time);
                           });
                out.report(0, true, 0.0f);
            }
        };

        // Implicit adder (several wires into one input): float sum in wire order.
        struct AdderNode {
            std::vector<Port*> in;
            Port out;
            void block() {
                out.begin();
                for (int i = 0; i < kBlock; i++) {
                    float s = 0.0f;
                    for (auto* p : in) s += p->buf[i];
                    out.buf[i] = s;
                }
                std::vector<InputState> st(in.size());
                for (size_t k = 0; k < in.size(); k++) st[k].start(in[k]);
                splitBlock(in, [](int, int) {}, [&](const PinEvent& e) {
                    st[e.pin].apply(e.ev);
                    bool any = false;
                    float sum = 0.0f;
                    for (auto& s : st) { any |= s.streaming; sum += s.value; }
                    out.report(e.time, any, sum);
                });
            }
        };

        // Level Adj (ug_multiplier): float product.
        struct MulNode {
            Port* a = nullptr;
            Port* b = nullptr;
            Port out;
            void block() {
                out.begin();
                for (int i = 0; i < kBlock; i++) out.buf[i] = a->buf[i] * b->buf[i];
                InputState sa, sb;
                sa.start(a);
                sb.start(b);
                splitBlock({ a, b }, [](int, int) {}, [&](const PinEvent& e) {
                    (e.pin == 0 ? sa : sb).apply(e.ev);
                    out.report(e.time, sa.streaming || sb.streaming, sa.value * sb.value);
                });
            }
        };

        struct PeakNode {
            PeakFollower pf;
            Port* in = nullptr;
            Port* attack = nullptr;
            Port* decay = nullptr;
            Port out;
            bool opened = false;
            void block() {
                out.begin();
                if (!opened) { pf.setAttack(attack->startValue); pf.setDecay(decay->startValue); opened = true; }
                splitBlock({ in, attack, decay }, [&](int a, int b) { pf.process(in->buf + a, out.buf + a, b - a); },
                           [&](const PinEvent& e) {
                               if (e.pin == 1) pf.setAttack(e.ev.streaming ? attack->buf[e.time] : e.ev.value);
                               if (e.pin == 2) pf.setDecay(e.ev.streaming ? decay->buf[e.time] : e.ev.value);
                           });
                out.report(0, true, 0.0f);
            }
        };

        struct FilterNode {
            DhFilter flt;
            int pendingType = -1;  // Filter Type list change at the block start
            Port* in = nullptr;
            Port* freq = nullptr;
            Port* res = nullptr;
            Port out;
            bool opened = false;

            bool audioStreamingAt(int t) const {
                bool s = in->startStreaming;
                for (auto& x : in->events) if (x.time <= t) s = x.streaming;
                return s;
            }
            void notify(int pin, int t) {
                bool audio = audioStreamingAt(t);
                flt.inputStateChanged(pin, audio);
                // Output state (0x10002640): max(ONE_OFF, audio input state).
                out.report(t, audio, 0.0f);
            }
            void block() {
                out.begin();
                if (!opened) {
                    opened = true;
                    out.report(0, in->startStreaming, 0.0f);
                }
                if (pendingType >= 0) {
                    flt.setType((DhFilter::Type)pendingType);
                    notify(3, 0);
                    pendingType = -1;
                }
                splitBlock({ in, freq, res },
                           [&](int a, int b) { flt.process(in->buf + a, freq->buf + a, res->buf + a, out.buf + a, b - a); },
                           [&](const PinEvent& e) { notify(e.pin, e.time); },
                           // The SEM hears every state change of the audio input, but of Freq and
                           // Q|Res only their settling (becoming static); changes it does not hear
                           // do not split its process calls.
                           [](int pin, const Event& e) { return pin == 0 || !e.streaming; });
            }
        };

        struct DelayNode {
            Delay2 d;
            int size = 1;
            Port* in = nullptr;
            Port* mod = nullptr;
            Port out;
            InputState inState;
            bool opened = false, asleep = false;
            int countdown = 0;  // +0x80

            // With a static input (0x10078ad0) the delay keeps running until its output has been
            // 0 for a whole buffer length (counted per process call), then sleeps and reports a
            // static output.
            void block() {
                out.begin();
                inState.start(in);
                if (!opened) {
                    opened = true;
                    if (!inState.streaming) countdown = size;
                }
                splitBlock({ in, mod },
                           [&](int a, int b) {
                               if (asleep) return;  // the buffer keeps its last contents
                               d.process(in->buf + a, mod->buf + a, out.buf + a, b - a);
                               if (inState.streaming) return;
                               bool silent = true;
                               for (int i = a; i < b; i++) silent &= out.buf[i] == 0.0f;
                               if (!silent) { countdown = size; return; }
                               countdown -= b - a;
                               if (countdown < 1) { asleep = true; out.report(b, false, out.buf[b - 1]); }
                           },
                           [&](const PinEvent& e) {
                               if (e.pin != 0) return;
                               inState.apply(e.ev);
                               countdown = size;
                               if (e.ev.streaming && asleep) { asleep = false; out.report(e.time, true, 0.0f); }
                           });
                if (!opened || (!asleep && out.events.empty() && !out.streaming)) out.report(0, true, 0.0f);
            }
        };

        // ---- the patch ----------------------------------------------------------------------

        struct ParamInfo { const char* name; float lo, hi; int sliderType; double presets[6]; };

        // Sliders (volts). Preset values: the patch's Ctl_Slider stored values (doubles), exactly.
        const ParamInfo kSliders[] = {
            { "Attack", 0, 10, 1, { 4.799991250038147, 4.799991250038147, 4.799991250038147, 4.799991250038147, 4.799991250038147, 4.799991250038147 } },
            { "Chorus", -5, 5, 3, { -5.0, 2.2499966621398926, -4.750000014901161, 4.749988913536072, 0.3499937057495117, 4.749988913536072 } },
            { "Cutoff", 0, 10, 1, { 4.299981594085693, 4.299981594085693, 4.299981594085693, 4.299981594085693, 4.299981594085693, 4.299981594085693 } },
            { "Decay", 0, 10, 1, { 10.0, 10.0, 10.0, 10.0, 10.0, 10.0 } },
            { "Dry/Wet", -5, 5, 2, { 5.0, 5.0, 5.0, 5.0, 5.0, 5.0 } },
            { "E", 0, 10, 3, { 2.0999957621097565, 3.3499976992607117, 2.5499963760375977, 4.049992263317108, 3.9999964833259583, 4.749999046325684 } },
            { "EnvMod", 0, 10, 1, { 10.0, 10.0, 10.0, 10.0, 10.0, 10.0 } },
            { "Gain", 0, 10, 1, { 10.0, 10.0, 10.0, 10.0, 10.0, 10.0 } },
            { "M", 0, 10, 3, { 2.8999552130699158, 3.3999568223953247, 3.3499538898468018, 2.399955242872238, 2.299955189228058, 3.6999574303627014 } },
            { "Noise", -5, -4, 3, { -5.0, -4.0, -4.7300001084804535, -4.0, -4.950000509619713, -4.0 } },
            { "O", 0, 10, 3, { 7.199991941452026, 7.549995183944702, 7.5999873876571655, 6.949987411499023, 6.799993515014648, 10.0 } },
            { "Portamento time", 4, 10, 3, { 5.559992253780365, 5.559992253780365, 5.559992253780365, 5.559992253780365, 5.559992253780365, 5.559992253780365 } },
            { "Q", 0, 10, 1, { 7.799978256225586, 7.799978256225586, 7.799978256225586, 7.799978256225586, 7.799978256225586, 7.799978256225586 } },
            { "Vibrato Amount", 0, 10, 3, { 0.0, 10.0, 7.0999884605407715, 1.4999893307685852, 1.0499896854162216, 10.0 } },
            { "Vibrato Attack", 0, 10, 3, { 10.0, 9.049985408782959, 6.599993705749512, 10.0, 10.0, 10.0 } },
            { "W", 0, 10, 3, { 4.316806793212891, 4.416800141334534, 4.216803014278412, 3.8668060302734375, 2.966805398464203, 5.466809272766113 } },
        };
        enum SliderId { sAttack, sChorus, sCutoff, sDecay, sDryWet, sE, sEnvMod, sGain, sM, sNoise, sO,
                        sPortamento, sQ, sVibAmount, sVibAttack, sW, kNumSliders };

        int sliderForParam(int p) {
            switch (p) {
                case MeowEngine::kAttack: return sAttack;
                case MeowEngine::kChorus: return sChorus;
                case MeowEngine::kCutoff: return sCutoff;
                case MeowEngine::kDecay: return sDecay;
                case MeowEngine::kDryWet: return sDryWet;
                case MeowEngine::kE: return sE;
                case MeowEngine::kEnvMod: return sEnvMod;
                case MeowEngine::kGain: return sGain;
                case MeowEngine::kM: return sM;
                case MeowEngine::kNoise: return sNoise;
                case MeowEngine::kO: return sO;
                case MeowEngine::kPortamento: return sPortamento;
                case MeowEngine::kQ: return sQ;
                case MeowEngine::kVibratoAmount: return sVibAmount;
                case MeowEngine::kVibratoAttack: return sVibAttack;
                case MeowEngine::kW: return sW;
                default: return -1;
            }
        }

        // Patch-memory values per preset (volts).
        const float kPresetRate[6] = { -9.069624900817871f, 0, 0, 0, 0, 0 };
        const float kPresetDepth[6] = { 4.1913251876831055f, 0, 0, 0, 0, 0 };
        const float kPresetMin[6] = { 1.149999976158142f, 0, 0, 0, 0, 0 };
        const float kPresetPhase[6] = { 0.3254300057888031f, 0, 0, 0, 0, 0 };
        const int kDivisionIndex[6] = { 7, 0, 0, 9, 9, 0 };
        const int kPresetBend[6] = { 11, 1, 1, 0, 11, 0 };
        const int kPresetMono[6] = { 1, 1, 1, 0, 0, 0 };
        // LFO division list (index -> Fixed Values volts): 4b, 2b, 1b, 2/1, 1/1, 1/2, 1/4, 1/8, 1/12, 1/16
        const float kDivisions[10] = { 16, 8, 4, 2, 1, 0.5f, 0.25f, 0.125f, 0.08333f, 0.0625f };

        struct Voice {
            MidiNode midi;
            OscNode noise{ Oscillator::kWhiteNoise };
            AdsrNode envNoise, envVib, envAmp;
            VcaNode vcaNoise, vcaVib, vcaAmp, vcaGainL, vcaGainR;
            AdderNode pitchSum, freqSum, peakIn;
            OscNode osc{ Oscillator::kRamp };
            XMixNode noiseMix, wetL, wetR, chorusL, chorusR;
            PeakNode peak;
            MulNode envMod;
            FilterNode filterL, filterR;
            DelayNode delayL, delayR;
            Port k0, k05, k07, k04, k1;  // constants
        };

    }

    // -------------------------------------------------------------------------------------

    struct MeowEngine::Impl {
        float fs = 44100.0f;
        double tempo = 120.0;
        int program = 0;
        float params[kNumParams] = {};
        long clock = 0;
        bool started = false;

        std::vector<std::unique_ptr<SliderNode>> sliders;
        Port lfoPitch, chorusPitch, pm0, pm1, pmDepth, half, depth, min, zeroPort, pw;
        OscNode lfo{ Oscillator::kSine, true, true };
        OscNode chorusOsc1{ Oscillator::kSine, false, true }, chorusOsc2{ Oscillator::kSine, false, true };
        AdderNode chorusOff1, chorusOff2, chorusMod1, chorusMod2;
        MulNode chorusDepth1, chorusDepth2;
        Voice voices[kVoices];
        int suspendCountdown[kVoices];   // blocks until a quiet voice is suspended (-1: not quiet)
        bool suspended[kVoices] = {};

        struct PendingMidi { int offset; unsigned char s, d1, d2; };
        std::vector<PendingMidi> midiQueue;
        std::vector<std::pair<int, float>> paramQueue;

        Impl() {
            for (auto& s : kSliders) sliders.emplace_back(new SliderNode(s.lo, s.hi, s.sliderType));
            buildGlobal();
            for (int i = 0; i < kVoices; i++) { build(voices[i]); suspendCountdown[i] = -1; }
        }

        float lfoPitchValue() const {
            // Divide(Tempo Out, Multiply(6000, division / 10)), see docs/dsp-notes.md
            float tempoOut = (float)(tempo / 10.0);
            float division = (float)((double)kDivisions[kDivisionIndex[program]] * 0.10000000149011612);
            float mult = (float)((double)6000.0f * division * 10.0f);
            return (float)(((double)tempoOut / mult) * 0.10000000149011612);
        }

        void build(Voice& v) {
            Port* S[kNumSliders];
            for (int i = 0; i < kNumSliders; i++) S[i] = &sliders[i]->out;
            v.k0.setConstant(0.0f);
            v.k05.setConstant(0.5f);
            v.k07.setConstant(0.7f);
            v.k04.setConstant(0.4f);
            v.k1.setConstant(1.0f);

            v.midi.portamento = S[sPortamento];
            // noise oscillator and its envelope (static times)
            v.noise.in[Oscillator::kPitch] = &v.midi.pitch;
            v.noise.in[Oscillator::kPulseWidth] = &pw;
            v.noise.in[Oscillator::kSync] = &zeroPort;
            v.noise.in[Oscillator::kPhaseMod] = &zeroPort;
            v.noise.in[Oscillator::kPmDepth] = &pmDepth;
            Port* en[] = { &v.midi.gate, &v.k0, &v.k05, &v.k07, &v.k04, &v.k1 };
            std::copy(en, en + 6, v.envNoise.in);
            v.vcaNoise.sig = &v.noise.out;
            v.vcaNoise.vol = &v.envNoise.out;
            // vibrato: LFO * envelope (attack and level from sliders), added to the pitch
            Port* ev[] = { &v.midi.gate, S[sVibAttack], &v.k05, &v.k07, &v.k04, S[sVibAmount] };
            std::copy(ev, ev + 6, v.envVib.in);
            v.vcaVib.sig = &lfo.out;
            v.vcaVib.vol = &v.envVib.out;
            v.pitchSum.in = { &v.midi.pitch, &v.vcaVib.out };
            // voice oscillator (ramp), synced to the gate
            v.osc.in[Oscillator::kPitch] = &v.pitchSum.out;
            v.osc.in[Oscillator::kPulseWidth] = &pw;
            v.osc.in[Oscillator::kSync] = &v.midi.gate;
            v.osc.in[Oscillator::kPhaseMod] = &zeroPort;
            v.osc.in[Oscillator::kPmDepth] = &pmDepth;
            Port* ea[] = { &v.midi.gate, S[sM], S[sO], S[sE], S[sW], &v.midi.velocity };
            std::copy(ea, ea + 6, v.envAmp.in);
            v.vcaAmp.sig = &v.osc.out;
            v.vcaAmp.vol = &v.envAmp.out;
            // noise mix (A = noise, B = voice)
            v.noiseMix.a = &v.vcaNoise.out;
            v.noiseMix.b = &v.vcaAmp.out;
            v.noiseMix.mix = S[sNoise];
            // filter section
            // Both of the filter container's inputs (left, right: the same signal) are wired to the
            // Peak Follower, so SynthEdit's implicit adder feeds it twice the signal.
            v.peakIn.in = { &v.noiseMix.out, &v.noiseMix.out };
            v.peak.in = &v.peakIn.out;
            v.peak.attack = S[sAttack];
            v.peak.decay = S[sDecay];
            v.envMod.a = &v.peak.out;
            v.envMod.b = S[sEnvMod];
            v.freqSum.in = { S[sCutoff], &v.envMod.out };
            for (FilterNode* f : { &v.filterL, &v.filterR }) {
                f->in = &v.noiseMix.out;
                f->freq = &v.freqSum.out;
                f->res = S[sQ];
                f->flt.setSampleRate(fs);
            }
            v.wetL.a = &v.filterL.out; v.wetL.b = &v.noiseMix.out; v.wetL.mix = S[sDryWet];
            v.wetR.a = &v.filterR.out; v.wetR.b = &v.noiseMix.out; v.wetR.mix = S[sDryWet];
            v.vcaGainL.sig = &v.wetL.out; v.vcaGainL.vol = S[sGain];
            v.vcaGainR.sig = &v.wetR.out; v.vcaGainR.vol = S[sGain];
            v.delayL.in = &v.vcaGainL.out; v.delayL.mod = &chorusMod1.out;
            v.delayR.in = &v.vcaGainR.out; v.delayR.mod = &chorusMod2.out;
            v.delayL.d.setup(fs, 0.06f);
            v.delayR.d.setup(fs, 0.06f);
            v.delayL.size = v.delayR.size = (int)((double)fs * 0.06f);
            v.chorusL.a = &v.delayL.out; v.chorusL.b = &v.vcaGainL.out; v.chorusL.mix = S[sChorus];
            v.chorusR.a = &v.delayR.out; v.chorusR.b = &v.vcaGainR.out; v.chorusR.mix = S[sChorus];
        }

        void buildGlobal() {
            zeroPort.setConstant(0.0f);
            pw.setConstant(0.5f);
            pmDepth.setConstant(0.5f);
            // chorus: global modulators (each voice has its own delays)
            chorusOsc1.in[Oscillator::kPitch] = &chorusPitch;
            chorusOsc2.in[Oscillator::kPitch] = &chorusPitch;
            for (OscNode* o : { &chorusOsc1, &chorusOsc2 }) {
                o->in[Oscillator::kPulseWidth] = &pw;
                o->in[Oscillator::kSync] = &zeroPort;
                o->in[Oscillator::kPmDepth] = &pmDepth;
            }
            chorusOsc1.in[Oscillator::kPhaseMod] = &pm0;
            chorusOsc2.in[Oscillator::kPhaseMod] = &pm1;
            chorusOff1.in = { &chorusOsc1.out, &half };
            chorusOff2.in = { &chorusOsc2.out, &half };
            chorusDepth1.a = &chorusOff1.out; chorusDepth1.b = &depth;
            chorusDepth2.a = &chorusOff2.out; chorusDepth2.b = &depth;
            chorusMod1.in = { &chorusDepth1.out, &min };
            chorusMod2.in = { &chorusDepth2.out, &min };
            // LFO
            lfo.in[Oscillator::kPitch] = &lfoPitch;
            lfo.in[Oscillator::kPulseWidth] = &pw;
            lfo.in[Oscillator::kSync] = &zeroPort;
            lfo.in[Oscillator::kPhaseMod] = &zeroPort;
            lfo.in[Oscillator::kPmDepth] = &pmDepth;
        }

        void runVoice(Voice& v) {
            for (Port* p : { &v.k0, &v.k05, &v.k07, &v.k04, &v.k1 }) p->begin();
            v.midi.block(clock);
            v.noise.block();
            v.envNoise.block(); v.vcaNoise.block();
            v.envVib.block(); v.vcaVib.block(); v.pitchSum.block();
            v.osc.block();
            v.envAmp.block(); v.vcaAmp.block();
            v.noiseMix.block();
            v.peakIn.block(); v.peak.block(); v.envMod.block(); v.freqSum.block();
            v.filterL.block(); v.filterR.block();
            v.wetL.block(); v.wetR.block();
            v.vcaGainL.block(); v.vcaGainR.block();
            v.delayL.block(); v.delayR.block();
            v.chorusL.block(); v.chorusR.block();
        }

        void open() {
            // sliders start at the program's values (a step); patch memory and lists likewise
            for (int i = 0; i < kNumSliders; i++) {
                const ParamInfo& p = kSliders[i];
                float norm = (float)((p.presets[program] - p.lo) / (p.hi - p.lo));
                sliders[i]->slider.setSampleRate(fs);
                sliders[i]->slider.setInitial(p.presets[program]);
                params[i] = norm;
            }
            lfoPitch.setConstant(lfoPitchValue());
            chorusPitch.setConstant((float)(kPresetRate[program] * 0.10000000149011612));
            pm0.setConstant(0.0f);
            pm1.setConstant((float)(kPresetPhase[program] * 0.10000000149011612));
            half.setConstant(0.5f);
            depth.setConstant((float)(kPresetDepth[program] * 0.10000000149011612));
            min.setConstant((float)(kPresetMin[program] * 0.10000000149011612));
            for (auto& v : voices) {
                v.midi.cv.setMonoMode(kPresetMono[program] != 0);
                v.midi.cv.setBendRange(kPresetBend[program]);
            }
            started = true;
        }

        void block(float* l, float* r) {
            // host events
            for (auto& p : paramQueue) {
                int s = sliderForParam(p.first);
                if (s >= 0) sliders[s]->pending.push_back({ 0, p.second });
                if (p.first == MeowEngine::kFilterType) {  // list of 5: LP, HP, BP1, BP2, Notch
                    int type = (int)(p.second * 4.0f + 0.5f);
                    for (auto& v : voices) v.filterL.pendingType = v.filterR.pendingType = std::max(0, std::min(4, type));
                }
            }
            paramQueue.clear();
            // The sliders' MIDI CC assignments (mod wheel on Vibrato Amount, CC5 on Portamento) are
            // not active in the exported plugin: the original ignores those controllers.
            // Mono mode: every note goes to the first voice. TODO: polyphonic voice allocation.
            for (auto& m : midiQueue) {
                voices[0].midi.pending.push_back({ m.offset, (unsigned)m.s | ((unsigned)m.d1 << 8) | ((unsigned)m.d2 << 16) });
                if ((m.s & 0xf0) == 0x90 && m.d2 > 0) { suspended[0] = false; suspendCountdown[0] = -1; }
            }
            midiQueue.clear();

            for (auto& s : sliders) s->block();
            for (Port* p : { &lfoPitch, &chorusPitch, &pm0, &pm1, &half, &depth, &min, &zeroPort, &pw, &pmDepth }) p->begin();
            lfo.block();
            chorusOsc1.block(); chorusOsc2.block();
            chorusOff1.block(); chorusOff2.block();
            chorusDepth1.block(); chorusDepth2.block();
            chorusMod1.block(); chorusMod2.block();

            std::fill(l, l + kBlock, 0.0f);
            std::fill(r, r + kBlock, 0.0f);
            for (int i = 0; i < kVoices; i++) {
                Voice& v = voices[i];
                if (!suspended[i]) runVoice(v);
                for (int k = 0; k < kBlock; k++) { l[k] += v.chorusL.out.buf[k]; r[k] += v.chorusR.out.buf[k]; }
                // A voice whose outputs have gone static is suspended two blocks later (it then
                // stops processing, so its noise oscillator stops drawing from the generator).
                if (!suspended[i]) {
                    bool quiet = !v.chorusL.out.streaming && !v.chorusR.out.streaming;
                    if (!quiet) suspendCountdown[i] = -1;
                    else if (suspendCountdown[i] < 0) suspendCountdown[i] = 2;
                    else if (--suspendCountdown[i] == 0) suspended[i] = true;
                }
            }
            clock += kBlock;
        }
    };

    MeowEngine::MeowEngine() : impl(new Impl) {}
    MeowEngine::~MeowEngine() = default;

    void MeowEngine::setSampleRate(float fs) { impl->fs = fs; }
    void MeowEngine::setTempo(double bpm) { impl->tempo = bpm; }
    void MeowEngine::setProgram(int p) { impl->program = std::max(0, std::min(5, p)); }
    void MeowEngine::setParameter(int i, float v) {
        if (i < 0 || i >= kNumParams) return;
        impl->params[i] = v;
        impl->paramQueue.push_back({ i, v });
    }
    float MeowEngine::getParameter(int i) const { return i >= 0 && i < kNumParams ? impl->params[i] : 0.0f; }
    void MeowEngine::midi(int offset, unsigned char s, unsigned char d1, unsigned char d2) {
        impl->midiQueue.push_back({ offset, s, d1, d2 });
    }

    void MeowEngine::process(float* left, float* right, int count) {
        if (!impl->started) impl->open();
        // MIDI offsets are relative to this call; hand each internal 128-sample block its share.
        std::vector<Impl::PendingMidi> all = std::move(impl->midiQueue);
        impl->midiQueue.clear();
        for (int pos = 0; pos < count; pos += kBlock) {
            for (auto& m : all)
                if (m.offset >= pos && m.offset < pos + kBlock)
                    impl->midiQueue.push_back({ m.offset - pos, m.s, m.d1, m.d2 });
            float l[kBlock], r[kBlock];
            impl->block(l, r);
            int n = std::min(kBlock, count - pos);
            std::memcpy(left + pos, l, sizeof(float) * n);
            std::memcpy(right + pos, r, sizeof(float) * n);
        }
    }
}
