# Skip Event IMessage Contract

## Message ID

`"ArpSkipEvent"`

## Attributes

| Attribute | Type | Range | Description |
|-----------|------|-------|-------------|
| `"lane"` | int64 | 0-5 | Lane index (0=Velocity, 1=Gate, 2=Pitch, 3=Ratchet, 4=Modifier, 5=Condition) |
| `"step"` | int64 | 0-31 | Step index within the lane |

## Sender (Processor)

```cpp
// Pre-allocate in initialize() or setupProcessing():
// Store 6 IMessage* in a std::array<Steinberg::IPtr<Steinberg::Vst::IMessage>, 6>
// Each message has id "ArpSkipEvent" set once at allocation.

// In processBlock(), when a step is skipped:
void Processor::sendSkipEvent(int lane, int step) {
    if (!editorOpen_) return;  // Don't send when UI is invisible (FR-012)

    auto* msg = skipMessages_[lane].get();
    if (!msg) return;

    auto* attrs = msg->getAttributes();
    attrs->setInt("lane", static_cast<int64>(lane));
    attrs->setInt("step", static_cast<int64>(step));
    sendMessage(msg);
}
```

## Receiver (Controller)

```cpp
// In Controller::notify():
Steinberg::tresult Controller::notify(Steinberg::Vst::IMessage* message) {
    if (FIDStringsEqual(message->getMessageID(), "ArpSkipEvent")) {
        int64 lane = 0, step = 0;
        if (message->getAttributes()->getInt("lane", lane) == kResultOk &&
            message->getAttributes()->getInt("step", step) == kResultOk) {
            // Update the corresponding lane's skip overlay on UI thread
            if (lane >= 0 && lane < 6 && step >= 0 && step < 32) {
                handleArpSkipEvent(static_cast<int>(lane), static_cast<int>(step));
            }
        }
        return kResultOk;
    }
    // ... existing message handling ...
}
```

## Pre-allocation Pattern

```cpp
// In Processor::initialize() or setupProcessing():
for (int i = 0; i < 6; ++i) {
    skipMessages_[i] = Steinberg::owned(allocateMessage());
    if (skipMessages_[i]) {
        skipMessages_[i]->setMessageID("ArpSkipEvent");
    }
}
```

## Frequency Estimate

At 200 BPM with 1/32 notes and 4x ratchet with conditions on every step:
- ~200 * 32/4 = 1600 steps/minute = ~27 steps/second
- With ~50% skip probability: ~14 messages/second
- Well within IMessage capacity

## Editor Open/Close Signal

The controller must signal the processor when the editor opens/closes so skip events are only sent when needed:

```cpp
// Controller sends IMessage "EditorState" with int "open" = 0 or 1
// Processor stores this in std::atomic<bool> editorOpen_
```

Note: This pattern may already exist in the codebase for other features (e.g., envelope display). Check existing implementation and reuse.
