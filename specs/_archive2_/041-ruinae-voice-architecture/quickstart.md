# Quickstart: Ruinae Voice Architecture

## What This Implements

The RuinaeVoice is the complete per-voice processing unit for the Ruinae chaos/spectral hybrid synthesizer. Each voice contains:

- **2 SelectableOscillators** (OSC A + OSC B) with 10 types each
- **Mixer** (CrossfadeMix or SpectralMorph mode)
- **Selectable Filter** (SVF/Ladder/Formant/Comb)
- **Selectable Distortion** (6 types including Clean bypass)
- **TranceGate** (optional rhythmic VCA)
- **VCA** (amplitude envelope)
- **3 ADSR Envelopes** + **1 LFO** + **VoiceModRouter** (per-voice modulation)

## New Files to Create

### Source Files

```
dsp/include/krate/dsp/systems/ruinae_types.h          # Enums (OscType, MixMode, etc.)
dsp/include/krate/dsp/systems/selectable_oscillator.h  # SelectableOscillator class
dsp/include/krate/dsp/systems/voice_mod_router.h       # VoiceModRouter class
dsp/include/krate/dsp/systems/ruinae_voice.h           # RuinaeVoice class
```

### Test Files

```
dsp/tests/unit/systems/selectable_oscillator_test.cpp
dsp/tests/unit/systems/voice_mod_router_test.cpp
dsp/tests/unit/systems/ruinae_voice_test.cpp
```

## Build Commands

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Configure
"$CMAKE" --preset windows-x64-release

# Build DSP tests only
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests

# Run tests
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe
```

## Usage Example

```cpp
#include <krate/dsp/systems/ruinae_voice.h>

Krate::DSP::RuinaeVoice voice;
voice.prepare(44100.0, 512);

// Configure
voice.setOscAType(OscType::PolyBLEP);
voice.setOscBType(OscType::Particle);
voice.setMixMode(MixMode::CrossfadeMix);
voice.setMixPosition(0.5f);
voice.setFilterType(RuinaeFilterType::SVF_LP);
voice.setFilterCutoff(1000.0f);
voice.setDistortionType(RuinaeDistortionType::Clean);

// Play
voice.noteOn(440.0f, 0.8f);

std::array<float, 512> buffer{};
voice.processBlock(buffer.data(), 512);

voice.noteOff();
// Continue processing until voice.isActive() == false
```

## Implementation Order

1. **ruinae_types.h** -- Enums and type definitions (no dependencies)
2. **SelectableOscillator** -- Oscillator wrapper with variant dispatch
3. **VoiceModRouter** -- Modulation routing (standalone, no audio deps)
4. **RuinaeVoice** -- Complete voice composing all sections
