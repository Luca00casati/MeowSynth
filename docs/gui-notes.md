# The interface

The original's editor is SynthEdit's panel for the `meow` skin: 335 x 228 pixels. Everything
was measured on screenshots of the original under Wine (`tools/snap_editor.cpp`; put the exe next
to the DLL you test, since Windows loads DLLs from the exe's folder first). The recreation's editor
(32 and 64-bit) matches the original pixel for pixel, for programs 0-5 and 9.

## Images

`tools/extract_assets.py MeowSynth.dll` extracts the skin from your copy of the plugin (the images
are not in the repository) and writes, into `assets/interface/`:

| skin image | frames | size | used for |
|---|---|---|---|
| `panels` | 1 | 337 x 228 | background, drawn at x = -2 |
| `knob_med` | 50 | 30 x 30 | the nine knobs |
| `baks` | 12 | 160 x 120 | the singing cat |
| `LFO` | 10 | 50 x 20 | vibrato speed (LFO tempo division) |
| `pitchrange` | 12 | 22 x 20 | pitch-bend range |
| `polycat` | 2 | 122 x 20 | mono / poly (frame 0 = mono) |
| `URL` | 2 | 167 x 30 | the knobster.org link |

DamSDK draws BMPs without alpha, so the script also writes 24-bit BMPs with every control's frames
pre-blended over the panel at the control's position. SynthEdit has two blends:

- Knobs (slider skins), fitted on its pixels: transparent pixels leave the panel alone, others
  give `(src * (a + 1) + dst * (255 - a)) >> 8` (opaque pixels are therefore copied).
- Images (`CMyBmpCtrl2`: the selectors and the link), FUN_1005eb10: in linear light. Tables
  built by FUN_1005ea30: `t[i] = (int)(pow(i * (1/255.f), 2.2f) * 16384 + 0.5)` and the midpoints
  between neighbours. Stored pixels carry transparency (255 - a): 0 copies, 255 skips; otherwise
  `v = t[src] + trunc((t[dst] - t[src]) * u / 16384)` with `u = ((255 - a) * 16384 + 127) / 255`,
  and the result is the largest k whose midpoint is below v (FUN_1005e3f0, a binary search).

## Layout

The SynthEdit sliders draw as `knob_med` knobs at (control left, control top + 16). The value is
first rounded to a whole percent, then to the nearest frame: `round(round(value * 100) * 49 / 100)`
(the frame boundaries, measured by moving a parameter on the original, sit halfway between
hundredths).

| control | position | parameter |
|---|---|---|
| M, O, E, W knobs | (200, 28), (230, 28), (260, 28), (290, 28) | amp ADSR attack, decay, sustain, release |
| vibrato amount, attack | (200, 91), (230, 91) | 20, 21 |
| porta, noise, chorus | (200, 146), (230, 146), (260, 146) | 17, 12, 1 |
| speed | (266, 96) | 23 (frame = list index) |
| pitch-bend range | (294, 151) | 25 (frame = list index); its container ignores program changes, so it keeps the patch's value (11) |
| POLYCAT | (199, 195) | 26 |
| link | (15, 188) | opens http://knobster.org/projects/meowsynth/ |
| cat | (18, 20) | the amplitude envelope |

The filter and chorus controls (cutoff, Q, dry/wet, ...) are not on the original's panel (their
containers don't show controls on the parent); they are host parameters only.

The selectors are SynthEdit images whose animation position the mouse changes; here a click
shows the next entry and a right-click the previous one.

The cat follows the amplitude ADSR of the voice through `KDL Volts2GuiInt` (sampled at 40 Hz):
frame = the envelope in volts (0..10), as an integer (truncated: "Rounding Mode 0"; not yet
compared with the original, which crashes in snap_editor when a note plays with the editor open).

## DamSDK notes

- `Rect` names its fields left, top, right, bottom, but hosts read VST's ERect (top, left, bottom,
  right): set `rect.right` to the height and `rect.bottom` to the width (Delay Lama does the same).
- `getCompanyName` answers effGetEffectName and `getProductName` effGetVendorString.
- The plugin must set `DamSDK::Gui::Platform::Windows::g_hInstance` in its DllMain.
- `Knob` needs a non-null origin point.
