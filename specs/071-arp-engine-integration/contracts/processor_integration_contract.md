# Processor Integration Contract

## New Members (processor.h)

```cpp
// In Processor class private section, after existing param fields:
ArpeggiatorParams arpParams_;                          // FR-010
Krate::DSP::ArpeggiatorCore arpCore_;                  // FR-010
std::array<Krate::DSP::ArpEvent, 128> arpEvents_{};   // FR-007
bool wasTransportPlaying_{false};                      // FR-018
```

## setupProcessing() Changes (FR-008)

```cpp
// After engine_.prepare():
arpCore_.prepare(sampleRate_, static_cast<size_t>(maxBlockSize_));
```

## setActive() Changes

```cpp
// After engine_.reset() in activation branch:
arpCore_.reset();
```

## process() Changes (FR-007)

Current order:
1. processParameterChanges()
2. Read tempo
3. applyParamsToEngine()
4. Build BlockContext -> engine_.setBlockContext(ctx)
5. processEvents()
6. engine_.processBlock()

New order:
1. processParameterChanges() -- unchanged
2. Read tempo -- unchanged
3. applyParamsToEngine() -- EXTENDED with arp params
4. Build BlockContext -> engine_.setBlockContext(ctx) + STORE ctx locally
5. processEvents() -- MODIFIED: branch on arp enabled
6. **NEW**: Arp block processing + transport handling
7. engine_.processBlock() -- unchanged

### Step 4: Store BlockContext

```cpp
Krate::DSP::BlockContext ctx;
// ... (existing code to populate ctx) ...
engine_.setBlockContext(ctx);
// Store for arp use:
blockCtx_ = ctx;  // OR just use ctx directly in the arp section below
```

### Step 5: processEvents() Modification (FR-006)

```cpp
void Processor::processEvents(Steinberg::Vst::IEventList* events) {
    // ... existing null check and loop ...

    const bool arpEnabled = arpParams_.enabled.load(std::memory_order_relaxed);

    // In kNoteOnEvent case:
    if (arpEnabled) {
        arpCore_.noteOn(pitch, velocity);
    } else {
        engine_.noteOn(pitch, velocity);
    }

    // In kNoteOffEvent case:
    if (arpEnabled) {
        arpCore_.noteOff(pitch);
    } else {
        engine_.noteOff(pitch);
    }

    // velocity-0 noteOn -> noteOff routing also needs the branch
}
```

### Step 6: Arp Block Processing (FR-007, FR-017, FR-018)

```cpp
// After processEvents(), before engine_.processBlock():
if (arpParams_.enabled.load(std::memory_order_relaxed)) {
    // Transport stop detection (FR-018)
    if (wasTransportPlaying_ && !ctx.isPlaying) {
        arpCore_.reset();  // Resets timing, sends note-offs, preserves held notes
    }
    wasTransportPlaying_ = ctx.isPlaying;

    // Process arp block (processBlock takes std::span<ArpEvent>, returns event count)
    size_t numArpEvents = arpCore_.processBlock(ctx, arpEvents_);

    // Route arp events to engine
    for (size_t i = 0; i < numArpEvents; ++i) {
        const auto& evt = arpEvents_[i];
        if (evt.type == Krate::DSP::ArpEvent::Type::NoteOn) {
            engine_.noteOn(evt.note, evt.velocity);
        } else {
            engine_.noteOff(evt.note);
        }
    }
} else {
    wasTransportPlaying_ = false;
}
```

## applyParamsToEngine() Extension (FR-009)

```cpp
// After existing trance gate section, add:
// --- Arpeggiator ---
arpCore_.setMode(static_cast<ArpMode>(
    arpParams_.mode.load(std::memory_order_relaxed)));
arpCore_.setOctaveRange(arpParams_.octaveRange.load(std::memory_order_relaxed));
arpCore_.setOctaveMode(static_cast<OctaveMode>(
    arpParams_.octaveMode.load(std::memory_order_relaxed)));
arpCore_.setTempoSync(arpParams_.tempoSync.load(std::memory_order_relaxed));
{
    auto mapping = getNoteValueFromDropdown(
        arpParams_.noteValue.load(std::memory_order_relaxed));
    arpCore_.setNoteValue(mapping.note, mapping.modifier);
}
arpCore_.setFreeRate(arpParams_.freeRate.load(std::memory_order_relaxed));
arpCore_.setGateLength(arpParams_.gateLength.load(std::memory_order_relaxed));
arpCore_.setSwing(arpParams_.swing.load(std::memory_order_relaxed));
arpCore_.setLatchMode(static_cast<LatchMode>(
    arpParams_.latchMode.load(std::memory_order_relaxed)));
arpCore_.setRetrigger(static_cast<ArpRetriggerMode>(
    arpParams_.retrigger.load(std::memory_order_relaxed)));
arpCore_.setEnabled(arpParams_.enabled.load(std::memory_order_relaxed));
// NOTE: setEnabled() LAST -- it may queue cleanup note-offs
```

## processParameterChanges() Extension

```cpp
// Add after harmonizer range:
} else if (paramId >= kArpBaseId && paramId <= kArpEndId) {
    handleArpParamChange(arpParams_, paramId, value);
}
```

## getState() Extension (FR-011)

```cpp
// Append after harmonizer enable flag:
saveArpParams(arpParams_, streamer);
```

## setState() Extension (FR-011)

```cpp
// Append after harmonizer enable flag load:
loadArpParams(arpParams_, streamer);
```

## Controller Integration

### Controller::initialize()

```cpp
// After registerTransientParams(parameters):
registerArpParams(parameters);
```

### Controller::getParamStringByValue()

```cpp
// Add after transient range:
} else if (id >= kArpBaseId && id <= kArpEndId) {
    result = formatArpParam(id, valueNormalized, string);
}
```

### Controller::setComponentState()

```cpp
// Append after harmonizer enable flag:
loadArpParamsToController(streamer, setParam);
```

### Controller::setParamNormalized()

```cpp
// Add arp sync visibility toggle:
if (tag == kArpTempoSyncId) {
    if (arpRateGroup_) arpRateGroup_->setVisible(value < 0.5);
    if (arpNoteValueGroup_) arpNoteValueGroup_->setVisible(value >= 0.5);
}
```

### Controller::didOpen() / createCustomView()

Wire up `arpRateGroup_` and `arpNoteValueGroup_` pointers from custom-view-name in editor.uidesc.

### Controller::willClose() / onTabChanged()

Null out arp view pointers when editor closes or tab switches away from SEQ.
