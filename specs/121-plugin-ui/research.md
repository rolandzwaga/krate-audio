# Research: Innexus Plugin UI (121-plugin-ui)

## R1: IMessage Protocol for Processor-to-Controller Display Data

**Decision**: Use a flat POD struct (`DisplayData`) sent via `IMessage::getAttributes()->setBinary()`. The processor sends display data at the end of `process()` when the harmonic frame changes. The controller's `notify()` copies it to a local buffer.

**Rationale**: The display data is small (~250 bytes: 48 floats + metadata). Copying via IMessage is simpler and safer than shared memory pointers (the Disrumpo pattern). No lifetime coupling between processor and controller.

**Alternatives considered**:
1. **Shared FIFO pointer via IMessage** (Disrumpo pattern): More complex. Requires careful lifetime management. The processor sends a raw pointer to a lock-free FIFO; the controller reads from it. Works well for high-bandwidth data (full spectrum bins at 30fps) but overkill for 48 amplitude values.
2. **Abuse VST3 parameters for display data**: Would require 50+ display-only parameters. The host would record them in automation lanes, wasting resources and polluting presets. Rejected.
3. **Atomic double-buffer without IMessage**: Processor writes to buffer A, controller reads buffer B, swap via atomic flag. Bypasses IMessage entirely. Simpler but violates VST3 architecture principle (Processor and Controller must communicate only via host-mediated channels: IMessage, state, parameters).

**DisplayData Struct**:
```cpp
namespace Innexus {
struct DisplayData {
    float partialAmplitudes[48];     // Linear amplitudes for spectral display
    uint8_t partialActive[48];       // 1 = active (not filtered), 0 = attenuated
    float f0;                        // Fundamental frequency (Hz)
    float f0Confidence;              // [0.0, 1.0]
    uint8_t slotOccupied[8];         // Memory slot status (1 = occupied)
    float evolutionPosition;         // Combined morph position [0.0, 1.0]
    float manualMorphPosition;       // Manual offset for ghost marker
    float mod1Phase;                 // Modulator 1 current phase [0.0, 1.0]
    float mod2Phase;                 // Modulator 2 current phase [0.0, 1.0]
    bool mod1Active;                 // Modulator 1 enabled & producing output
    bool mod2Active;                 // Modulator 2 enabled & producing output
    uint32_t frameCounter;           // Monotonic counter; controller compares to detect new data
};
} // namespace Innexus
```

**Size**: 48*4 + 48 + 4 + 4 + 8 + 4 + 4 + 4 + 4 + 1 + 1 + 4 = 278 bytes. Trivial for IMessage.

**Send frequency**: Only when `morphedFrame_` changes (typically at analysis hop rate: ~86 hops/sec at 44.1kHz with 512 hop). No display data sent during silence.

## R2: CVSTGUITimer Pattern for Custom View Updates

**Decision**: Single shared `CVSTGUITimer` created in `didOpen()`, stopped in `willClose()`. 30ms interval (~33fps). Timer callback checks `frameCounter` and calls `invalid()` on views with new data.

**Rationale**: One timer is simpler than per-view timers and ensures consistent frame rate. Disrumpo uses this pattern for sweep visualization. 33fps exceeds SC-003 (>=10fps) with 3x headroom.

**Alternatives considered**:
1. **Per-view timers**: Each custom view creates its own timer. More independent but wastes resources (5 timer callbacks per 30ms instead of 1).
2. **IDependent on parameter change**: Would couple repaint to parameter automation rate. When parameters change rapidly (smooth knob turns), views would repaint hundreds of times per second. Timer decouples repaint from data rate.

**Implementation Pattern**:
```cpp
// In didOpen():
displayTimer_ = VSTGUI::makeOwned<CVSTGUITimer>(
    [this](CVSTGUITimer*) { onDisplayTimerFired(); }, 30);

void Controller::onDisplayTimerFired() {
    if (lastDisplayCounter_ == cachedDisplayData_.frameCounter) return;
    lastDisplayCounter_ = cachedDisplayData_.frameCounter;

    if (harmonicDisplay_) { harmonicDisplay_->updateData(cachedDisplayData_); }
    if (confidenceIndicator_) { confidenceIndicator_->updateData(cachedDisplayData_); }
    if (memorySlotStatus_) { memorySlotStatus_->updateData(cachedDisplayData_); }
    if (evolutionPosition_) { evolutionPosition_->updateData(cachedDisplayData_); }
    // Modulator views updated similarly
}

// In willClose():
if (displayTimer_) { displayTimer_->stop(); displayTimer_ = nullptr; }
```

## R3: Modulator Sub-Controller for Template Reuse (FR-046)

