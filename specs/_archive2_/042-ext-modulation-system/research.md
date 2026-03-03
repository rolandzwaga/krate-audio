# Research: Extended Modulation System

**Feature**: 042-ext-modulation-system | **Date**: 2026-02-08

## R-001: Rungler ModulationSource Adapter Pattern

**Question**: How should the Rungler be adapted to function as a ModulationSource?

**Decision**: Add `public ModulationSource` to the Rungler class declaration. Implement `getCurrentValue()` returning `runglerCV_` (already maintained, range [0, +1]) and `getSourceRange()` returning `{0.0f, 1.0f}`.

**Rationale**: The spec clarification explicitly states the interface must be added directly to Rungler (not a wrapper). The codebase has 6 existing examples of Layer 2 processors inheriting from Layer 0 ModulationSource: ChaosModSource, RandomSource, TransientDetector, SampleHoldSource, PitchFollowerSource, and (in tests) MockModulationSource. All follow the same pattern: override `getCurrentValue()` to return an internal state variable, and `getSourceRange()` to return the output range.

**Alternatives Considered**:
1. **Wrapper class RunglerModSource**: Rejected by spec. Would add heap allocation, indirection, and a separate class.
2. **Manual value extraction**: Pass Rungler output manually to ModulationEngine via setMacroValue(). Rejected because it breaks the ModulationSource abstraction pattern.

**Layer Compliance**: Rungler is Layer 2, ModulationSource is Layer 0. Layer 2 depending on Layer 0 is valid per architecture rules.

**Impact on Existing Code**: Adding a virtual base class to Rungler introduces a vtable pointer. Since Rungler is always heap-allocated (or stack-allocated in tests), this is a negligible 8-byte increase. No existing Rungler callers are affected since the new methods are additions.

---

## R-002: VoiceModSource/VoiceModDest Enum Extension

**Question**: How to safely extend the per-voice modulation enums without breaking existing code?

**Decision**: Append new values before the `NumSources`/`NumDestinations` sentinels.

**Rationale**: All array sizing in VoiceModRouter uses `static_cast<size_t>(VoiceModSource::NumSources)` and `static_cast<size_t>(VoiceModDest::NumDestinations)`. Adding values before these sentinels automatically resizes the arrays. Existing values retain their numeric positions (Env1=0 through KeyTrack=6, FilterCutoff=0 through OscBPitch=6).

**Before**:
```
VoiceModSource: Env1=0, Env2=1, Env3=2, VoiceLFO=3, GateOutput=4, Velocity=5, KeyTrack=6, NumSources=7
VoiceModDest: FilterCutoff=0, ..., OscBPitch=6, NumDestinations=7
```

**After**:
```
VoiceModSource: ..., KeyTrack=6, Aftertouch=7, NumSources=8
VoiceModDest: ..., OscBPitch=6, OscALevel=7, OscBLevel=8, NumDestinations=9
```

**Breaking Change**: VoiceModRouter::computeOffsets() signature changes from 7 to 8 parameters. This requires updating:
1. `ruinae_voice.h` line 355 (the only production caller)
2. `voice_mod_router_test.cpp` (all test cases)

**Backward Compatibility**: SC-005 requires existing 041 tests to pass. Since we ARE updating the test file (to add the 8th parameter), the existing test logic remains unchanged -- we just add `0.0f` as the aftertouch parameter to existing calls. The default aftertouch of 0.0f means no contribution from aftertouch routes.

---

## R-003: OscALevel/OscBLevel Application Strategy

**Question**: Where in the signal flow should oscillator level modulation be applied?

**Decision**: Apply level modulation to oscillator buffers BEFORE the mixer stage, computed once per block.

**Rationale**: The current signal flow:
1. Generate oscABuffer_ (full block)
2. Generate oscBBuffer_ (full block)
3. Mix (per-sample with morph modulation)
4. Filter (per-sample)
5. Distortion (block)
6. DC Block + TranceGate + VCA (per-sample)

The most natural place for oscillator level control is between steps 2 and 3: scale each oscillator buffer by its effective level before mixing. This:
- Keeps modulation application centralized in the modulation section
- Does not require modifying oscillator code
- Works correctly for both CrossfadeMix and SpectralMorph modes (both take oscABuffer_ and oscBBuffer_ as inputs)

**Per-block computation**: Since oscillator buffers are generated as complete blocks, and VoiceModRouter computes per-block (FR-009), the OscALevel/OscBLevel offsets should be computed once at block start and applied uniformly. This is consistent with how the spec envisions per-voice modulation.

**Implementation Approach**: The current code computes modulation per-sample inside the inner loop. For OscALevel/OscBLevel we need the offsets before the loop. Solution:
1. Call computeOffsets() once before the per-sample loop with the initial envelope values
2. Extract OscALevel/OscBLevel offsets from this initial computation
3. Apply level scaling to buffers
4. Continue with the per-sample loop for filter cutoff, morph position, etc.

This requires restructuring processBlock() to separate the block-rate and sample-rate modulation concerns.

---

## R-004: Global Modulation Source Registration Strategy

**Question**: How to register Pitch Bend and Mod Wheel as global modulation sources when they are not in the ModSource enum?

