# Quickstart: Bow Model Exciter (Spec 130)

## What This Feature Does

Adds a continuous bow exciter to the Innexus plugin's physical modelling engine. Unlike the transient ImpactExciter, the BowExciter produces sustained tones through stick-slip friction simulation, enabling violin/cello-like sounds with real-time expressive control over pressure, speed, and position.

## Key Files to Touch

### New Files
- `dsp/include/krate/dsp/processors/bow_exciter.h` -- BowExciter class (Layer 2)
- `dsp/tests/unit/processors/bow_exciter_test.cpp` -- Unit tests

### Modified Files (DSP Library)
- `dsp/include/krate/dsp/processors/impact_exciter.h` -- Refactor `process()` to accept `float feedbackVelocity`
- `dsp/include/krate/dsp/processors/residual_synthesizer.h` -- Same refactor
- `dsp/include/krate/dsp/processors/modal_resonator_bank.h` -- Add 8 bowed-mode velocity taps
- `dsp/include/krate/dsp/processors/waveguide_string.h` -- Relocate DC blocker (FR-021)
- `dsp/tests/unit/processors/impact_exciter_test.cpp` -- Update call sites
- `dsp/tests/unit/processors/residual_synthesizer_tests.cpp` -- Update call sites

### Modified Files (Plugin)
- `plugins/innexus/src/plugin_ids.h` -- Add kBowPressureId, kBowSpeedId, kBowPositionId, kBowOversamplingId
- `plugins/innexus/src/processor/innexus_voice.h` -- Add bowExciter field
- `plugins/innexus/src/processor/processor.cpp` -- Add bow exciter dispatch in process loop
- `plugins/innexus/src/processor/processor_midi.cpp` -- Handle bow trigger on note-on
- `plugins/innexus/src/processor/processor_params.cpp` -- Handle bow parameter changes
- `plugins/innexus/src/processor/processor_state.cpp` -- Save/load bow parameters
- `plugins/innexus/src/controller/controller.cpp` -- Register bow parameters

### CMake
- `dsp/tests/unit/CMakeLists.txt` -- Add bow_exciter_test.cpp

## Implementation Order

1. **Unified interface refactor** (FR-015, FR-016) -- change ImpactExciter + ResidualSynthesizer signatures, update all call sites and tests. Build + test before proceeding.

2. **BowExciter core DSP** (FR-001 through FR-010) -- implement the class with all friction, jitter, energy control. Write comprehensive unit tests.

3. **WaveguideString DC blocker relocation** (FR-021) -- move DC blocker insertion point. Verify existing waveguide tests still pass.

4. **ModalResonatorBank bowed-mode taps** (FR-020, FR-024) -- add 8 bandpass filters and harmonic weighting. Write tests for modal-bow coupling.

5. **Plugin integration** (FR-011 through FR-014, FR-025) -- add parameters, wire into InnexusVoice, update processor loop.

6. **Oversampling path** (FR-022, FR-023) -- implement switchable 2x oversampling for the friction junction.

## Critical Gotchas

1. **ImpactExciter refactor breaks 13+ test call sites** -- search-and-replace `.process()` to `.process(0.0f)` in test files BEFORE building.

2. **WaveguideString DC blocker relocation** -- existing waveguide tests may change output values slightly. Use `Approx().margin()` comparisons.

3. **ModalResonatorBank is 3264 bytes** -- adding 8 biquad states (~8 x 28 bytes = 224 bytes) is fine within budget.

4. **LFO is 208 bytes per voice** -- acceptable but not trivial for 16 voices. If memory is a concern, the 0.7 Hz oscillator could be replaced with a Gordon-Smith phasor (8 bytes).

5. **Energy control requires resonator energy** -- BowExciter needs `setResonatorEnergy()` called each sample from the voice engine, using `resonator->getControlEnergy()`.

6. **ADSR drives acceleration, not velocity** -- the envelope output must be integrated (accumulated) to get velocity. Common mistake: using ADSR output as velocity directly.