**Decision**: `ModulatorSubController` extends `VSTGUI::DelegationController`. Uses an `int modIndex_` (0 or 1) to remap generic template tags to Mod1 or Mod2 parameter IDs. Counter-based instantiation in `createSubController()`.

**Rationale**: Direct parallel to Disrumpo's `BandSubController`. The offset between Mod1 and Mod2 parameter IDs is exactly 10 (610-619 vs 620-629), making the mapping trivial: `baseId + modIndex_ * 10`.

**Alternatives considered**:
1. **Duplicate XML**: Write identical XML twice with different tags. Violates FR-046 (SC-007). Doubles maintenance burden.
2. **Programmatic template instantiation**: Create modulator panels from C++ code. More flexible but harder to adjust layout without recompiling.

**Tag Mapping Table**:
| Template Tag | Formula | Mod 0 | Mod 1 |
|---|---|---|---|
| `Mod.Enable` | `kMod1EnableId + modIndex_ * 10` | 610 | 620 |
| `Mod.Waveform` | `kMod1WaveformId + modIndex_ * 10` | 611 | 621 |
| `Mod.Rate` | `kMod1RateId + modIndex_ * 10` | 612 | 622 |
| `Mod.Depth` | `kMod1DepthId + modIndex_ * 10` | 613 | 623 |
| `Mod.RangeStart` | `kMod1RangeStartId + modIndex_ * 10` | 614 | 624 |
| `Mod.RangeEnd` | `kMod1RangeEndId + modIndex_ * 10` | 615 | 625 |
| `Mod.Target` | `kMod1TargetId + modIndex_ * 10` | 616 | 626 |

## R4: Spectral Display Drawing Strategy (FR-009..FR-012)

**Decision**: Draw 48 vertical bars with logarithmic (dB) height mapping. Active partials in accent color (cyan/teal), attenuated partials in dimmed color (dark gray). Empty state shows centered "No analysis data" text.

**Rationale**: Logarithmic (dB) scale is standard for spectral displays and matches spec clarification. The -60dB to 0dB range provides good dynamic range without showing noise floor.

**dB Conversion**:
```cpp
float amplitudeToBarHeight(float linearAmplitude, float viewHeight) {
    if (linearAmplitude <= 0.001f) return 0.0f;  // Below -60dB floor
    float dB = 20.0f * std::log10f(linearAmplitude);
    float normalized = (dB + 60.0f) / 60.0f;     // Map [-60, 0] to [0, 1]
    return std::clamp(normalized, 0.0f, 1.0f) * viewHeight;
}
```

**Bar Layout**: 48 bars across view width. Each bar width = `(viewWidth - padding) / 48`. Small gap between bars for visual clarity.

## R5: F0 Confidence Color Scheme (FR-014)

**Decision**: Three-zone color coding with smooth interpolation:
- Green (#4CAF50): confidence > 0.7
- Yellow (#FFC107): confidence 0.3-0.7
- Red (#F44336): confidence < 0.3

Use `Krate::Plugins::lerpColor()` for smooth transitions between zones.

## R6: Note Name from Frequency (FR-015)

**Decision**: Inline utility in `ConfidenceIndicatorView`:
```cpp
std::string freqToNoteName(float freq) {
    if (freq <= 0.0f) return "";
    float midiNote = 12.0f * std::log2f(freq / 440.0f) + 69.0f;
    int noteIndex = static_cast<int>(std::roundf(midiNote)) % 12;
    int octave = static_cast<int>(std::roundf(midiNote)) / 12 - 1;
    static const char* noteNames[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    return std::string(noteNames[noteIndex]) + std::to_string(octave);
}
```

## R7: Evolution Position Indicator Drawing (FR-036)

**Decision**: Horizontal track with vertical playhead line. Track is a thin rounded rectangle. Playhead is a solid vertical line at `x = position * trackWidth`. Ghost marker (when evolution active) is a semi-transparent vertical line at the manual morph position.

**Colors**: Track background: dark gray. Playhead: bright accent (cyan). Ghost marker: same color at 30% opacity.

## R8: Blend Section Enable/Disable Visual State (FR-052)

**Decision**: When Blend is disabled, slot weight controls are visually dimmed by reducing the `ArcKnob` arc color alpha. This is achieved via an IDependent controller that watches `kBlendEnableId` and updates the views' opacity/interactivity.

**Note**: VSTGUI controls bound to parameters remain functional even when visually dimmed. The spec says "SHOULD appear visually dimmed", not "MUST be disabled". We dim the visual but do not block interaction (the parameter still works, the engine just ignores weights when blend is off).
