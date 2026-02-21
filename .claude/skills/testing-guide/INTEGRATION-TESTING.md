# Integration Testing Guide

Integration tests verify that components work correctly **when wired together** in the real
processing chain. Unit tests prove a DSP component works in isolation; integration tests prove
it works through the processor's MIDI routing, parameter application, and audio pipeline.

**This is where the most costly bugs hide.** Both critical arp bugs (071) passed all unit tests
but failed immediately on manual testing — because the integration layer (wiring, parameter
application, host environment) was never tested for behavioral correctness.

---

## When Integration Tests Are Required

Write integration tests whenever you:

1. **Wire a sub-component into the processor** (MIDI routing, audio chain insertion)
2. **Apply parameters from atomics to a sub-component** via `applyParamsToEngine()`
3. **Route events conditionally** (e.g., MIDI goes to arp OR directly to engine)
4. **Add stateful per-block processing** (arpeggiator, sequencer, trance gate, LFO sync)
5. **Depend on host-provided context** (transport state, tempo, time signature)

**Rule of thumb:** If the feature involves the processor calling methods on a sub-component
every audio block, there MUST be integration tests that verify the observable behavior across
multiple blocks — not just "audio present" but "audio correct."

---

## Anti-Pattern: The Existence Check

> **THIS IS THE #1 INTEGRATION TEST FAILURE MODE**
>
> Checking "is there audio output?" does NOT verify the feature works. It only verifies
> the processor didn't crash and something made noise.

### The Bug That Shipped

```cpp
// BAD: This test passed while the arp only played one note repeatedly
TEST_CASE("Arp produces audio", "[arp][integration]") {
    // ... setup, enable arp, send chord (C4, E4, G4) ...

    bool audioFound = false;
    for (int i = 0; i < 60; ++i) {
        processBlock();
        if (hasNonZeroSamples(outL.data(), blockSize)) {
            audioFound = true;
            break;
        }
    }
    REQUIRE(audioFound);  // PASSES! But only note C4 is ever played.
}
```

**Why it passed:** The arp DID produce audio — it just played the same note (C4) over and
over because `setMode()` was resetting the step index every block. The test never checked
WHICH notes were playing.

### The Fix: Verify Behavioral Correctness

```cpp
// GOOD: Verify the arp actually cycles through different notes
TEST_CASE("Arp cycles through chord notes in Up mode", "[arp][integration]") {
    using namespace Krate::DSP;

    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setTempoSync(true);

    arp.noteOn(60, 100);  // C4
    arp.noteOn(64, 100);  // E4
    arp.noteOn(67, 100);  // G4

    BlockContext ctx{.sampleRate = 44100.0, .blockSize = 512,
                     .tempoBPM = 120.0, .isPlaying = true};
    std::array<ArpEvent, 128> events{};
    std::set<uint8_t> notesHeard;

    for (int block = 0; block < 100; ++block) {
        size_t n = arp.processBlock(ctx, events);
        for (size_t i = 0; i < n; ++i) {
            if (events[i].type == ArpEvent::Type::NoteOn)
                notesHeard.insert(events[i].note);
        }
    }

    // All 3 chord notes must appear, not just the first one
    REQUIRE(notesHeard.size() == 3);
    CHECK(notesHeard.count(60) == 1);
    CHECK(notesHeard.count(64) == 1);
    CHECK(notesHeard.count(67) == 1);
}
```

### What to Verify Instead of "Audio Present"

| Feature | Existence Check (BAD) | Behavioral Check (GOOD) |
|---------|----------------------|------------------------|
| Arpeggiator | "Audio output exists" | "All held notes appear in output events" |
| Trance gate | "Audio output exists" | "Audio amplitude follows gate pattern" |
| LFO modulation | "Parameter changes" | "Parameter follows expected LFO shape" |
| Filter sweep | "Audio output exists" | "Spectral content shifts with cutoff" |
| Pitch shifter | "Audio output exists" | "Output frequency matches target pitch" |
| Sequencer | "Notes play" | "Notes play in correct order with correct timing" |

---

## Anti-Pattern: The Perfect Host

> **Testing only with ideal host conditions misses real-world failures.**

### The Bug That Shipped

```cpp
// BAD: Fixture always sets kPlaying — every host provides transport, right?
struct ArpFixture {
    ArpFixture() {
        processContext.state = ProcessContext::kPlaying      // Always playing
                            | ProcessContext::kTempoValid    // Always has tempo
                            | ProcessContext::kTimeSigValid; // Always has time sig
        processContext.tempo = 120.0;
        // ...
    }
};
```

**What happened:** The arp worked perfectly in DAWs (which provide transport state). In Plugin
Buddy (a simple test host with NO transport), `kPlaying` was never set, so the arp
early-returned and produced silence. No test ever simulated this.

### The Fix: Test Degraded Host Conditions

Every feature that reads from `ProcessContext` must be tested with minimal context:

