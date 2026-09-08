# WhySynth

**A versatile software synthesizer, by Sean Bolton.** Four oscillators with
eleven modes: minBLEP, wavecycle, chorused wavecycle, asynchronous granular,
three kinds of FM, waveshaper, noise, PADsynth, and phase distortion. Two
filters with ten modes. Three LFOs, five envelopes, a modulation mixer, and
effects. Almost four hundred factory patches, many of them interpretations of
Kawai K4 and Ensoniq SQ-80 sounds. Free software since 2005.

Version 2 is the same synth, revived for 2026: it builds as a **CLAP** and an
**LV2** plugin on Linux, macOS and Windows, with CMake, tests and CI, and a
command line renderer. FFTW, DSSI, liblo, ALSA and GTK are no longer
required. The DSSI plugin and its GTK2 editor still build on Linux when their
libraries are installed.

| | |
|---|---|
| Plugins | CLAP (`WhySynth.clap`), LV2 (`whysynth.lv2`), Audio Unit (`WhySynth.component`, macOS), DSSI (legacy, Linux) |
| Platforms | Linux, macOS (Apple silicon and Intel), Windows |
| Patches | `.WhySynth` text files; the factory bank is built in |
| License | GPL-2.0-or-later; the patches are public domain |

## Download

Builds for every platform are attached to each
[release](https://github.com/keithadler/whysynth/releases). Unzip and copy:

| Platform | CLAP | LV2 | Audio Unit |
|---|---|---|---|
| Linux | `~/.clap/` | `~/.lv2/` | |
| macOS | `~/Library/Audio/Plug-Ins/CLAP/` | `~/Library/Audio/Plug-Ins/LV2/` | `~/Library/Audio/Plug-Ins/Components/` |
| Windows | `%COMMONPROGRAMFILES%\CLAP\` | `%APPDATA%\LV2\` | |

**Logic Pro and GarageBand** use the Audio Unit. After copying it, restart
Logic; it appears under AU Instruments as Keith Adler > WhySynth. It passes
Apple's `auval`, which is the check Logic runs before listing a plugin.

The macOS build is unsigned. If macOS refuses to load it, remove the
quarantine flag once:

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/CLAP/WhySynth.clap
```

## Using it

WhySynth has no window of its own yet. Your host shows its parameters, all
196 of them plus polyphony, voice mode and glide mode, grouped by module
(Osc1, VCF2, EG3 and so on), and you play it over MIDI.

**CLAP.** The *Program* parameter selects a patch from the loaded bank, and
a MIDI program change does the same. Selecting a patch rewrites every
parameter, and the plugin tells the host so its display follows. Use the
host's preset browser to load any `.WhySynth` file as a new bank (the plugin
implements `clap.preset-load`). The whole bank is saved with your session.

**LV2.** The factory patches are shipped as LV2 presets, generated from the
bank at build time, so they appear in your host's preset list. Any patch file
can be turned into presets with `whysynth-lv2-gen presets out.ttl file.WhySynth`.

**Audio Unit.** The AU is the CLAP wrapped by
[clap-wrapper](https://github.com/free-audio/clap-wrapper), so it has the
same parameters and the Program parameter, and Logic's own preset system
saves the whole bank with the project.

**Both.** Set `WHYSYNTH_DEFAULT_BANK` to a patch file to load it on every
instance. Mod wheel, pressure, key and velocity are modulation sources; the
sustain pedal, all-notes-off and all-sound-off are honoured.

The PADsynth and wavetable oscillators render their tables on a background
thread the first time a patch asks for them, so a patch may take a moment
before it sounds. That is how the original worked too.

## The command line renderer

`whysynth-render` plays notes or a whole MIDI file through the engine and
writes a stereo WAV. No host, no audio device.

```bash
whysynth-render --list
whysynth-render --program 12 --note 48 --note 55 --note 60 --out chord.wav
whysynth-render --bank patches/more_K4_interpretations.WhySynth --midi song.mid --out song.wav
```

## Building

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

That produces `build/WhySynth.clap`, `build/lv2/whysynth.lv2/`,
`build/whysynth-render`, and on macOS `build/auv2/WhySynth.component`. The CLAP and LV2 headers are fetched by CMake if not
installed; KISS FFT is vendored. On Linux, installing `dssi-dev liblo-dev
libgtk2.0-dev libasound2-dev` also builds the original DSSI plugin and GTK2
editor. Installing `lilv-dev` (or `brew install lilv`) enables the LV2 host
test.

Options: `-DWHYSYNTH_BUILD_CLAP`, `-DWHYSYNTH_BUILD_LV2`,
`-DWHYSYNTH_BUILD_DSSI`, `-DWHYSYNTH_BUILD_TOOLS`, `-DWHYSYNTH_BUILD_TESTS`
(all on).

## Embedding the engine

`src/whysynth_engine.h` is a small C API with no plugin framework attached:
create an engine at a sample rate, set parameters by port number, feed it
events with frame offsets, render stereo float. Patch loading, the bank,
state save and load, and MIDI parsing are all there. The CLAP plugin and the
renderer are each a few hundred lines on top of it; the LV2 plugin sits one
level down on `whysynth_core.h`, which speaks LADSPA-style ports directly.

## What changed in 2.0

- CLAP and LV2 plugins on three platforms, plus an Audio Unit on macOS for
  Logic and GarageBand, with per-platform CI and a `clap-validator` pass on
  every commit.
- CMake replaces autotools.
- FFTW replaced by a vendored KISS FFT (BSD) behind two small wrappers; the
  engine has no external dependencies beyond pthreads.
- The table-rendering worker thread is woken through a condition variable
  instead of a pipe and `poll()`, which is what made a Windows build possible.
- The engine is separated from the plugin API and covered by tests: patch
  files in every version of the format, rendering of every factory patch,
  voice management, state, and sample rates.
- Patches can be read from memory and written back to text without a GUI,
  which is what session state is built on.
- `WHYSYNTH_DEFAULT_BANK` loads a bank on instantiation.
- Several latent crashes in the oscillators at extreme pitches or sample
  rates are fixed, and instances at different sample rates can coexist.

Not yet: a graphical editor for the CLAP and LV2 plugins. The GTK2 editor
still builds on Linux for DSSI hosts.

## Credits

WhySynth was written by Sean Bolton, drawing on Xsynth-DSSI, hexter, Csound,
Mats Olsson's MSS, Fons Adriaensen's filters, Nasca Octavian Paul's PADsynth
algorithm, Juhana Sadeharju's plate reverb, and wavecycle data resynthesized
from Claude Kaber's Virtual K4 and //christian's SQ-80 work. See
[AUTHORS](AUTHORS). The 2.0 port is by Keith Adler.

The original README, with the voice architecture, the oscillator modes and
the wavetable guide, is kept as
[docs/README-20170701.rst](docs/README-20170701.rst).
