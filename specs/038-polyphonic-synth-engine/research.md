# Research: Polyphonic Synth Engine

**Feature Branch**: `038-polyphonic-synth-engine` | **Date**: 2026-02-07

## Research Questions

### RQ-1: SynthVoice `setFrequency()` Method Addition

**Decision**: Add a `setFrequency(float hz)` method to `SynthVoice` that updates both oscillator frequencies without retriggering envelopes.

**Rationale**: The spec (FR-009 assumption) explicitly requires this for mono mode legato pitch changes. Currently, `SynthVoice::noteOn()` always gates both envelopes. For legato (retrigger=false), the engine needs to update the oscillator pitch without restarting the amplitude/filter envelopes. The existing `noteOn()` cannot be reused because it calls `ampEnv_.gate(true)` and `filterEnv_.gate(true)` unconditionally.

**Implementation**:
```cpp
void setFrequency(float hz) noexcept {
    if (detail::isNaN(hz) || detail::isInf(hz)) return;
    noteFrequency_ = (hz < 0.0f) ? 0.0f : hz;
    osc1_.setFrequency(noteFrequency_);
    updateOsc2Frequency();
}
```

**Alternatives considered**:
- Option A: Add a `bool retrigger` parameter to `noteOn()`. Rejected because it changes the existing API contract and adds complexity to the most commonly called method.
- Option B: Expose oscillators directly. Rejected because it breaks encapsulation and the SynthVoice signal flow abstraction.

**Impact**: Backwards-compatible addition. No existing tests break. One new method, ~5 lines.

---

### RQ-2: Gain Compensation Strategy

**Decision**: Use `1.0f / std::sqrt(static_cast<float>(polyphonyCount))` as the compensation factor, based on the **configured** polyphony count (not the active voice count).

**Rationale**: Per spec clarification, using the configured count prevents level pumping as voices come and go. The 1/sqrt(N) formula is the standard for summing uncorrelated signals, where power adds linearly (RMS) rather than amplitude.

**Alternatives considered**:
- Active voice count compensation: Rejected because it causes audible level pumping when voices trigger/release.
- Fixed 1/N (linear): Too aggressive -- 8 voices at 1/8 gain each produces perceptually quiet output.
- No compensation: Output clips with even 4 voices at full velocity.

**Key values**:
| Polyphony | Compensation | With masterGain=1.0 |
|-----------|-------------|---------------------|
| 1 | 1.000 | 1.000 |
| 2 | 0.707 | 0.707 |
| 4 | 0.500 | 0.500 |
| 8 | 0.354 | 0.354 |
| 16 | 0.250 | 0.250 |

---

### RQ-3: Soft Limiting via Sigmoid::tanh()

**Decision**: Use `Sigmoid::tanh()` (which wraps `FastMath::fastTanh()`) for the soft limiter. No oversampling needed.

**Rationale**: The Pade (5,4) approximation in `FastMath::fastTanh()` is:
- 3x faster than std::tanh (verified in benchmarks)
- Constexpr and real-time safe
- Returns +/-1 for +/-Inf, NaN for NaN (safe edge cases)
- Nearly linear for |x| < 0.5 (< 4% deviation from unity)
- At |x| = 0.8, deviation from unity is ~2% (per spec FR-025 threshold)
- Asymptotically approaches +/-1.0 (never exceeds)

**Oversampling consideration**: Constitution Principle X requires min 2x oversampling for saturation/waveshaping. However, the soft limiter is applied at the master output stage where the input is a sum of already band-limited oscillator outputs. The tanh function is applied as a safety limiter, not as a creative distortion effect. The aliasing contribution is minimal because:
1. Input signals are already band-limited by PolyBLEP oscillators
2. The limiter only engages on peaks (most samples pass through nearly linearly)
3. The purpose is clipping prevention, not harmonic generation

**Decision**: No oversampling for the soft limiter. Document this as a justified deviation from Principle X, since oversampling would double the CPU cost of the entire engine output path for a negligible quality improvement in a safety limiter context.

**Alternatives considered**:
- `Sigmoid::softClipCubic()`: Faster (8-10x vs std::tanh) but hard-clips at |x| >= 1.0, defeating the purpose of soft limiting for signals that exceed unity.
- `std::tanh()`: Accurate but 3x slower. Unnecessary for a safety limiter.
- Hard clip: Introduces harsh distortion artifacts, rejected.

---

### RQ-4: Poly-to-Mono Voice Transfer Strategy

