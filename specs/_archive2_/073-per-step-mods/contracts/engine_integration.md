# API Contract: Engine Integration for Legato/Slide

**Date**: 2026-02-21

## RuinaeEngine::noteOn() Extension

**File**: `plugins/ruinae/src/engine/ruinae_engine.h`

### Current Signature
```cpp
void noteOn(uint8_t note, uint8_t velocity) noexcept;
```

### New Signature
```cpp
void noteOn(uint8_t note, uint8_t velocity, bool legato = false) noexcept;
```

**Backward compatibility**: The `bool legato = false` defaulted parameter means all existing call sites that pass only (note, velocity) continue to work without modification. Only the arp event routing in processor.cpp explicitly passes the third argument.

### Behavioral Changes

When `legato = false` (default): Current behavior, unchanged.

When `legato = true`:
- **Poly mode** (`mode_ == VoiceMode::Poly`):
  - Find the voice currently playing the previous arp note (search `voices_` for active voice with matching note)
  - Call `voices_[voiceIndex].setFrequency(newFreq)` on that voice (no envelope retrigger)
  - If no matching voice found (defensive), fall back to normal `dispatchPolyNoteOn()`
- **Mono mode** (`mode_ == VoiceMode::Mono`):
  - Route through `monoHandler_.noteOn()` which already handles legato when `legato_` is set
  - The MonoHandler's legato flag should be temporarily activated for this noteOn to suppress retrigger

### New Method: dispatchPolyLegatoNoteOn

The preferred strategy is to find the voice playing the previous arp note by MIDI note
number (from `currentArpNotes_[0]`), not merely the most recently triggered voice globally.
Searching by note number is precise: it avoids gliding the wrong voice in scenarios where
the user plays notes outside the arp path simultaneously. Fall back to timestamp search
only if no voice matches by note number.

```cpp
// prevArpNote: the MIDI note number of the previous arp note (from currentArpNotes_[0])
// newNote: the new target MIDI note
void dispatchPolyLegatoNoteOn(uint8_t prevArpNote, uint8_t newNote, uint8_t velocity) noexcept {
    // Primary: find the voice playing the previous arp note by note number
    int bestVoice = -1;
    for (size_t i = 0; i < polyphonyCount_; ++i) {
        if (voices_[i].isActive() && voices_[i].currentNote() == prevArpNote) {
            bestVoice = static_cast<int>(i);
            break;
        }
    }

    // Fallback: find most recently triggered active voice by timestamp
    if (bestVoice < 0) {
        uint64_t bestTimestamp = 0;
        for (size_t i = 0; i < polyphonyCount_; ++i) {
            if (voices_[i].isActive() && noteOnTimestamps_[i] > bestTimestamp) {
                bestTimestamp = noteOnTimestamps_[i];
                bestVoice = static_cast<int>(i);
            }
        }
    }

    if (bestVoice >= 0) {
        float freq = noteProcessor_.getFrequency(newNote);
        voices_[bestVoice].setFrequency(freq);
        noteOnTimestamps_[bestVoice] = ++timestampCounter_;
    } else {
        // No active voice found, fall back to normal noteOn
        dispatchPolyNoteOn(newNote, velocity);
    }
}
```

**Caller in noteOn()**: When `legato=true` and Poly mode, call
`dispatchPolyLegatoNoteOn(currentArpNotes_[0], note, velocity)` so the previous
arp note number is passed for precise voice matching.

## RuinaeEngine::setPortamentoTime() -- Already Exists

**Current implementation** (line 1271-1274):
```cpp
void setPortamentoTime(float ms) noexcept {
    if (detail::isNaN(ms) || detail::isInf(ms)) return;
    monoHandler_.setPortamentoTime(ms);
}
```

**Required change**: Also set portamento time on each voice for Poly mode:
```cpp
void setPortamentoTime(float ms) noexcept {
    if (detail::isNaN(ms) || detail::isInf(ms)) return;
    monoHandler_.setPortamentoTime(ms);
    // For Poly slide: set on each voice (FR-034)
    for (size_t i = 0; i < kMaxPolyphony; ++i) {
        voices_[i].setPortamentoTime(ms);
    }
}
```

**Note**: This requires RuinaeVoice to gain a `setPortamentoTime(float ms)` method. See voice_portamento section below.

## RuinaeVoice Portamento Extension

**File**: `plugins/ruinae/src/engine/ruinae_voice.h`

### New Method: setPortamentoTime

```cpp
/// @brief Set portamento/glide time for legato pitch transitions.
/// @param ms Duration in milliseconds. 0 = instant.
void setPortamentoTime(float ms) noexcept {
    portamentoTimeMs_ = std::max(0.0f, ms);
}
```

### New Private Members

```cpp
float portamentoTimeMs_{0.0f};   ///< Portamento glide duration (ms)
float portamentoSourceFreq_{0.0f}; ///< Frequency at start of portamento
float portamentoTargetFreq_{0.0f}; ///< Target frequency
float portamentoProgress_{1.0f};   ///< 0.0 = start, 1.0 = complete
```

### Modified Method: setFrequency

When called during a legato transition (portamentoTimeMs_ > 0 and a note is sounding), instead of instantly setting the frequency, initiate a portamento ramp:

```cpp
void setFrequency(float freq) noexcept {
    if (portamentoTimeMs_ > 0.0f && noteFrequency_ > 0.0f) {
        portamentoSourceFreq_ = noteFrequency_;
        portamentoTargetFreq_ = freq;
        portamentoProgress_ = 0.0f;
    }
    noteFrequency_ = freq;
    updateOscFrequencies();
}
```

### Modified Method: processBlock (portamento ramp)

In the per-sample loop, if portamento is active (`portamentoProgress_ < 1.0f`):
```cpp
// Advance portamento
float portaIncrement = 1.0f / (portamentoTimeMs_ * 0.001f * sampleRate_);
portamentoProgress_ = std::min(portamentoProgress_ + portaIncrement, 1.0f);
float currentFreq = portamentoSourceFreq_ *
    std::pow(portamentoTargetFreq_ / portamentoSourceFreq_, portamentoProgress_);
// Use currentFreq for oscillator pitch this sample
```

**Note**: Exponential interpolation between frequencies produces perceptually linear pitch glide.

## Processor Event Routing Change

**File**: `plugins/ruinae/src/processor/processor.cpp`

### Current (line 225-232):
```cpp
for (size_t i = 0; i < numArpEvents; ++i) {
    const auto& evt = arpEvents_[i];
    if (evt.type == Krate::DSP::ArpEvent::Type::NoteOn) {
        engine_.noteOn(evt.note, evt.velocity);
    } else {
        engine_.noteOff(evt.note);
    }
}
```

### New:
```cpp
for (size_t i = 0; i < numArpEvents; ++i) {
    const auto& evt = arpEvents_[i];
    if (evt.type == Krate::DSP::ArpEvent::Type::NoteOn) {
        engine_.noteOn(evt.note, evt.velocity, evt.legato);
    } else {
        engine_.noteOff(evt.note);
    }
}
```

The only change is passing `evt.legato` as the third argument.
