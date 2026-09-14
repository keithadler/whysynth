# Changelog

## Unreleased

### Fixed
- The standalone could stop at startup with "Unable to configure audio: RtApi::getDeviceInfo:
  deviceId argument not found" on Windows (seen with hexter 2.1.0, the same clap-wrapper host).
  RtAudio had answered 0 for the default output, which happens when the default endpoint fails
  its probe or there is no capture device, and clap-wrapper asked for device 0. A patch applied
  to clap-wrapper at build time (`cmake/`) now falls back to the first device with outputs, and
  keeps the window and MIDI up without sound when there is none, so a device can be chosen in
  Audio/MIDI Settings. DirectSound is compiled in beside WASAPI as a second API to choose from.

## 2.1.0 (2026-09-13)

### Added
- A standalone application, `WhySynth.app` (macOS), `WhySynth` (Linux) and `WhySynth.exe`
  (Windows): the CLAP in a window of its own with audio and MIDI I/O, through clap-wrapper
  with RtAudio and RtMidi. It opens on the default output, listens on every MIDI input, and
  has an Audio/MIDI Settings panel. No DAW needed. The same as hexter 2.1.0.

### Changed
- The Windows release is built with MSVC instead of MinGW, because clap-wrapper's Windows
  standalone shell is C++/WinRT. The C runtime is linked statically, so the zip still needs
  nothing installed. MinGW builds keep working (CI checks them) and skip the standalone.
- The engine's mutexes, condition variable and worker thread are behind `y_thread.h`:
  pthreads where they exist, SRWLOCK, CONDITION_VARIABLE and `_beginthreadex` on MSVC, which
  has no pthread.h.

### Fixed
- The release workflow passed hexter's `-DHEXTER_BUILD_DSSI=OFF` instead of
  `-DWHYSYNTH_BUILD_DSSI=OFF`, a leftover from copying it.

## 2.0.1 (2026-09-12)

### Fixed
- Windows: the CLAP, the LV2 plugin and `whysynth-render.exe` in the 2.0.0 zip needed
  `libwinpthread-1.dll`, MinGW's pthreads runtime, which a Windows machine has only when
  MinGW is installed. The plugins failed to load and the tool stopped with
  "libwinpthread-1.dll was not found" (the same defect Reaper10 reported against hexter on theabolton/hexter#18). The MinGW runtime is now linked statically,
  so the zip stands on its own.

## 2.0.0 (2026-09-07)

The revival release. Same synth, new plugin formats, new build.

### Added
- Audio Unit (`WhySynth.component`, macOS) built from the CLAP through
  clap-wrapper, for Logic Pro and GarageBand. Passes `auval`.
- CLAP plugin (`WhySynth.clap`): all 196 parameters plus polyphony, voice
  mode, glide mode and a Program parameter; state; `clap.preset-load` for
  patch files; CLAP and MIDI note dialects; parameter changes from a program
  change reported back to the host.
- LV2 plugin (`whysynth.lv2`): MIDI in, the parameters as control ports,
  factory patches as LV2 presets generated at build time by
  `whysynth-lv2-gen`.
- `whysynth-render`: renders notes or Standard MIDI Files to stereo WAV
  with no host.
- `src/whysynth_engine.h`: a host-independent engine API; `src/whysynth_core.h`:
  the LADSPA-style core underneath it.
- Patch reading from memory and writing to text without the GUI.
- `WHYSYNTH_DEFAULT_BANK` environment variable.
- Tests: engine (patch formats, rendering of every factory patch, voices,
  state, sample rates), CLAP host-side (dlopen), LV2 host-side (lilv).
- GitHub Actions CI on Linux, macOS and Windows with `clap-validator`;
  release builds attached to tags.

### Fixed
- The fourth mode LFO started at a phase past 1.0 when the phase spread
  exceeded 120 degrees and read past the wavetable.
- Oscillator phase wraps subtracted one cycle only, so a note above the
  sample rate read past a wavetable; every wrap is now a full wrap and the
  increment is clamped at Nyquist. The same in PADsynth sample playback.
- PADsynth wrote its fundamental past the table at very low sample rates.
- The global LFO's slope was computed over zero samples right after
  activation, which put infinities into every modulator that used it.
- Instances at different sample rates can now coexist: grain envelopes are
  per instance and PADsynth samples are keyed by sample rate. The DSSI
  plugin refused a second rate.
- If a non-finite sample ever reaches the output, the run loop silences the
  block and resets voices, filters and effects instead of going quiet for
  good.

### Changed
- CMake replaces autotools.
- FFTW replaced by a vendored KISS FFT behind halfcomplex wrappers
  (`src/yfft.c`).
- The sampleset worker thread is woken through a pthread condition variable
  instead of a pipe and `poll()`.
- The engine no longer includes `ladspa.h` or `dssi.h`; the DSSI plugin is
  a thin wrapper in `dssp_synth.c` over `whysynth_core.c`.
- `README.rst` moved to `docs/README-20170701.rst`; `README.md` is new.

## 20170701

See [ChangeLog](ChangeLog) for the history from 2005 to 2017.