```cpp
TEST_CASE("Feature works without host transport", "[integration]") {
    // Simulate a minimal host: no kPlaying, no kTempoValid
    processContext.state = 0;  // Nothing set
    processContext.tempo = 0.0;

    // Feature should still function (using defaults or fallbacks)
    enableFeature();
    sendNote(60, 0.8f);

    bool audioFound = false;
    for (int i = 0; i < 60; ++i) {
        processBlock();
        if (hasNonZeroSamples(outL.data(), blockSize)) {
            audioFound = true;
            break;
        }
    }
    REQUIRE(audioFound);
}
```

### Host Conditions to Test

| Condition | What's Missing | Real Hosts That Do This |
|-----------|---------------|------------------------|
| No transport | `kPlaying` not set | Plugin Buddy, some simple hosts |
| No tempo | `kTempoValid` not set | Offline renderers, some hosts |
| No time signature | `kTimeSigValid` not set | Many hosts |
| No process context | `data.processContext == nullptr` | Edge case, but legal |
| Transport stop mid-playback | `kPlaying` cleared after notes sent | All DAWs |
| Tempo change mid-playback | `tempo` changes between blocks | All DAWs |

---

## Anti-Pattern: The Reset Trap

> **Calling configuration setters every block can silently break stateful components.**

### The Bug That Shipped

```cpp
// In applyParamsToEngine(), called every audio block:
void Processor::applyParamsToEngine() {
    // ...
    arpCore_.setMode(static_cast<ArpMode>(arpParams_.mode.load()));  // EVERY BLOCK
    // ...
}
```

`NoteSelector::setMode()` internally calls `reset()` which resets the step index to 0.
The arp never advanced past the first note because step index was reset 86 times per second.

### The Fix: Change-Detection Guards

```cpp
void Processor::applyParamsToEngine() {
    const auto mode = static_cast<ArpMode>(arpParams_.mode.load());
    if (mode != prevArpMode_) {         // Only when value changes
        arpCore_.setMode(mode);
        prevArpMode_ = mode;
    }
}
```

### The Required Test

**Every sub-component integrated into the processor must have a "per-block config" test:**

```cpp
TEST_CASE("Calling setMode every block prevents note advance", "[integration][bug]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);

    arp.noteOn(60, 100);
    arp.noteOn(64, 100);
    arp.noteOn(67, 100);

    BlockContext ctx{.sampleRate = 44100.0, .blockSize = 512,
                     .tempoBPM = 120.0, .isPlaying = true};
    std::array<ArpEvent, 128> events{};
    std::set<uint8_t> notesHeard;

    SECTION("BUG: setMode every block resets state") {
        for (int block = 0; block < 100; ++block) {
            arp.setMode(ArpMode::Up);  // Same value, but resets!
            size_t n = arp.processBlock(ctx, events);
            for (size_t i = 0; i < n; ++i)
                if (events[i].type == ArpEvent::Type::NoteOn)
                    notesHeard.insert(events[i].note);
        }
        CHECK(notesHeard.size() == 1);  // Only first note ever heard
    }

    SECTION("FIX: setMode only on change") {
        // setMode already called once in setup. Don't call again.
        for (int block = 0; block < 100; ++block) {
            size_t n = arp.processBlock(ctx, events);
            for (size_t i = 0; i < n; ++i)
                if (events[i].type == ArpEvent::Type::NoteOn)
                    notesHeard.insert(events[i].note);
        }
        REQUIRE(notesHeard.size() == 3);  // All notes cycle
    }
}
```

### Sub-Component Methods That Commonly Reset State

Watch out for setters that have side effects. Common patterns:

| Method Pattern | Likely Side Effect | Risk |
|---------------|-------------------|------|
| `setMode()` | Resets step/phase index | Sequencer/arp stuck on step 0 |
| `setWaveform()` | Resets phase | LFO/osc glitch every block |
| `setBufferSize()` | Clears internal buffer | Audio dropout every block |
| `setOrder()` | Reinitializes coefficients | Filter transient every block |
| `prepare()` | Full reset | Everything breaks |

**Rule:** Before calling any setter in `applyParamsToEngine()`, check whether it resets
internal state. If it does, guard it with change detection.

---

## Testing at the Right Level

When audio output analysis is unreliable (e.g., ADSR release tails make note boundary detection
impossible), test the sub-component directly while mirroring the processor's calling pattern:

### Level 1: Sub-Component Direct (Preferred for Behavioral Tests)

Use the sub-component's API directly, checking its output events or values. This avoids the
complexity of full audio processing while still testing the integration pattern.

```cpp
// Test the arp directly, mirroring how the processor calls it
TEST_CASE("Arp chord arpeggiation", "[arp][integration]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);

    // Mirror processor's applyParamsToEngine pattern
    arp.setMode(ArpMode::Up);
    arp.setEnabled(true);

    arp.noteOn(60, 100);
    arp.noteOn(64, 100);
    arp.noteOn(67, 100);

    // Simulate processor loop: only call safe setters per block
    for (int block = 0; block < 100; ++block) {
        arp.setTempoSync(true);     // Safe: no state reset
        arp.setGateLength(80.0f);   // Safe: no state reset
        // Do NOT call setMode() — mirrors the change-detection guard
        size_t n = arp.processBlock(ctx, events);
        // ... check events ...
    }
}
```

