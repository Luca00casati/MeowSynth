# DSP notes

Reverse engineering notes on the SynthEdit 1.x modules MeowSynth uses. Addresses are in the original
`MeowSynth.dll` (image base 0x10000000).

## Module classes (`ug_*`)

All modules derive from `ug_base`. Their vtables share a layout; the slots that matter:

| Slot | Offset | Meaning |
|---|---|---|
| 0 | +0x00 | destructor |
| 5 | +0x14 | clone / create |
| 11 | +0x2c | input changed (re-selects the process function) |
| 12 | +0x30 | pin list (`ListInterface`), see `tools/se_pins.json` |
| 13 | +0x34 | open / initialise |

| Module | Class | Changed | Pins | Open |
|---|---|---|---|---|
| Oscillator | `ug_oscillator2` | 0x100aed60 | 0x100af6a0 | 0x100b09d0 |
| ADSR | `ug_adsr` | 0x10080a80 | 0x1006e9f0 | 0x1006e940 |
| VCA | `ug_vca` | 0x100c2b00 | 0x100c2ee0 | 0x100c36b0 |
| X-Mix | `ug_cross_fade` | 0x10077070 | 0x10077160 | 0x10077000 |
| Level Adj | `ug_multiplier` | 0x10097290 | 0x100a6140 | – |
| Peak Follower | `ug_peak_det` | 0x100b7100 | 0x100b7230 | 0x100b6d50 |
| Delay2 | `ug_delay2` | 0x10079ab0 | 0x1007a970 | 0x10079c20 |
| Midi to CV | `ug_midi_to_cv` | 0x100a1cc0 | 0x100a25b0 | 0x100a16e0 |
| Divide / Multiply | `ug_math_div` / `ug_math_mult` | 0x10097290 | 0x10097670 / 0x10097fe0 | – |
| Switch (Many->1) | `ug_switch` | 0x100bfe10 | 0x100bff00 | 0x100bfd40 |
| Fixed Values | `ug_fixed_values` | – | 0x1008b1c0 | 0x1008b1a0 |

`FUN_1006fd10(n)` returns pin `n`; a pin's `+0x44` is its state (2 = streaming, i.e. driven by audio,
otherwise static). `FUN_100dcab0` reads a pin's current value. Modules pick a specialised process
function (stored at `this+0x68`) for each combination of streaming and static inputs.

## Oscillator (`ug_oscillator2`)

- Pins: 0 Pitch (1 V/oct, 5 V = A440), 1 Pulse Width, 2 Waveform (Sine, Saw, Ramp, Triangle, Pulse,
  White Noise, Pink Noise), 3 Audio Out, 4 Sync, 5 Phase Mod, 6 Freq Scale, 7 PM Depth, 8 Smooth
  Peaks, 9 Sync X-Fade.
- Phase is a 32-bit fixed-point accumulator. The pitch-to-increment conversion (`FUN_100b0b40`) uses
  the float trick `v * 10 + 1572864.0`; the top bits index an exponential table and the low 23 bits
  interpolate between entries.
- The waveforms are band-limited wavetables, built once and shared by every instance (globals around
  `0x1019a3dc`–`0x1019a438`; built by `FUN_100aeec0` → `FUN_100b1150` → `FUN_100b0d30`):
  - sine at `0x1019a3fc`;
  - saw, ramp and pulse at `0x1019a424`, or `0x1019a410` with Smooth Peaks (Gibbs) on;
  - triangle at `0x1019a438`;
  - silence at `0x1019a3dc`, for when the pitch is above the highest table.
- Each table set is a list of 0x20-byte entries, one per frequency band, each with a
  `[min, max]` phase-increment range at `+0x10`/`+0x14`. `FUN_100aeb60` picks the band for the
  current increment and cross-fades over 20 samples (`this+0xe8 = 0x14`) when it changes.
- Process selection is in `FUN_100ae920`, by waveform (`this+0xa8`) and by which of Pitch,
  Phase Mod and Pulse Width are streaming:
  - White noise is `0x100ac6f0` and pink noise is `0x100ac750`.
  - Sync streaming, or a pending sync, wraps the chosen function in `FUN_100ae720`.
  - `0x100acc80` runs the table cross-fade.

MeowSynth uses waveform 2 (ramp) for the voice, 5 (white noise) for the breath, and 0 (sine) with
Freq Scale 1 V/kHz for the vibrato LFO and the chorus modulators.
