# `membrum-fit`

Offline analysis-by-synthesis fitter for the Membrum drum synth.
Reads WAV drum samples and emits Membrum-format `.vstpreset` files (per-pad
or full v6 kit) plus an optional JSON intermediate, by rendering candidate
voices through the *real* Membrum DSP and minimising a perceptual loss
between the candidate and the input.

See [`specs/membrum-fit-tool.md`](../../specs/membrum-fit-tool.md) for the
full design rationale, references, and phase plan.

## Build

```bash
cmake --preset windows-x64-release
cmake --build build/windows-x64-release --config Release --target membrum_fit
```

External deps fetched at configure time: Eigen 3.4 (MPL-2), CLI11 v2.4.2
(BSD-3), NLopt v2.7.1 (BSD/MIT for the algorithms used). All gated by
`-DVSTWORK_BUILD_MEMBRUM_FIT=OFF` if you want to skip the tool entirely.

NLopt can be disabled separately with `-DMEMBRUM_FIT_ENABLE_NLOPT=OFF`;
the deterministic fit pipeline still works without BOBYQA refinement.

## Usage

```bash
# Fit a single WAV to a per-pad preset
membrum_fit per-pad kick.wav out/kick.vstpreset

# Fit a kit from a JSON map of MIDI note -> WAV path
membrum_fit kit kit.json out/

# Fit from an SFZ kit
membrum_fit kit kit.sfz out/

# Override modal extractor + add global escape (CRS) + emit JSON
membrum_fit per-pad kick.wav out/kick.vstpreset \
    --modal-method esprit --global --json \
    --w-stft 0.6 --w-mfcc 0.2 --w-env 0.2 \
    --max-evals 300
```

### Kit JSON format

```json
{ "36": "kick.wav", "38": "snare.wav", "42": "hat.wav" }
```

Keys are MIDI notes in [36, 67] (GM drum range); values are paths
(absolute or relative to the JSON's parent directory).

## Pipeline

11 stages per spec §3, each a pure-function module under `src/`:

| Stage | Module | Notes |
|---|---|---|
| 1 Loader | `loader.cpp` | dr_wav + linear resample + -1 dBFS normalise |
| 2 Segmentation | `segmentation.cpp` | Spectral flux peak + RMS gate |
| 3 Attack features | `features.cpp` | LAT, flatness, centroid, AR, inharmonicity |
| 4 Exciter classifier | `exciter_classifier.cpp` | Rule tree (Phase-1 3-way + 6-way) |
| 5 Modal extraction | `modal/matrix_pencil.cpp`, `modal/esprit.cpp` | Eigen JacobiSVD + CES |
| 5b Mode-order selection | `modal/mode_selection.cpp` | MDL-style 95% energy cutoff |
| 6 Body classifier | `body_classifier.cpp` | Shift-search log-ratio scoring |
| 7 Mapper inversion | `mapper_inversion/*.cpp` | Per-body f0(size) + brightness |
| 8 Tone shaper fit | `tone_shaper_fit.cpp` | Pitch env + filter ADSR + drive/fold |
| 9 Unnatural fit | `unnatural_fit.cpp` | modeInject + decaySkew |
| 10 Refinement | `refinement/bobyqa_refine.cpp`, `refinement/cmaes_refine.cpp` | NLopt BOBYQA + optional CRS escape |
| 11 Preset writer | `preset_io/*.cpp` | Per-pad, kit (v6), JSON |

## Companion tool

`membrum_fit_gen` renders a JSON `PadConfig` description through the
production `RenderableMembrumVoice` to a WAV file. Used to generate the
golden-test corpus referenced in spec §5.3.

```bash
membrum_fit_gen ground.json kick_gold.wav --sec 1.0 --vel 1.0
```

## Tests

```bash
cmake --build build/windows-x64-release --config Release --target membrum_fit_tests
build/windows-x64-release/bin/Release/membrum_fit_tests.exe
```

Default suite covers: loader (mono/stereo/resample), segmentation, attack
features, Matrix Pencil SNR sweep, body classifier (all 6 bodies),
mapper inversions (FR-055 defaults + 100x size round-trip per body),
SFZ + WAV-dir ingest, BOBYQA, CRS, MSS/MFCC/envelope loss identity,
per-pad blob round-trip, kit + pad JSON, byte-exact kit writer
regression, CLI parser, and the end-to-end CLI pipeline.

The `[corpus]` test (hidden by default) runs a small N-config sweep
that renders random PadConfigs through the production voice, fits them
back, and reports body-class round-trip rate + mean MSS distance.
Run with `membrum_fit_tests "[corpus]"`.

## Known limitations

- Body classifier is best-effort on single-shot real samples; perceptual
  match (MSS) is the operative quality bar, not class accuracy.
- Multi-velocity round-robin fitting is deferred (spec §1.2 / §8 Phase 5).
- Coupling between pads cannot be inferred from isolated samples; the
  kit writer emits `couplingAmount=0.5` and globals=0 (spec §9 risk #7).
- Phase 6 macros are always emitted as 0.5 (neutral; spec §9 risk #8).
- libcmaes (LGPL) global-escape path is gated off; the BSD NLopt CRS
  variant is used instead (spec §6 + §9 risk #9).

## License

The tool itself is under the same license as the parent repository.
External deps:

| Dep | License |
|---|---|
| dr_wav | Public Domain / MIT-0 |
| Eigen 3.4 | MPL-2 |
| CLI11 | BSD-3 |
| NLopt (BOBYQA, CRS, ESPRIT-via-MP) | BSD/MIT subset |
| nlohmann/json | MIT |
| KrateDSP / membrum_dsp / membrum_preset_io | project |