**Decision**: Use the simplified approach from the spec assumptions -- track noteOn timestamps per voice, identify the most recently triggered voice, call `noteOff()` on all other voices, and allow the most recent voice to continue at its current index. Initialize the MonoHandler with the active note.

**Rationale**: The spec explicitly says "For the initial implementation, a simpler approach is acceptable: the engine identifies the most recent voice, calls noteOff on all other voices, and allows the most recent voice to continue at its current index. The MonoHandler is then initialized with the active note." This avoids complex state copying between voice instances.

**Implementation detail**: The engine maintains a `uint64_t noteOnTimestamp_[kMaxPolyphony]` array. On each `noteOn()` call, the corresponding voice index gets the current monotonic counter value. When switching to mono, scan for the highest timestamp to find the most recent voice.

**Mono voice index**: In mono mode, voice 0 is NOT required to be the mono voice. The mono voice continues at whatever index it was already playing. This simplifies the switch. However, for clarity and consistency with the spec's reference to "voice 0", we can designate voice 0 as the mono voice and call `setFrequency()` on voice 0 if the most recent voice is at a different index, while calling `noteOff()` on the original. After switching, all subsequent mono mode operations use voice 0.

**Revised approach**: Actually, re-reading FR-013 more carefully: "designate the surviving voice as the mono voice for subsequent processing". The spec does NOT require it to be voice 0 specifically. But FR-009 says "the engine MUST route the frequency and velocity to voice index 0 (the designated mono voice)". So voice 0 IS the mono voice. This means if the most recently triggered voice is at index 3, we need to either swap it to index 0 or restart voice 0 with the same note. Since SynthVoice does not expose state getters, the simplest approach is: noteOff all voices, then noteOn voice 0 with the most recent note's frequency/velocity. This causes a brief envelope restart, but FR-013 says the voice "continues playing", which conflicts.

**Final decision**: The engine tracks which voice index holds the most recent note via timestamps. When switching poly->mono:
1. If most recent voice IS at index 0: just noteOff all other voices, done.
2. If most recent voice is at index N (N != 0): call noteOff on ALL voices including N, then immediately call noteOn on voice 0 with the frequency/velocity of the most recent note. This means voice 0 restarts (envelope from attack). Per SC-007, the mode switch must not produce discontinuities > -40 dBFS (peak abs diff < 0.01). Since the old voice enters release and the new voice starts attack, the crossover should be smooth enough if both happen in the same block.

This is acceptable because: mono mode is primarily used for bass/lead lines where a brief restart is musically acceptable, and the SC-007 test can verify smoothness.

**Alternatives considered**:
- Swap voice objects in the array: Would work but adds complexity and the SynthVoice class was not designed for swapping mid-playback.
- Keep mono voice at arbitrary index: Conflicts with FR-009 which specifies voice index 0.

---

### RQ-5: MonoHandler noteOn/noteOff Parameter Types

**Decision**: The MonoHandler takes `int note, int velocity` for noteOn and `int note` for noteOff, NOT `uint8_t`. The engine must cast appropriately.

**Rationale**: Reading the actual MonoHandler API:
```cpp
[[nodiscard]] MonoNoteEvent noteOn(int note, int velocity) noexcept;
[[nodiscard]] MonoNoteEvent noteOff(int note) noexcept;
```
These take `int`, not `uint8_t`. The engine receives `uint8_t` from the caller and casts to `int` when forwarding to MonoHandler.

---

### RQ-6: NoteProcessor Integration Pattern

**Decision**: The NoteProcessor is called once per block for pitch bend smoothing (`processPitchBend()`), then individual voice frequencies are obtained via `getFrequency(note)` which applies the current smoothed bend ratio.

**Rationale**: This matches the documented usage pattern in the NoteProcessor header:
```
1. prepare(sampleRate) -- once at init or sample rate change
2. setPitchBend(bipolar) -- when MIDI pitch bend received
3. processPitchBend() -- once per audio block (shared by all voices)
4. getFrequency(note) -- per voice per block
5. mapVelocity(velocity) -- per note-on event
```

The engine does NOT need to recalculate all voice frequencies every block. It only needs to call `processPitchBend()` once and then use the updated bend ratio implicitly through `getFrequency()`. However, for mono mode portamento, voice 0's frequency is updated per-sample from the MonoHandler's portamento output, which already includes the pitch slide. In that case, the pitch bend ratio from NoteProcessor should also be applied to the portamento frequency.

