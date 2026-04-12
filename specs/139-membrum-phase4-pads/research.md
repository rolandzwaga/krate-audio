# Research: Membrum Phase 4 -- 32-Pad Layout, Per-Pad Presets, Kit Presets, Separate Outputs

**Date**: 2026-04-12

## R1: VST3 Multi-Bus Output Implementation

### Decision: Use `addAudioOutput()` in `initialize()` to declare 16 stereo buses

### Rationale

The VST3 SDK's `AudioEffect` base class (which `Processor` extends) provides:
- `addAudioOutput(name, speakerArr, busType, flags)` to declare output buses
- The first output is `kMain` by default; subsequent calls create `kAux` buses
- The host calls `activateBus(kAudio, kOutput, index, state)` to activate/deactivate aux buses
- The base `Component::activateBus()` implementation updates `Bus::setActive()` -- we override it to track active buses for the audio thread
- In `process()`, `data.numOutputs` tells us how many output buses are present; each `data.outputs[i]` provides buffer pointers and `silenceFlags`
- Inactive buses may have `data.outputs[i].channelBuffers32` set to nullptr or may not appear at all depending on the host -- must check both `numOutputs` and `isActive()`

### Pattern (from SDK source analysis)

```cpp
// In initialize():
addAudioOutput(STR16("Main Out"), SpeakerArr::kStereo);        // bus 0: kMain, default active
for (int i = 1; i < kMaxOutputBuses; ++i) {
    char16_t name[32];
    // format "Aux N"
    addAudioOutput(name, SpeakerArr::kStereo, BusTypes::kAux, 0);  // default inactive
}

// Override activateBus to track bus activation state:
tresult PLUGIN_API activateBus(MediaType type, BusDirection dir, int32 index, TBool state) override {
    auto result = AudioEffect::activateBus(type, dir, index, state);
    if (result == kResultTrue && type == kAudio && dir == kOutput) {
        busActive_[index] = (state != 0);
    }
    return result;
}

// In process():
for each voice:
    render to scratch
    accumulate to outputs[0] (main -- always)
    if pad's outputBus > 0 && busActive_[outputBus] && outputBus < data.numOutputs:
        accumulate to outputs[outputBus]
```

### Alternatives Considered

1. **Dynamic bus creation**: Not supported in VST3 -- all buses must be declared at `initialize()` time
2. **Fewer auxiliary buses (e.g., 8)**: Would limit professional use. 16 (1 main + 15 aux) matches Battery, Kontakt, and industry standard
3. **Single bus with channel pairs**: Would require non-standard host configuration. Multi-bus is the standard approach

### Key Finding

The `activateBus()` override is essential. The base class updates the Bus object, but the audio thread needs a simple boolean array lookup (no virtual calls or pointer chasing in the hot path). The processor maintains `std::array<bool, 16> busActive_` (initialized to `{true, false, false, ...}`) and updates it from `activateBus()`. This is safe because `activateBus()` is called from the component thread (not the audio thread) and the bus activation state is read by the audio thread -- the VST3 host guarantees these do not overlap with `process()` calls.

---

## R2: Per-Pad Parameter Dispatch Pattern (Selected Pad Proxy)

### Decision: Global parameter IDs 100-252 act as "selected pad proxy" parameters in the controller; per-pad parameter IDs 1000-3047 are the canonical storage in the processor

### Rationale

The spec requires (FR-012, FR-013, FR-091):
- Per-pad parameters exist at computed IDs: `1000 + padIndex * 64 + offset`
- Global IDs (100-252) serve as "proxy" parameters that redirect to the currently selected pad
- When `kSelectedPadId` changes, the controller pushes the new pad's values into the global parameter IDs
- When a global parameter changes, the controller forwards it to the selected pad's per-pad parameter ID

This is a controller-only concern. The processor stores per-pad data in `PadConfig[32]` and dispatches parameter changes by computing `padIndex = (paramId - kPadBaseId) / kPadParamStride` and `offset = (paramId - kPadBaseId) % kPadParamStride`.

### Pattern

