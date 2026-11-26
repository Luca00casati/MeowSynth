# MeowSynth Recreation

An open source recreation of **MeowSynth**, the singing-cat VST instrument from
[knobster.org](http://knobster.org/projects/meowsynth/) (2007). The original is only available as a
32-bit Windows plugin; the goal is a 64-bit version that sounds the same, built on
[DamSDK](https://github.com/Luca00casati/DamSDK), like the [Delay Lama recreation](https://github.com/Luca00casati/DelayLama).

## How the original is built

MeowSynth was made with **SynthEdit 1.x**. The DLL is the generic SynthEdit runtime (MFC 7, libpng,
zlib); the synth itself is a SynthEdit project stored, lightly scrambled, as resource `SE1 #165`.
So instead of decompiling the whole DLL, this project:

1. decodes the embedded project and parses its module graph (`tools/`),
2. reimplements only the modules the graph uses (oscillator, ADSR, VCA, X-Mix, Delay2, peak
   follower, the DH_MultiFilter2 and BPM Clock add-ons, ...),
3. checks the result against the original plugin's audio.

The decoded graph (modules, settings, wiring, presets) is in [docs/patch-graph.txt](docs/patch-graph.txt).

## Building

```sh
git submodule update --init                      # DamSDK
python3 tools/extract_assets.py /path/to/original/MeowSynth.dll   # the interface (optional)
cmake -S . -B build/x86_64 -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-x86_64.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build/x86_64                       # build/x86_64/MeowSynth.dll (64-bit VST 2.x)
cmake -S . -B build/i686 -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build/i686                         # build/i686/MeowSynth.dll (32-bit)
```

The engine and its tests also build natively (`cmake -S . -B build/native && cmake --build
build/native && ctest --test-dir build/native`).

Both plugin builds produce **the same samples as the original, bit for bit**, in every reference
scenario (checked by loading them in the same Wine harness as the original, see below). The
engine also matches the original at every host block size tested (37 to 2048 samples, including
primes; SynthEdit derives its internal block length from the host's, see docs/dsp-notes.md).

## Tools

Needs Python 3 with `pefile`, and a copy of the original `MeowSynth.dll` (not included).

```sh
python3 tools/dump_resources.py MeowSynth.dll extracted/rsrc          # dump PE resources
python3 tools/se1_decode.py extracted/rsrc/SE1/165_0.bin extracted/MeowSynth.se1
python3 tools/se1_dump.py extracted/MeowSynth.se1 > docs/patch-graph.txt
python3 tools/se1_parse.py extracted/MeowSynth.se1 --json extracted/patch.json
```

The interface images and the embedded add-on modules are not in the repository; extract them from
your own copy of the plugin. `tools/ghidra/` has the Ghidra scripts used to find the class layouts.

## Reference audio

`tests/fixtures/*.f32` are renders of the original plugin (interleaved stereo float32, 44.1 kHz) for
the scenarios in `tests/helpers/RenderScenario.h`. Each scenario isolates part of the signal chain
with the plugin's own parameters (filter dry, chorus dry, no noise, ...). The original is fully
deterministic, so the recreation can be compared sample by sample. Its white-noise generator is
shared by the whole DLL, so render each scenario in its own process. To re-record (needs 32-bit Wine):

```sh
i686-w64-mingw32-g++ -O2 -static -I tests/helpers tools/render_reference.cpp -o render_reference.exe
for s in voice legato filter vibrato chorus preset0 preset1 preset2 preset3 preset4 preset5; do
    wine render_reference.exe MeowSynth.dll tests/fixtures $s          # 512-sample host blocks
    wine render_reference.exe MeowSynth.dll tests/fixtures/host37 $s 37
done
i686-w64-mingw32-g++ -O2 -static -I tests/helpers tools/probe_plugin.cpp -o probe_plugin.exe
wine probe_plugin.exe MeowSynth.dll    # parameters, programs, I/O
```

## Roadmap
- [x] Decode and fully parse the embedded SynthEdit project.
- [x] Reverse engineer the DSP of each module the patch uses ([docs/dsp-notes.md](docs/dsp-notes.md)).
- [x] Reference renders from the original plugin (under Wine).
- [x] Audio tests against the references: bit-exact.
- [x] Synth engine on DamSDK, 32 and 64-bit builds.
- [x] Interface (panel, knobs, cat animation) from the original skin, extracted from your own copy
  ([docs/gui-notes.md](docs/gui-notes.md)); without it the plugin builds with the host's generic UI.

## Licensing
Copyright (C) 2026 Luca Casati

This program is free software; you can redistribute it and/or modify it under the terms of the GNU
Lesser General Public License as published by the Free Software Foundation; either version 2.1 of
the License, or (at your option) any later version. It is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See [LICENSE](LICENSE) for the full text.

The reference renders in `tests/fixtures/` are recordings of the original plugin's output, kept
for testing only.

## Legal Notes
This is a reverse engineering project for educational and historical preservation purposes.
MeowSynth is a product of knobster.org; SynthEdit is by Jeff McClintock; DH_MultiFilter2 is by
David Haupt; the KDL modules are by Kelly Lynch. This project is not affiliated with or endorsed by
any of them, and contains none of their binaries or artwork.
