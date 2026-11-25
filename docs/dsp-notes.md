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

### Numerics

The original runs the x87 FPU at its MSVC default of 53-bit precision. Intermediate results therefore
behave like `double`, and are rounded to `float` only when stored. Plain `double` arithmetic with
explicit `(float)` stores reproduces it.

### Wavetables (`FUN_100aeec0(set, kind, smooth)`) — verified against the original

`tools/dump_wavetables.cpp` dumps the original's tables from memory, and `tools/wavetable_check.py`
rebuilds them. All band headers match, and every sample is bit-identical except a ~1e-23 residue at
the zero crossing of 8 saw bands.

- Harmonics (amplitude, number), then all amplitudes are multiplied by `scale * 0.5`:
  - kind 0 (sine, `0x1019a3fc`, smooth=1): (1, 1), scale 1.
  - kind 2 (saw, `0x1019a424` smooth=0, `0x1019a410` smooth=1): (−1/n, n) for n = 1..256, scale 0.64.
  - kind 5 (triangle, `0x1019a438`, smooth=0): odd n = 1..255, amplitude ±1/n² with alternating sign
    starting at +, scale 0.81.
- Bands: start at harmonic index `i = 0`. A band holds harmonics `0..i`. The next band starts at the
  first index whose number is ≥ √2 · number[i], and the last band ends at the last harmonic. Each new
  band is inserted at the front, so band 0 (lowest pitch) has the most harmonics.
- Increment limits: `maxf = 2^31 / number[i] * 2^(-1/6)`, `max = (int)maxf`. The band built after it
  (now index 1) gets `f18 = max` and `min = (int)(maxf * 2^(-1/12))`. Band 0 has `min = f18 = 0`.
- Smooth Peaks (Gibbs) uses a Hamming window:
  `a[k] *= 0.46f * cos((number[k] − 1) * (float)π / (i + 1)) + 0.54f`.
- Samples: there are 514 (512 + 2 guard). `phase = s * ((double)(float)2π * 2^-9)` (a double), and
  `sum = Σ_{k=i..0} sin(phase * number[k]) * a[k]`, summed from the highest harmonic down. Stored as
  float, with |x| < 2.2534666e-31 flushed to 0.

### Pitch → phase increment (`FUN_100b0b40`)

- 1 V/oct (Freq Scale 0):
  - `d = (double)(10.0f * v + 1572864.0f)`; the high dword minus `0x41380000` is the octave `o`, and
    the low dword is the 32-bit fraction `f`.
  - `t = table[f >> 23]`, then `inc = t + (((f & 0x7fffff) * (table[(f >> 23) + 1] - t)) >> 23)`
    (64-bit product).
  - `inc >>= 8 - o` for `o < 9` (0 if the shift exceeds 31); otherwise `inc <<= o - 8` (0xffffffff on
    overflow).
  - The table is a shared resource ("osc pitch lookup") of 514 ints:
    `table[i] = trunc(pow(2, (i * (float)(1/5120) + 0.8f) * 10 - 5) * 440 * 2^32 / fs + 0.5)`.
    It covers one octave, 8 V to 9 V (3520–7040 Hz).
- 1 V/kHz (Freq Scale 1): `hz = clamp(v * 10000, 0, (float)(fs * 0.5))`,
  `inc = trunc(hz * 2^32 / fs + 0.5)`.

### Process loops

- Phase is `uint32`; `index = phase >> 23` (512 entries) and `frac = (float)(1 + (phase & 0x7fffff)/2^23) - 1`.
  `out = (float)(frac * (t[index+1] - t[index]) + t[index])`, evaluated in double precision.
- Static pitch (`0x100ad330`): one increment for the whole block.
- Streaming pitch (`FUN_100ad160`): the wavetable band is chosen once at block start from the first
  sample's pitch. The increment is recomputed for every sample, *after* that sample's output.
- Streaming pitch + phase mod (`FUN_100ad020`): adds `pm = round(pm_in * depth * 2^32)`, applied as a
  delta from the previous sample's value.
- White noise (`0x100ac6f0`): `seed = seed * 0x19660d + 0x3c6ef35f` (a global shared by every noise
  oscillator, starting at 0), `out = asfloat(0x3f800000 | (seed & 0x7fffff)) - 1.5`.

## Engine (SynthEdit runtime)

Observed with `tools/trace_modules.cpp`, which finds module instances in memory by vtable and
records every plug buffer:

