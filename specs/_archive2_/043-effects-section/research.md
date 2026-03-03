# Research: Ruinae Effects Section

**Feature**: 043-effects-section | **Date**: 2026-02-08

## R-001: Heterogeneous Delay API Normalization

**Question**: How should the chain normalize five delay types with different prepare/process/parameter signatures into a uniform interface?

**Decision**: Direct dispatch via switch on `RuinaeDelayType` enum, NOT a polymorphic base class.

**Rationale**: The five delay types have genuinely different APIs:

| Delay Type | prepare() Signature | process() Signature | Time Setter | Mix Setter |
|------------|--------------------|--------------------|-------------|------------|
| Digital | `(sampleRate, maxBlockSize, maxDelayMs)` | `(L, R, n, ctx)` in-place | `setTime(ms)` | `setMix(0-1)` |
| Tape | `(sampleRate, maxBlockSize, maxDelayMs)` | `(L, R, n)` NO ctx | `setMotorSpeed(ms)` | `setMix(0-1)` |
| PingPong | `(sampleRate, maxBlockSize, maxDelayMs)` | `(L, R, n, ctx)` in-place | `setDelayTimeMs(ms)` | `setMix(0-1)` |
| Granular | `(sampleRate)` only | `(inL, inR, outL, outR, n, ctx)` separate buffers | `setDelayTime(ms)` | `setDryWet(0-1)` |
| Spectral | `(sampleRate, maxBlockSize)` | `(L, R, n, ctx)` in-place | `setBaseDelayMs(ms)` | `setDryWetMix(0-1)` |

A polymorphic wrapper would require virtual dispatch per sample or per block, adding unnecessary overhead and complexity. Instead, a simple switch statement in each forwarding method (processDelay, setDelayTimeForType, etc.) dispatches to the correct API. This is the same pattern used throughout the codebase (RuinaeVoice uses switch dispatch for filter/distortion types).

**Alternatives Considered**:
1. **std::variant + std::visit**: Clean but still requires visitor lambdas for each method, and the types are non-copyable. No real benefit over switch.
2. **Polymorphic IDelay interface**: Would require wrapper classes around each delay, adding layers. The API differences (separate buffers, percent vs ratio) make this verbose.
3. **Direct switch dispatch** (chosen): Simple, zero overhead, matches existing patterns in the codebase.

## R-002: Crossfade State Machine Design

**Question**: How should the delay type crossfade state machine be structured for click-free transitions with fast-track support?

**Decision**: Three-state machine: `Idle`, `Crossfading`. Two slot pointers: `activeType_` and `outgoingType_`. Alpha ramp from 0 to 1 with per-sample increment.

**Rationale**: The state machine needs to handle:
1. Normal switch: start crossfade from current to new type
2. Fast-track: snap current crossfade to completion, start new immediately
3. Same-type no-op: skip if requested type == active type
4. Post-crossfade cleanup: reset outgoing delay

**State Machine Design**:
```
States: Idle, Crossfading
Transitions:
  Idle + setDelayType(same)     -> Idle (no-op)
  Idle + setDelayType(new)      -> Crossfading (active=old, incoming=new, alpha=0)
  Crossfading + alpha >= 1.0    -> Idle (active=incoming, reset outgoing)
  Crossfading + setDelayType(X) -> Fast-track: snap alpha=1.0, active=incoming,
                                   reset old outgoing, start new Crossfading
```

**Per-sample crossfade in processBlock**:
```
for each sample:
  process active delay -> activeOut
  if crossfading:
    process incoming delay -> incomingOut
    output = activeOut * (1 - alpha) + incomingOut * alpha
    alpha += increment
    if alpha >= 1.0:
      complete crossfade
  else:
    output = activeOut
```

The duration is 30ms (chosen within the 25-50ms spec range), calculated as: `increment = crossfadeIncrement(30.0f, sampleRate)`.

**Alternatives Considered**:
1. **Block-level crossfade**: Process both delays for full blocks, blend afterward. Simpler but wastes CPU processing full blocks even when crossfade ends mid-block.
2. **Per-sample with early termination** (chosen): More precise, allows cleanup immediately when crossfade completes within a block.

## R-003: Per-Delay Latency Compensation Strategy

**Question**: How should non-spectral delays compensate to match the spectral delay's FFT latency?

**Decision**: Use dedicated `DelayLine` instances per non-spectral delay type as padding delays. Each non-spectral delay's output is routed through its compensation delay during processing.

**Rationale**: The SpectralDelay has intrinsic FFT latency (default 1024 samples = ~23ms at 44.1kHz). For the host to report a constant latency, all delay types must appear to have the same latency. Non-spectral delays (Digital, Tape, PingPong, Granular) have zero or negligible intrinsic latency.

**Implementation**:
- At prepare() time: query `spectralDelay_.getLatencySamples()` to get target latency (e.g., 1024)
- For each non-spectral delay: allocate a stereo `DelayLine` pair sized to target latency
- During processBlock(): after processing a non-spectral delay, write output to compensation delay line, read with fixed offset = targetLatency
- The spectral delay already has this latency built in, so no compensation needed for it
- `getLatencySamples()` always returns the spectral delay's FFT size

