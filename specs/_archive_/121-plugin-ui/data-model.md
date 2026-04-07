# Data Model: Innexus Plugin UI (121-plugin-ui)

## DisplayData (New Struct)

**Location**: `plugins/innexus/src/controller/display_data.h`
**Namespace**: `Innexus`
**Purpose**: POD struct for processor-to-controller display data transfer via IMessage.

| Field | Type | Description |
|-------|------|-------------|
| `partialAmplitudes[48]` | `float[48]` | Linear amplitude per partial (index 0-47) |
| `partialActive[48]` | `uint8_t[48]` | 1 = active, 0 = attenuated by harmonic filter |
| `f0` | `float` | Detected fundamental frequency (Hz) |
| `f0Confidence` | `float` | Pitch tracking confidence [0.0, 1.0] |
| `slotOccupied[8]` | `uint8_t[8]` | Memory slot occupied status (1 = occupied) |
| `evolutionPosition` | `float` | Combined morph position [0.0, 1.0] |
| `manualMorphPosition` | `float` | Manual morph offset for ghost marker |
| `mod1Phase` | `float` | Modulator 1 current LFO phase [0.0, 1.0] |
| `mod2Phase` | `float` | Modulator 2 current LFO phase [0.0, 1.0] |
| `mod1Active` | `bool` | Modulator 1 enabled and running |
| `mod2Active` | `bool` | Modulator 2 enabled and running |
| `frameCounter` | `uint32_t` | Monotonic counter for change detection |

**Validation**: `frameCounter` must be strictly increasing. All float fields must be finite.

**Size**: ~278 bytes. Trivially fits in IMessage binary payload.

## Custom View Classes (New)

### HarmonicDisplayView

**Location**: `plugins/innexus/src/controller/views/harmonic_display_view.h/.cpp`
**Base Class**: `VSTGUI::CView`
**Namespace**: `Innexus`

**State**:
| Field | Type | Description |
|-------|------|-------------|
| `amplitudes_[48]` | `float[48]` | Cached linear amplitudes |
| `active_[48]` | `bool[48]` | Active/attenuated state per partial |
| `hasData_` | `bool` | Whether any analysis data has been received |

**Methods**:
- `void updateData(const DisplayData& data)` - Copy relevant fields, call `invalid()`
- `void draw(CDrawContext* context)` override - Draw 48 vertical bars
- Private: `float amplitudeToBarHeight(float amp, float viewHeight)` - dB conversion

### ConfidenceIndicatorView

**Location**: `plugins/innexus/src/controller/views/confidence_indicator_view.h/.cpp`
**Base Class**: `VSTGUI::CView`
**Namespace**: `Innexus`

**State**:
| Field | Type | Description |
|-------|------|-------------|
| `confidence_` | `float` | Current F0 confidence [0.0, 1.0] |
| `f0_` | `float` | Detected F0 frequency (Hz) |

**Methods**:
- `void updateData(const DisplayData& data)` - Copy f0/confidence, call `invalid()`
- `void draw(CDrawContext* context)` override - Draw horizontal bar + note text
- Private: `std::string freqToNoteName(float freq)` - Convert Hz to note name
- Private: `CColor getConfidenceColor(float confidence)` - Zone-based color

### MemorySlotStatusView

**Location**: `plugins/innexus/src/controller/views/memory_slot_status_view.h/.cpp`
**Base Class**: `VSTGUI::CView`
**Namespace**: `Innexus`

**State**:
| Field | Type | Description |
|-------|------|-------------|
| `slotOccupied_[8]` | `bool[8]` | Occupied state per slot |

**Methods**:
- `void updateData(const DisplayData& data)` - Copy slot status, call `invalid()`
- `void draw(CDrawContext* context)` override - Draw 8 circles (filled=occupied, hollow=empty)

### EvolutionPositionView

**Location**: `plugins/innexus/src/controller/views/evolution_position_view.h/.cpp`
**Base Class**: `VSTGUI::CView`
**Namespace**: `Innexus`

**State**:
| Field | Type | Description |
|-------|------|-------------|
| `position_` | `float` | Combined morph position [0.0, 1.0] |
| `manualPosition_` | `float` | Manual offset for ghost marker |
| `showGhost_` | `bool` | Whether to show ghost marker (evolution active) |

**Methods**:
- `void updateData(const DisplayData& data, bool evolutionActive)` - Copy positions
- `void draw(CDrawContext* context)` override - Draw track + playhead + optional ghost

### ModulatorActivityView

**Location**: `plugins/innexus/src/controller/views/modulator_activity_view.h/.cpp`
**Base Class**: `VSTGUI::CView`
**Namespace**: `Innexus`

**State**:
| Field | Type | Description |
|-------|------|-------------|
| `phase_` | `float` | Current LFO phase [0.0, 1.0] |
| `active_` | `bool` | Whether modulator is active |
| `modIndex_` | `int` | 0 or 1 (set by sub-controller verifyView) |

**Methods**:
- `void updateData(float phase, bool active)` - Update phase/active, call `invalid()`
- `void draw(CDrawContext* context)` override - Draw pulsing circle or waveform preview

## ModulatorSubController (New)

**Location**: `plugins/innexus/src/controller/modulator_sub_controller.h`
**Base Class**: `VSTGUI::DelegationController`
**Namespace**: `Innexus`

**State**:
| Field | Type | Description |
|-------|------|-------------|
| `modIndex_` | `int` | 0 or 1 |

**Methods**:
- `int32_t getTagForName(UTF8StringPtr name, int32_t registeredTag) const override` - Remap generic "Mod.X" tags to modulator-specific parameter IDs
- `CView* verifyView(CView* view, ...) override` - Tag ModulatorActivityView with modIndex

## Existing Entities Referenced (Not Modified)

| Entity | Location | Usage |
|--------|----------|-------|
| `Krate::DSP::HarmonicFrame` | `dsp/include/krate/dsp/processors/harmonic_types.h` | Source for partial amplitudes in processor |
| `Krate::DSP::Partial` | Same file | Individual partial data |
| `Krate::DSP::MemorySlot` | `dsp/include/krate/dsp/processors/harmonic_snapshot.h` | Source for slot occupied status |
| `Innexus::Processor` | `plugins/innexus/src/processor/processor.h` | Extended with `sendDisplayData()` |
| `Innexus::Controller` | `plugins/innexus/src/controller/controller.h` | Extended with VST3EditorDelegate |

## State Transitions

### Custom View Data Flow

```
Processor::process()
    |
    v
Processor populates DisplayData from morphedFrame_, memorySlots_, etc.
    |
    v
Processor::sendDisplayData() via IMessage("DisplayData")
    |
    v
Controller::notify() receives IMessage, copies to cachedDisplayData_
    |
    v
CVSTGUITimer fires every 30ms
    |
    v
Controller::onDisplayTimerFired() checks frameCounter
    |
    v
Calls updateData() on each custom view -> view calls invalid() -> draw()
```

### Freeze State Display

| Freeze State | Spectral Display Behavior |
|---|---|
| Off (0.0) | Continuously updates from live analysis |
| On (1.0) | Shows frozen frame; stops updating from live data |

The processor handles freeze logic; the controller simply displays whatever `morphedFrame_` contains.

### Memory Slot Status

| Slot State | Visual |
|---|---|
| Empty | Hollow circle (stroke only) |
| Occupied | Filled circle (solid) |