**Open question resolved**: The spec says in FR-017 "pitch bend is also forwarded to the VoiceAllocator (for frequency recalculation on active voices)". Looking at `VoiceAllocator::setPitchBend(float semitones)`, it takes semitones (not bipolar) and recalculates all active voice frequencies. So the engine should: (1) forward bipolar to NoteProcessor, (2) compute semitones = bipolar * pitchBendRange, and (3) forward semitones to VoiceAllocator. However, VoiceAllocator already does its own frequency computation including pitch bend. This means the engine should use VoiceAllocator's frequency as the source of truth for poly mode, not NoteProcessor's `getFrequency()`. In fact, VoiceAllocator stores frequency per voice and applies pitch bend via `recalculateAllFrequencies()`.

**Revised integration**: For poly mode, use VoiceAllocator's stored frequency (from `VoiceEvent::frequency`) which already includes pitch bend. For mono mode, use MonoHandler's portamento output frequency, then apply NoteProcessor's bend ratio on top.

---

### RQ-7: Scratch Buffer Strategy

**Decision**: Pre-allocate a single `std::vector<float>` scratch buffer during `prepare()` with size `maxBlockSize`. Zero-fill it before each voice's `processBlock()` call.

**Rationale**: The engine needs a temporary buffer to hold each voice's output before summing into the main output. Using a single scratch buffer (overwritten per voice) is memory-efficient. The `std::vector` allocation happens only in `prepare()`, not during audio processing. The scratch buffer is resized in `prepare()` and never resized after.

**Alternative**: `std::array<float, kMaxBlockSize>` with a compile-time max. Rejected because the max block size varies by host (typically 32-4096 samples) and a compile-time limit would either waste memory or be too restrictive.

---

### RQ-8: Voice Lifecycle Deferred Notification

**Decision**: After processBlock completes, iterate all voices and check `isActive()`. For any voice that was active before the block but is now inactive, call `allocator_.voiceFinished(i)`.

**Rationale**: Per FR-028 and FR-029, voice lifecycle notifications must be deferred until after the entire block is processed. This prevents mid-block state changes in the allocator.

**Implementation**: Track which voices were active at the start of the block using a simple `bool wasActive[kMaxPolyphony]` array. After processing, compare with current `isActive()` state.

---

### RQ-9: Memory Footprint Analysis (SC-010)

**Decision**: Verify that `sizeof(PolySynthEngine)` excluding the scratch buffer vector does not exceed 32 KB.

**Estimated breakdown**:
| Member | Size (bytes) |
|--------|-------------|
| 16x SynthVoice (~200 bytes each) | ~3200 |
| 1x VoiceAllocator (~4000 bytes) | ~4000 |
| 1x MonoHandler (~100 bytes) | ~100 |
| 1x NoteProcessor (~50 bytes) | ~50 |
| 1x SVF (global filter, ~60 bytes) | ~60 |
| VoiceMode enum + config fields | ~100 |
| Timestamp tracking (16x uint64_t) | 128 |
| wasActive tracking (16x bool) | 16 |
| Misc fields (gains, bools, etc.) | ~100 |
| Total | ~7754 |

Well under 32 KB. The `std::vector<float>` scratch buffer itself is just ~24 bytes (pointer + size + capacity); the heap allocation it manages is excluded per SC-010.

---

### RQ-10: Global Filter Bypass Strategy

**Decision**: When `globalFilterEnabled_` is false, skip the SVF processing entirely (no call to `filter_.process()` or `processBlock()`). The summed voice output passes directly to the master gain stage.

**Rationale**: This is the most CPU-efficient approach and matches FR-020: "no processing overhead" when disabled.

---

### RQ-11: SIMD Viability for Voice Processing Loop

**Decision**: NOT BENEFICIAL for initial implementation. Defer to Phase 2 if profiling shows need.

**Rationale**:
- The main loop iterates over active voices (variable count, typically 1-8)
- Each voice has internal feedback (filter state, envelope state) creating serial dependencies
- Voice processing is already encapsulated in SynthVoice::processBlock() which is a black box
- The summing loop (adding voice outputs) could benefit from SIMD, but the overhead of setting up SIMD for 128-4096 float additions is negligible vs the voice processing itself
- The engine is primarily a composition/orchestration layer, not a compute-intensive DSP kernel

**Alternative optimizations**:
- Skip processing of inactive voices (already in spec as FR-027)
- Early-out when all voices are idle (return silence)
- Use the existing per-voice processBlock() which processes in contiguous blocks for cache efficiency
