# VST3 Interface Contract: Membrum Phase 1

## Plugin Identity

| Property | Value |
|----------|-------|
| Plugin Name | Membrum |
| Subcategories | `Instrument\|Drum` |
| Processor FUID | `0x4D656D62, 0x72756D50, 0x726F6331, 0x00000136` |
| Controller FUID | `0x4D656D62, 0x72756D43, 0x74726C31, 0x00000136` |
| AU Type | `aumu` |
| AU Subtype | `Mbrm` |
| AU Manufacturer | `KrAt` |
| Audio Inputs | 0 |
| Audio Outputs | 1 stereo bus |
| Event Inputs | 1 (MIDI) |

## Parameter Contract

| ParamID | Name | Min | Max | Default | Step Count | Unit |
|---------|------|-----|-----|---------|------------|------|
| 100 | Material | 0.0 | 1.0 | 0.5 | 0 | -- |
| 101 | Size | 0.0 | 1.0 | 0.5 | 0 | -- |
| 102 | Decay | 0.0 | 1.0 | 0.3 | 0 | -- |
| 103 | Strike Position | 0.0 | 1.0 | 0.3 | 0 | -- |
| 104 | Level | 0.0 | 1.0 | 0.8 | 0 | dB |

All parameters are continuous (stepCount=0), normalized 0.0-1.0 at the VST boundary.

## State Contract

Binary format, version-tagged:
1. Read/write `int32` version (currently 1)
2. Read/write 5x `float64` normalized parameter values in order: Material, Size, Decay, StrikePosition, Level
3. On load: unknown future fields after the known 5 are silently ignored (forward compat)
4. On load: if fewer than 5 fields, use defaults for missing params

## MIDI Contract

Raw MIDI velocity (0–127) is normalized to [0.0, 1.0] (velocity / 127.0f) before being passed to DrumVoice::noteOn().

| Event | Behavior |
|-------|----------|
| Note On, note=36, velocity>0 | Trigger single drum voice (velocity normalized to [0,1]) |
| Note On, note=36, velocity=0 | Treat as Note Off |
| Note Off, note=36 | Release ADSR envelope (natural decay) |
| Note On, note!=36 | Ignored silently |
| Note Off, note!=36 | Ignored silently |
| Rapid retrigger (note 36) | Restart voice (no voice accumulation) |

## Bus Configuration

```cpp
// In Processor::initialize():
addEventInput(STR16("Event In"));
addAudioOutput(STR16("Stereo Out"), Steinberg::Vst::SpeakerArr::kStereo);
// NO addAudioInput -- instrument with 0 audio inputs
```

## Process Contract

- `process()` handles `data.numInputs == 0` (no audio input bus)
- `process()` handles `data.numSamples == 0` (zero-length blocks)
- Output is mono voice duplicated to both stereo channels
- No latency reported (0 samples)
- No tail time reported (default)