```
Controller flow:
1. Host changes kSelectedPadId (260) to value N
2. Controller::setParamNormalized(kSelectedPadId, N/31.0) is called
3. For each global param (kMaterialId, kSizeId, etc.):
   - Read padConfigs_[N]'s corresponding value
   - Call setParamNormalized(globalParamId, padN_value)
   - This updates the host-generic editor to show pad N's values

4. Host changes global param (e.g., kMaterialId = 100) to value V
5. Controller::performEdit(kMaterialId, V)
6. Controller also calls performEdit(padParamId, V) where padParamId = 1000 + selectedPad * 64 + 2 (material offset)
7. Processor receives the per-pad parameter change and updates padConfigs_[selectedPad].material

Processor flow:
1. Receives parameter change for ID in [1000, 3047]
2. Computes padIndex = (id - 1000) / 64, offset = (id - 1000) % 64
3. Updates padConfigs_[padIndex].fields[offset]
4. Does NOT touch currently sounding voices (parameter takes effect on next noteOn)
```

### Alternatives Considered

1. **Processor-side proxy**: Processor maps global IDs to per-pad. Rejected because the processor should only deal with per-pad IDs; the proxy is a UI convenience
2. **No proxy (expose all 1024 params only)**: Host-generic editor would be unusable with 1024+ parameters. The proxy makes the existing ~30 parameter editing experience work for whichever pad is selected
3. **IMessage-based pad selection**: Overkill; a simple parameter (`kSelectedPadId`) suffices and round-trips with DAW state

### Key Finding

The critical subtlety is that when `kSelectedPadId` changes, the controller must push ~30 parameter value updates. The spec explicitly says no batch suppression is needed -- hosts handle rapid sequential `setParamNormalized()` calls correctly.

---

## R3: Kit Preset and Per-Pad Preset File Formats

### Decision: Kit presets reuse the full plugin state format (identical to getState/setState binary stream). Per-pad presets use a compact binary format containing one pad's 30 sound parameters + exciter type + body model.

### Rationale

The existing `PresetManager` in `plugins/shared/src/preset/preset_manager.h` uses:
- `StateProvider`: a callback returning an `IBStream*` containing the full plugin state
- `LoadProvider`: a callback receiving an `IBStream*` and a `PresetInfo`
- `PresetManagerConfig`: provides processor UID, plugin name, subcategory names

For **kit presets** (FR-050--FR-053):
- The kit preset IS the plugin state minus `selectedPadIndex`
- The `StateProvider` callback writes the same v4 binary format as `getState()` but omits the `selectedPadIndex` field at the end
- The `LoadProvider` callback reads the v4 binary format and applies it, but does not modify `selectedPadIndex`
- Subcategories: "Electronic", "Acoustic", "Experimental", "Cinematic"

For **per-pad presets** (FR-060--FR-063):
- A second `PresetManager` instance (or `PadPresetManager`) with its own config
- Subcategories: "Kick", "Snare", "Tom", "Hat", "Cymbal", "Perc", "Tonal", "808", "FX"
- Binary format: version header (int32) + exciter type (int32) + body model (int32) + 30 float64 values (sound params only, no choke group, no output bus)
- Load applies to the currently selected pad only

### Alternatives Considered

1. **JSON or XML format**: Rejected -- binary matches the existing IBStream pattern, is simpler to implement, and avoids text parsing in the controller
2. **Single PresetManager with type discriminator**: Possible but messier. Two separate PresetManager instances with different configs is cleaner and matches the existing pattern
3. **Shared preset directory**: Rejected -- kit presets in `Kit Presets/` and pad presets in `Pad Presets/` keeps them clearly separated

### Key Finding

The `PresetManagerConfig` struct has `std::vector<std::string> subcategoryNames` which maps directly to subdirectories. The `PresetManager` constructor takes a processor UID for `.vstpreset` file identification. For per-pad presets, we can use the same processor UID (the preset is still "Membrum" data) but a different `pluginName` directory segment (e.g., "Membrum/Pad Presets" vs "Membrum/Kit Presets") or we handle it with a custom path prefix in the config.

---

## R4: State v4 Binary Format and Migration

### Decision: v4 state format writes all 32 pad configs sequentially, followed by `selectedPadIndex`. v3-to-v4 migration applies the single shared config to pad 0 and initializes pads 1-31 with GM defaults.

### Rationale