**Decision**: Use the existing Macro infrastructure in ModulationEngine. Map Pitch Bend to Macro1 and Mod Wheel to Macro2, with Macro3-4 available as user macros.

**Rationale**: The ModSource enum has 13 values (None + 12 sources): LFO1, LFO2, EnvFollower, Random, Macro1-4, Chaos, SampleHold, PitchFollower, Transient. Neither PitchBend nor ModWheel exist as dedicated sources. The options are:

1. **Extend ModSource enum**: Clean but requires Layer 0 changes, ModulationEngine source handling updates, and impacts all existing tests.
2. **Use Macros as proxy**: Simple, no enum changes, macro values are user-settable [0,1] floats. Pitch bend normalization: `14-bit [-1,+1] -> macro [0,1]`. Mod wheel: `CC#1 [0,127] -> [0,1]`.
3. **Create standalone MidiControlSource**: Would use ModulationMatrix (not ModulationEngine) and add complexity.

Option 2 is chosen because:
- The Ruinae Engine (Phase 6) will eventually own the full integration
- For this spec's test scaffold, macros are sufficient to validate the routing logic
- The normalization formulas (FR-015, FR-016) can be tested in isolation before applying to macro values
- Phase 6 can either keep the macro approach or add dedicated ModSource values

**Normalization Details**:
- Pitch Bend (FR-015): MIDI 14-bit [0x0000, 0x3FFF] -> center 0x2000=0.0, min=-1.0, max=+1.0
  - Formula: `normalized = (rawValue - 8192) / 8192.0f` (or `(rawValue - 8192) / 8191.0f` for exact +1.0 at max)
  - Mapped to macro: `macroValue = (normalized + 1.0f) * 0.5f`
- Mod Wheel (FR-016): MIDI CC#1 [0, 127] -> [0.0, 1.0]
  - Formula: `normalized = ccValue / 127.0f`
  - Mapped directly to macro value

---

## R-005: Global-to-Voice Forwarding Mechanism

**Question**: How should global modulation offsets be forwarded to per-voice parameters?

**Decision**: The test scaffold processes the global ModulationEngine first, extracts offsets for "All Voice" destinations, then applies them to each voice using the two-stage clamping formula from FR-021.

**Rationale**: The spec clearly defines the application order:
```
finalValue = clamp(clamp(baseValue + perVoiceOffset, min, max) + globalOffset, min, max)
```

This means:
1. Per-voice modulation is computed and clamped first (within voice-level valid range)
2. Global offset is added on top
3. Final result is clamped again

For each forwarded destination:
- **All Voice Filter Cutoff**: Global offset added to each voice's computed filter cutoff in Hz
- **All Voice Morph Position**: Global offset added to each voice's mix position [0, 1]
- **Trance Gate Rate**: Global offset (Hz) added to each voice's TranceGateParams.rateHz, clamped to [0.1, 20.0]

The forwarding happens in the engine's per-block processing, after ModulationEngine.process() and before individual voice processBlock() calls.

---

## R-006: Real-Time Safety Verification Strategy

**Question**: How to verify zero heap allocations in the modulation path?

**Decision**: Use a combination of code review and benchmark tests.

**Rationale**: The spec suggests AddressSanitizer or allocator instrumentation (SC-004). For this spec:
1. **Code review**: Verify no `new`, `delete`, `malloc`, `free`, `vector::push_back`, `string`, or other allocating operations in the modified code paths
2. **Benchmark tests**: Use Catch2 BENCHMARK to measure modulation processing time, which would reveal any unexpected allocation overhead
3. **Existing patterns**: All modified components already use fixed-size std::array and pre-allocated storage. The changes add no new dynamic data structures.

The existing VoiceModRouter uses `std::array<VoiceModRoute, 16>` (fixed). The ModulationEngine uses `std::array<ModRouting, 32>` (fixed). No changes introduce dynamic allocation.

---

## R-007: ChaosModSource Lorenz Parameters Verification

**Question**: Does the existing ChaosModSource already use canonical Lorenz parameters per FR-025?

**Decision**: Yes -- verified in `chaos_mod_source.h` lines 191-202.

**Evidence**:
```cpp
void updateLorenz(float dt) noexcept {
    constexpr float sigma = 10.0f;   // canonical
    constexpr float rho = 28.0f;     // canonical
    constexpr float beta = 8.0f / 3.0f; // canonical (2.667)
    // ... Euler integration
}
```

And the divergence check at line 258-264:
```cpp
void checkAndResetIfDiverged() noexcept {
    if (std::abs(state_.x) > safeBound_ * 10.0f ||  // safeBound_ = 50 for Lorenz
        std::abs(state_.y) > safeBound_ * 10.0f ||  // 50 * 10 = 500
        std::abs(state_.z) > safeBound_ * 10.0f) {
        resetModelState();
    }
}
```

This matches FR-025: "auto-reset when any state variable exceeds 10x its safe bound (500 for Lorenz)."

Output normalization uses `std::tanh(state_.x / 20.0f)` clamped to [-1, +1], matching the soft-limiting requirement.

**Conclusion**: FR-025 is already satisfied by the existing ChaosModSource. No changes needed.
