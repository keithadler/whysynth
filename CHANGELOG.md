# Changelog

## 2.0.0 (2026-09-07)

The revival release. Same synth, new plugin formats, new build.

### Added
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