Current v3 state layout (302 bytes):
- int32 version (=3)
- 5 x float64 Phase 1 params (40 bytes)
- int32 exciterType + int32 bodyModel (8 bytes)
- 27 x float64 Phase 2 continuous params (216 bytes)
- uint8 maxPolyphony + uint8 voiceStealingPolicy (2 bytes)
- 32 x uint8 chokeGroupAssignments (32 bytes)

Proposed v4 state layout:
- int32 version (=4)
- int32 maxPolyphony
- int32 voiceStealingPolicy
- For each of 32 pads:
  - int32 exciterType
  - int32 bodyModel
  - 30 x float64 sound params (material, size, decay, strikePos, level, toneShaper x10, unnatural x4, morph x5, FM ratio, feedback amount, noise burst dur, friction pressure) -- 240 bytes per pad
  - uint8 chokeGroup
  - uint8 outputBus
- int32 selectedPadIndex

Total: 4 + 4 + 4 + 32 * (4 + 4 + 240 + 1 + 1) + 4 = 8012 bytes

v3-to-v4 migration:
1. Read v3 blob as before (reuse existing code)
2. Map the single shared config to pad 0's PadConfig
3. Initialize pads 1-31 with GM-inspired defaults per FR-030/FR-031
4. Set all output buses to 0 (main only)
5. Set selectedPadIndex to 0

### Alternatives Considered

1. **Variable-length format**: Only write non-default pads. Rejected -- adds complexity for minimal space savings (8 KB is negligible)
2. **JSON state**: Rejected -- binary consistency with v1-v3 and performance

---

## R5: AU Configuration for Multi-Output

### Decision: Update `au-info.plist` and `audiounitconfig.h` to declare multi-output capability

### Rationale

From the memory entry about AU wrapper / macOS CI, the `au-info.plist` must declare supported channel configurations and `audiounitconfig.h` must update `kSupportedNumChannels`.

For 16 stereo outputs (0 inputs, 32 output channels total), the AU wrapper needs:
- Multiple output elements, one per bus
- The plist must declare supported configurations

However, AU multi-output for instruments is complex. The VST3-to-AU wrapper handles this by creating AU IO elements for each VST3 bus. The critical thing is that the base config must declare at least the main output (0 in / 2 out), and the AU host handles aux bus activation.

The plist should declare: `0 in / 2 out` (minimum -- main only). The AU wrapper will create additional output elements based on the VST3 bus declarations. Hosts like Logic Pro support multi-output instruments natively and will activate buses as needed.

### Key Finding

For AU multi-output instruments, the `kSupportedNumChannels` packed format in `audiounitconfig.h` needs to include the main configuration. The wrapper creates output elements based on `addAudioOutput()` calls. The key is not to declare all 16 x 2 = 32 channels in a single config line but to let the wrapper handle the per-bus setup. The plist `0 in / 2 out` config stays as the minimum; Logic detects the additional outputs from the wrapper.

---

## R6: Default Kit Template Parameter Values

### Decision: Six template types (Kick, Snare, Tom, Hat, Cymbal, Perc) with GM-standard mappings

### Rationale

The spec (FR-030--FR-033) defines exact template mappings. The templates differ in:
- Exciter type (Impulse, Mallet, NoiseBurst)
- Body model (Membrane, Plate, NoiseBody)
- Material, Size, Decay, Strike Position values
- Pitch envelope settings (especially for kicks)
- Choke group assignments (hats in group 1)

These are applied at initialization time and stored in a constexpr lookup table or an initialization function. The GM drum map reference table in the spec maps each of the 32 pads to one of these templates.

### Key Data

Tom size progression (FR-033):
- High Tom (MIDI 50, pad 15): Size=0.4
- Hi-Mid Tom (MIDI 48, pad 13): Size=0.45
- Low-Mid Tom (MIDI 47, pad 12): Size=0.5
- Low Tom (MIDI 45, pad 10): Size=0.6
- High Floor Tom (MIDI 43, pad 8): Size=0.7
- Low Floor Tom (MIDI 41, pad 6): Size=0.8

Hi-hat choke group (FR-032):
- Closed Hi-Hat (MIDI 42, pad 7): choke group 1
- Pedal Hi-Hat (MIDI 44, pad 9): choke group 1
- Open Hi-Hat (MIDI 46, pad 11): choke group 1
- All others: choke group 0 (no choke)