### Level 2: Full Processor (Required for Wiring Tests)

Use the actual `Processor` class with mock VST3 interfaces. Required for testing MIDI routing,
parameter handling, and state serialization — anything that involves the VST3 layer.

```cpp
TEST_CASE("Arp enable routes MIDI through arpeggiator", "[arp][integration]") {
    ArpIntegrationFixture f;
    f.enableArp();

    f.events.addNoteOn(60, 0.8f);
    f.processBlock();
    f.clearEvents();

    bool audioFound = false;
    for (int i = 0; i < 60; ++i) {
        f.processBlock();
        if (hasNonZeroSamples(f.outL.data(), f.kBlockSize)) {
            audioFound = true;
            break;
        }
    }
    REQUIRE(audioFound);
}
```

### When to Use Which Level

| What You're Testing | Level | Why |
|---------------------|-------|-----|
| Feature produces correct output (notes, timing, values) | Level 1 | Easier to inspect events/values directly |
| MIDI routing (enable/disable switches paths) | Level 2 | Needs real processor MIDI handling |
| Parameter changes affect sub-component | Level 1 | Mirror the apply pattern directly |
| State save/restore includes feature | Level 2 | Needs real getState/setState |
| Host environment variance | Level 2 | Needs real ProcessContext handling |
| Per-block configuration doesn't break state | Level 1 | Directly test the failure mode |

---

## Integration Test Checklist

When integrating a new sub-component into the processor, write tests for ALL of these:

### Wiring Tests (Level 2 — Full Processor)
- [ ] Feature enabled: input routes through sub-component, output is audible
- [ ] Feature disabled: input bypasses sub-component, output is audible
- [ ] Enable while playing: no stuck notes, no glitches
- [ ] Disable while playing: cleanup happens, output goes silent

### Behavioral Correctness Tests (Level 1 — Sub-Component Direct)
- [ ] Output is **correct**, not just **present** (verify specific values/events)
- [ ] Multi-block behavior advances properly (step index, phase, position)
- [ ] All modes/configurations produce expected distinct behavior

### Parameter Application Tests (Level 1 — Sub-Component Direct)
- [ ] Mirror the exact `applyParamsToEngine()` calling pattern
- [ ] Verify state-resetting setters don't break when called every block with same value
- [ ] Verify parameter changes take effect on next block

### Host Environment Tests (Level 2 — Full Processor)
- [ ] Works with no transport (`kPlaying` not set)
- [ ] Works with no tempo (`kTempoValid` not set)
- [ ] Works with `processContext == nullptr` (if applicable)
- [ ] Handles transport stop/start transitions
- [ ] Handles tempo changes mid-playback

### State Tests (Level 2 — Full Processor)
- [ ] All sub-component parameters survive save/load roundtrip
- [ ] Loading state from older version (without sub-component data) doesn't crash

---

## Common Integration Test Fixtures

### Minimal Processor Fixture

```cpp
struct IntegrationFixture {
    Processor processor;
    EventList events;
    EmptyParamChanges emptyParams;
    std::vector<float> outL, outR;
    float* channelBuffers[2];
    AudioBusBuffers outputBus{};
    ProcessData data{};
    ProcessContext processContext{};
    static constexpr size_t kBlockSize = 512;

    IntegrationFixture() : outL(kBlockSize, 0.0f), outR(kBlockSize, 0.0f) {
        channelBuffers[0] = outL.data();
        channelBuffers[1] = outR.data();
        outputBus.numChannels = 2;
        outputBus.channelBuffers32 = channelBuffers;

        // Standard host environment
        data.numSamples = kBlockSize;
        data.numOutputs = 1;
        data.outputs = &outputBus;
        data.inputParameterChanges = &emptyParams;
        data.inputEvents = &events;

        processContext.state = ProcessContext::kPlaying
                            | ProcessContext::kTempoValid;
        processContext.tempo = 120.0;
        data.processContext = &processContext;

        processor.initialize(nullptr);
        ProcessSetup setup{};
        setup.sampleRate = 44100.0;
        setup.maxSamplesPerBlock = kBlockSize;
        processor.setupProcessing(setup);
        processor.setActive(true);
    }

    void processBlock() {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        processor.process(data);
        // Advance transport
        processContext.projectTimeSamples += kBlockSize;
    }
};
```

### Degraded Host Fixture (No Transport)

```cpp
struct NoTransportFixture : IntegrationFixture {
    NoTransportFixture() : IntegrationFixture() {
        // Simulate a host with no transport support
        processContext.state = 0;
        processContext.tempo = 0.0;
    }
};
```