- **Blocks:** processing runs in internal blocks of 128 samples (`DAT_10199a68 = 128`), whatever
  the host block size. Output is bit-identical with 128- and 512-sample host blocks.
- **Polyphony:** the patch is 6-voice polyphonic. Everything up to and including the chorus is
  cloned per voice (6 Midi to CV, 15 oscillators, 12 filters, 12 Delay2s, ...). SynthEdit inserts
  an implicit `ug_adder` wherever two wires feed one input.
- **Buffers:** a plug's buffer is at `plug+8`; outputs own it, inputs point at their source.
  Module layout: `+0x34` sample clock, `+0x38` container (its `+0x4c` is the block start),
  `+0x50`/`+0x54` plugs, `+0x68` process function.
- **Events:** `ug_base::DoProcess` (`0x10071680`) runs the process function up to the next event's
  timestamp, then hands the event to vtable slot 4. Blocks are therefore split exactly at events.
- **Static / streaming:** a module picks its process function by which inputs are streaming.
  An output that stops changing is reported static. Observed timing:
  - the VCA switches to its static path on the sample after the volume's last change;
  - the ADSR restarts its segment at the sample of the Overall Level's last change.
- **Rounding mode:** some loops (VCA streaming, the generic table lookup `FUN_100dc760`, ...)
  switch the x87 to round-toward-zero (`fnstcw` + `or 0xc3f`, saved at `0x1019a360`/`0x1019a364`).
  `fesetround(FE_TOWARDZERO)` around the same code reproduces it (`src/dsp/Fpu.h`).
- **FPU precision:** real Windows hosts run the x87 at 53-bit precision. The Wine tools set that
  explicitly, since MinGW's startup code uses 64-bit.

## Ported modules (bit-exact against the trace)

- VCA (`src/dsp/Vca.*`): curve 1 table
  `t[i] = 1 - (1 - e^{-3(1 - i/512)}) * (float)(1/(1 - e^{-3}))` (514 entries). Streaming volume
  uses the chop-mode interpolation `(t[i] + (t[i+1]-t[i])*frac) * signal` with `i = trunc(v*512)`;
  static volume uses the formula directly.
- X-Mix (`src/dsp/CrossFade.*`): law 1 table `sin(i * (float)(pi/1024))`. Position
  `f = clamp(0.5 - mix, 0, 1)`, then `out = A*gain(1-f) + B*gain(f)`. In the streaming loop, gain B
  is stored as float and gain A is used at register precision.
- ADSR (`src/dsp/Adsr.*`):
  - Segments: attack → 1.0 (time pin 1), decay → Sustain (time pin 2, the sustain segment), and
    release → 0 (time pin 4).
  - Rate: `rate = 1/(2^((v*10*12000*0.1 - 8000)/1200) * fs)`. A segment lasts
    `trunc((target - level)/rate)` samples (minimum 1). Output is level × Overall Level.

## Midi to CV (in progress)

- Note-on schedules the gate at `clock + 3` (`FUN_1006fa60(clock+3, 0x100a1c40)`), which is the
  3-sample gate delay seen in the trace. Pitch and velocity change at the event's own sample.
- Pitch: `(note - 69) * (float)(1/120) + 0.5`, i.e. 1 V/octave with 5 V = A440.
- Velocity: `vel * (float)(1/127)`, sent to an output smoother at `+0x2c0` over
  `N = trunc((|new - old| + 1) * 60)` samples (when `+0xb6` is set; otherwise a step).
- Output smoother (`FUN_100636a0` / `FUN_10063440` / `FUN_10063510`): a smoothstep
  `3t^2 - 2t^3` from the current value to the target over N samples, by forward differencing:
  - Setup: `c = 1/N^3`, `val = cur + (3N - 2) c Δ`, `d1 = 6(N - 2) c Δ + (3N - 2) c Δ`,
    `d2 = -12 c Δ + 6(N - 2) c Δ`, `d3 = -12 c Δ`.
  - Each sample (float arithmetic): output `val`, then `val += d1; d1 += d2; d2 += d3`.
  - At the end: snap to the target and report the output static.
- Portamento is event 12 (`FUN_100a1b60`): slope `(target - current)/(fs * pow(...time...))`.

## Still to do (oscillator)
- Sync wrapper `FUN_100ae720` (the voice oscillator's Sync input is the MIDI gate), the band
  cross-fade `0x100acc80`, and the pulse/PW variants (not used by MeowSynth).
- How SynthEdit schedules blocks and pin events. The original has 7 samples of latency from MIDI
  to output.