**Important**: The compensation delays are simple pass-through delays (write + read at fixed offset). They do NOT use interpolation since the offset is an integer number of samples.

**Alternatives Considered**:
1. **Report zero latency, let host deal with it**: Violates FR-027 (constant latency).
2. **Report spectral latency only when spectral is active**: Causes latency changes on type switch, which is precisely what the spec forbids.
3. **Internal padding delays** (chosen): Encapsulated, constant latency, transparent to callers.

## R-004: FreezeMode Integration as Insert Effect

**Question**: How should FreezeMode (designed as a standalone delay effect with its own dry/wet) be used as an insert slot in the effects chain?

**Decision**: Use FreezeMode with dry/wet mix set to 100% (fully wet) when freeze is enabled, and bypass the slot entirely when freeze is disabled. The chain manages the freeze enable/disable state.

**Rationale**: FreezeMode is a complete Layer 4 effect with its own delay line, dry/wet mix, and feedback network. In the effects chain context:
- When freeze is **disabled** (`freezeEnabled_ == false`): the freeze slot is bypassed entirely (signal passes through unchanged). This avoids any latency or coloration from the internal delay line.
- When freeze is **enabled**: FreezeMode's dry/wet mix is set to 100% so the full freeze output is used. The chain exposes `setFreeze(bool)` to toggle the actual freeze capture.

The FreezeMode's creative parameters (pitch semitones, shimmer mix, decay) are forwarded directly through the chain's API.

**Alternatives Considered**:
1. **Use FreezeFeedbackProcessor directly**: Lower-level, would need manual FlexibleFeedbackNetwork setup. FreezeMode already handles all this composition.
2. **Always process FreezeMode**: Wastes CPU when freeze is off, and the internal delay adds unwanted latency.
3. **Bypass when disabled** (chosen): Zero overhead when off, full capability when on.

## R-005: GranularDelay Buffer Normalization

**Question**: GranularDelay uses separate input/output buffers. How to normalize this for the in-place chain?

**Decision**: Pre-allocate temporary input buffers during prepare(). Copy in-place buffers to temp, call GranularDelay::process() with temp as input and in-place as output.

**Rationale**: GranularDelay's signature is:
```cpp
void process(const float* leftIn, const float* rightIn,
             float* leftOut, float* rightOut,
             size_t numSamples, const BlockContext& ctx) noexcept
```

The chain needs to call this from in-place buffers. The simplest approach:
```cpp
// Copy input to temp
memcpy(tempL_, left, numSamples * sizeof(float));
memcpy(tempR_, right, numSamples * sizeof(float));
// Process with temp as input, original buffers as output
granular_.process(tempL_, tempR_, left, right, numSamples, ctx);
```

The temp buffers (`tempL_`, `tempR_`) are pre-allocated in prepare() to `maxBlockSize` samples. These same temp buffers are used for crossfade mixing, so they serve dual purpose.

## R-006: Crossfade Duration Selection

**Question**: What exact crossfade duration within the 25-50ms range?

**Decision**: 30ms.

**Rationale**: 30ms is in the middle of the spec range (25-50ms). At 44.1kHz this is 1323 samples, which provides sufficient smoothing to mask discontinuities while being short enough to feel responsive. This matches the crossfade times used elsewhere in the codebase (CharacterProcessor uses variable times, DigitalDelay wavefold crossfade uses 100ms for a different purpose).

## R-007: Parameter Forwarding During Crossfade

**Question**: Should delay parameters (time, feedback, mix) be forwarded to both active and incoming delays during a crossfade?

**Decision**: Yes, forward to ALL delay types always, not just the active/incoming pair.

**Rationale**: FR-015 explicitly requires that parameters be forwarded to both active and crossfade partner during transitions. However, a simpler approach is to forward parameters to all five delay types at all times. This ensures:
1. When a crossfade starts, the incoming delay already has current parameters
2. No special-case logic needed for crossfade vs non-crossfade
3. Parameter setters remain cheap (just set a value on each delay type)
4. Inactive delays ignore the parameters since they're not processing audio

The only performance concern would be if parameter setters triggered expensive computation, but examining the headers shows they all just store values and update smoothers -- trivially cheap.

## R-008: Freeze Slot Bypass vs Mix Control

**Question**: Should the freeze slot bypass be via FreezeMode's own dry/wet mix or via external bypass logic?

**Decision**: External bypass logic in the chain. When `freezeEnabled_ == false`, skip the FreezeMode::process() call entirely. When enabled, always call process() with dry/wet at 100%.

**Rationale**: The spec defines two separate controls:
- `setFreezeEnabled(bool)`: activates/deactivates the freeze slot
- `setFreeze(bool)`: toggles the freeze capture state

When the slot is disabled, we want zero overhead (no processing). When enabled but not frozen, FreezeMode acts as a pass-through delay (live signal goes through). When enabled and frozen, FreezeMode captures and sustains the spectrum.

Setting FreezeMode's internal dry/wet to 100% and managing bypass externally gives the cleanest separation of concerns.
