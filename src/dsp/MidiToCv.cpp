// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Luca Casati
#include "MidiToCv.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace meow {

    // ---- Smoother ------------------------------------------------------------------------

    void Smoother::set(float newTarget, int length) {  // FUN_100636a0
        if (newTarget == target && done)
            return;
        Mode m = mode;
        done = false;
        if (length < 1) m = kStep;
        if (m == kStep) length = 1;
        if (m == kCubic) {  // FUN_10063440
            double n = (float)length;
            double delta = (double)newTarget - current;
            counter = length - 1;
            target = newTarget;
            double c = 1.0 / (n * n * n);
            double a = (n * 3.0 - 2.0) * c * delta;
            double b = (n - 2.0) * c * delta * 6.0;
            double k = c * delta * -12.0;
            d3 = (float)k;
            value = (float)(a + current);
            d1 = (float)(b + a);
            d2 = (float)(k + b);
            return;
        }
        // step / linear
        value = current;
        d2 = 0.0f;
        d1 = (float)(((double)newTarget - current) / (float)length);
        d3 = 0.0f;
        counter = length - 1;
        target = newTarget;
        value = (float)((double)d1 + value);
    }

    void Smoother::process(float* out, int count) {  // FUN_100635f0 + FUN_10063510
        if (done) {
            // The original writes the constant for one more block, then sleeps; the buffer keeps it.
            for (int i = 0; i < count; i++) out[i] = current;
            return;
        }
        for (int i = 0; i < count; i++) {
            out[i] = value;
            if (--counter < 0) {
                value = current = target;
                for (int k = i; k < count; k++) out[k] = target;
                done = true;
                return;
            }
            value = (float)((double)value + d1);
            d1 = (float)((double)d2 + d1);
            d2 = (float)((double)d3 + d2);
        }
        current = value;
    }

    // ---- MidiToCv ------------------------------------------------------------------------

    MidiToCv::MidiToCv() {
        std::memset(order, -1, sizeof(order));
        velocity.mode = Smoother::kCubic;
    }

    static float pow2Time(double x) { return (float)std::pow(2.0, x); }

    void MidiToCv::midi(int time, unsigned char status, unsigned char d1, unsigned char d2) {
        int type = status & 0xf0;
        Event e{ clock + time, -1, 0, 0 };
        e.a = (float)status; e.b = (float)(d1 | (d2 << 8));
        e.type = 100 + type;  // raw MIDI, handled when reached
        events.push_back(e);
    }

    void MidiToCv::handleMidi(unsigned char status, unsigned char d1, unsigned char d2) {
        int type = status & 0xf0, channel = status & 0x0f;
        if (type == 0x90 && d2 == 0) type = 0x80;
        if (type == 0x90) {
            masterNoteOn(channel, d1, d2);
        } else if (type == 0x80) {
            masterNoteOff(channel, d1);
        } else if (type == 0xe0) {
            int v = (d2 - 0x40) * 0x80 + d1;
            channelBend[channel] = (float)v * 0.0001220703125f;
            voiceBend(channelBend[channel]);
        }
    }

    void MidiToCv::masterNoteOn(int channel, int note, int vel) {  // 0x100a21d0
        heldVelocity[note] = (unsigned char)vel;
        held[note] = true;
        heldChannel[note] = (unsigned char)channel;
        orderIndex = (orderIndex + 1) & 0xf;
        order[orderIndex] = (signed char)note;
        if (mono && priority > 0) {
            if (priority == 1) {
                for (int k = 0; k < note; k++) if (held[k]) return;
            } else if (priority == 2) {
                for (int k = 0x7f; k > note; k--) if (held[k]) return;
            }
        }
        glideTarget = (float)((double)(note - 0x45) * 0.008333333767950535f + 0.5f);
        if (currentNote < 0) {
            glidePos = glideTarget;
            glideSlope = 0.0f;
        } else if (portamentoTime != 0.0f) {
            double t = std::pow(2.0, (((double)portamentoTime * 10.0f - 2.0f) * 12000.0f * 0.1f - 8000.0f) * 0.0008333333333333334);
            glideSlope = (float)(((double)glideTarget - glidePos) / (t * sampleRate));
        } else {
            glidePos = glideTarget;
            glideSlope = 0.0f;
        }
        currentNote = note;
        voiceNoteOn(channel, note, vel);
        voiceGlide(glidePos, glideTarget);
    }

    void MidiToCv::masterNoteOff(int channel, int note) {
        if (currentNote == note) currentNote = -1;
        held[note] = false;
        if (mono && priority > 0) {
            int found = -1;
            if (priority == 1) {
                for (int k = 0; k < 0x80; k++) if (held[k]) { if (note < k) found = k; break; }
            } else if (priority == 2) {
                for (int k = 0x7f; k >= 0; k--) if (held[k]) { if (k < note) found = k; break; }
            } else if (priority == 3 && order[orderIndex] == note) {
                for (;;) {
                    if (--orderIndex < 0) orderIndex = 0xf;
                    signed char c = order[orderIndex];
                    order[orderIndex] = -1;
                    if (c < 0) break;
                    if (held[(int)c]) { found = c; orderIndex--; break; }
                }
            }
            if (found >= 0) {
                currentNote = note;
                masterNoteOn(heldChannel[found], found, heldVelocity[found]);
                return;
            }
        }
        (void)channel;
        // Note off to the voice (0x100a20f0): gate low now, and the pending gate level becomes 0;
        // a gate-on scheduled by an earlier note-on will therefore fire with level 0.
        gateLevel = 0.0f;
        gate.set(0.0f, gate.defaultLength);
        events.push_back(Event{ clock + 3, kEvGateOn, 0, 0 });
    }

    void MidiToCv::voiceNoteOn(int channel, int note, int vel) {  // 0x100a1fd0
        (void)note;
        bendCur = bendTarget = (float)((double)bendRange * channelBend[channel] * 0.008333333333333333);
        bendSlope = 0.0f;
        if (retrigger || !mono)
            gate.set(0.0f, gate.defaultLength);
        float v = (float)vel * 0.007874015718698502f;
        int n = mono ? (int)((std::fabs((double)v - velocity.current) + 1.0) * 60.0) : 0;
        velocity.set(v, n);
        gateLevel = 1.0f;
        events.push_back(Event{ clock + 3, kEvGateOn, 0, 0 });
    }

    void MidiToCv::voiceGlide(float from, float to) {  // 0x100a1b60
        portTarget = to;
        portCur = from;
        double t = std::pow(2.0, (((double)portamentoTime * 10.0f - 2.0f) * 12000.0f * 0.1f - 8000.0f) * 0.0008333333333333334);
        portSlope = (float)(((double)portTarget - portCur) / ((double)sampleRate * t));
    }

    void MidiToCv::voiceBend(float value) {  // 0x100a1ef0
        bendTarget = (float)((double)bendRange * value * 0.008333333333333333);
        bendSlope = (float)(((double)bendTarget - bendCur) / ((double)sampleRate * 0.01666666753590107f));
    }

    void MidiToCv::advanceMaster(int count) {  // top of 0x100a1750
        if (glideSlope != 0.0f) {
            if (((double)glideTarget - glidePos) / glideSlope <= (double)(float)count) {
                glidePos = glideTarget;
                glideSlope = 0.0f;
            } else {
                glidePos = (float)((double)(float)count * glideSlope + glidePos);
            }
        }
    }

    void MidiToCv::render(float* const* out, int offset, int count) {  // 0x100a1750
        advanceMaster(count);
        float* pitch = out[kPitch] + offset;
        if (bendSlope == 0.0f && portSlope == 0.0f) {
            float v = bendCur + portCur;
            for (int i = 0; i < count; i++) pitch[i] = v;
        } else {
            int remaining = count;
            while (remaining > 0) {
                bool portDone = false, bendDone = false;
                int n = remaining;
                if (portSlope != 0.0f) {
                    double s = ((double)portTarget - portCur) / portSlope + 0.5;
                    if (s < (double)(float)remaining) { n = std::max(0, (int)s); portDone = true; }
                }
                int m = n;
                if (bendSlope != 0.0f) {
                    double s = ((double)bendTarget - bendCur) / bendSlope + 0.5;
                    if (s < (double)(float)n) { m = std::max(0, (int)s); bendDone = true; }
                }
                for (int i = 0; i < m; i++) {
                    // The sums are stored, but the output adds the unrounded register values.
                    double b = (double)bendSlope + bendCur;
                    double p = (double)portSlope + portCur;
                    bendCur = (float)b;
                    portCur = (float)p;
                    *pitch++ = (float)(p + b);
                }
                remaining -= m;
                if (portDone) { portCur = portTarget; portSlope = 0.0f; }
                if (bendDone) { bendCur = bendTarget; bendSlope = 0.0f; }
            }
        }
        gate.process(out[kGate] + offset, count);
        velocity.process(out[kVelocity] + offset, count);
        aftertouch.process(out[kAftertouch] + offset, count);
    }

    void MidiToCv::process(float* const* out, int offset, int count) {
        const long end = clock + count;
        while (clock < end) {
            // next event time
            long next = end;
            for (auto& e : events) if (e.time < next) next = std::max(e.time, clock);
            if (next > clock) {
                render(out, offset, (int)(next - clock));
                offset += (int)(next - clock);
                clock = next;
                if (clock >= end) break;
            }
            // handle all events due now, in arrival order
            for (size_t i = 0; i < events.size();) {
                if (events[i].time <= clock) {
                    Event e = events[i];
                    events.erase(events.begin() + i);
                    if (e.type >= 100) {
                        int b = (int)e.b;
                        handleMidi((unsigned char)e.a, (unsigned char)(b & 0xff), (unsigned char)(b >> 8));
                    } else if (e.type == kEvGateOn) {
                        gate.set(gateLevel, gate.defaultLength);
                    }
                    i = 0;
                } else {
                    i++;
                }
            }
        }
    }

}
