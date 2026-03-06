# IMessage Protocol Contract: DisplayData

## Overview

The processor sends display-relevant data to the controller via the VST3 `IMessage` mechanism. This enables the UI to visualize real-time harmonic analysis state without violating the processor/controller separation.

## Message Specification

### Message ID

`"DisplayData"`

### Direction

Processor -> Controller (one-way)

### Payload Format

Binary blob via `IAttributeList::setBinary("data", ptr, size)`.

The blob is a `DisplayData` struct (POD, no pointers, no dynamic allocation):

```cpp
namespace Innexus {
struct DisplayData {
    float partialAmplitudes[48];     // Linear amplitudes [0.0, ~1.0]
    uint8_t partialActive[48];       // 1 = active, 0 = filtered/attenuated
    float f0;                        // Fundamental frequency (Hz), 0 if none
    float f0Confidence;              // [0.0, 1.0]
    uint8_t slotOccupied[8];         // 1 = memory slot occupied
    float evolutionPosition;         // Combined morph position [0.0, 1.0]
    float manualMorphPosition;       // Manual knob value [0.0, 1.0]
    float mod1Phase;                 // LFO phase [0.0, 1.0]
    float mod2Phase;                 // LFO phase [0.0, 1.0]
    bool mod1Active;                 // Modulator 1 enabled & depth > 0
    bool mod2Active;                 // Modulator 2 enabled & depth > 0
    uint32_t frameCounter;           // Monotonic, incremented per new frame
};
} // namespace Innexus
```

### Send Frequency

- Sent at end of `Processor::process()` when new harmonic frame data is available
- Typically at the analysis hop rate (~86 times/sec at 44.1kHz, 512-sample hop)
- NOT sent during silence or when no analysis is running
- The controller's 30ms timer naturally downsamples to ~33fps for display

### Receive Handling

```
Controller::notify(IMessage* message):
1. Check message ID == "DisplayData"
2. Get binary attribute "data"
3. Validate size == sizeof(DisplayData)
4. memcpy to cachedDisplayData_
5. (Timer callback handles view updates)
```

### Existing IMessage Usage

The Innexus controller already handles `notify()` for JSON snapshot import (M5). The display data message is a new message ID that coexists with the existing handler.

## Error Handling

- If `setBinary` size doesn't match `sizeof(DisplayData)`, the controller ignores the message
- If the controller is not connected (editor closed), `sendMessage()` returns `kResultFalse` but the processor continues without error
- `frameCounter` overflow wraps naturally (uint32_t); the controller only checks inequality
