# Disrumpo - VSTGUI Implementation Specification

**Related Documents:**
- [specs-overview.md](specs-overview.md) - Core requirements specification
- [plans-overview.md](plans-overview.md) - System architecture overview
- [ui-mockups.md](ui-mockups.md) - UI layout specifications
- [dsp-details.md](dsp-details.md) - DSP and parameter ID details
- [custom-controls.md](custom-controls.md) - Custom VSTGUI control specifications
- [roadmap.md](roadmap.md) - Development schedule

---

## 1. Plugin Identifiers

### 1.1 Component FUIDs

```cpp
// plugins/Disrumpo/src/plugin_ids.h
#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace Disrumpo {

// Processor Component FUID (GENERATE NEW GUIDs - DO NOT REUSE!)
// Generate at: https://www.guidgenerator.com/
static const Steinberg::FUID kProcessorUID(
    0xAAAAAAAA, 0xBBBBBBBB, 0xCCCCCCCC, 0xDDDDDDDD);

// Controller Component FUID
static const Steinberg::FUID kControllerUID(
    0x11111111, 0x22222222, 0x33333333, 0x44444444);

// VST3 Sub-categories
constexpr const char* kSubCategories = "Fx|Distortion";

} // namespace Disrumpo
```

### 1.2 Parameter ID Range Allocation

```cpp
// Parameter ID ranges - 100 ID gaps for future expansion
//
// ENCODING SCHEME (from dsp-details.md):
//   Global parameters:     0x0F00-0x0F0F
//   Sweep parameters:      0x0E00-0x0E0F
//   Per-band parameters:   makeBandParamId(band, param)
//   Per-node parameters:   makeNodeParamId(band, node, param)
//
// DECIMAL RANGE ALLOCATION (for clarity in uidesc control-tags):
//   0-99:       Global parameters
//   100-199:    Sweep parameters
//   200-299:    Modulation sources (LFO1, LFO2, Env, etc.)
//   300-399:    Modulation routing matrix
//   400-499:    Macro parameters
//   1000-1799:  Band 0 parameters (100 per band × 8 bands)
//   1800-2599:  Band 1 parameters
//   ...         (continue pattern)
//   8200-8999:  Band 7 parameters
//   9000-9999:  UI-only tags (visibility controllers, labels)

enum GlobalParameterIDs : Steinberg::Vst::ParamID {
    // Global (0-99)
    kInputGainId        = 0,
    kOutputGainId       = 1,
    kGlobalMixId        = 2,
    kBandCountId        = 3,
    kOversampleMaxId    = 4,

    // Sweep (100-199)
    kSweepEnableId      = 100,
    kSweepFrequencyId   = 101,
    kSweepWidthId       = 102,
    kSweepIntensityId   = 103,
    kSweepMorphLinkId   = 104,
    kSweepFalloffId     = 105,

    // LFO 1 (200-219)
    kLFO1RateId         = 200,
    kLFO1ShapeId        = 201,
    kLFO1PhaseId        = 202,
    kLFO1SyncId         = 203,
    kLFO1NoteValueId    = 204,
    kLFO1UnipolarId     = 205,

    // LFO 2 (220-239)
    kLFO2RateId         = 220,
    kLFO2ShapeId        = 221,
    kLFO2PhaseId        = 222,
    kLFO2SyncId         = 223,
    kLFO2NoteValueId    = 224,
    kLFO2UnipolarId     = 225,

    // Envelope Follower (240-259)
    kEnvFollowerAttackId    = 240,
    kEnvFollowerReleaseId   = 241,
    kEnvFollowerSensitivityId = 242,

    // Random (260-279)
    kRandomRateId       = 260,
    kRandomSmoothnessId = 261,
    kRandomSyncId       = 262,

    // Chaos Modulation (280-299)
    kChaosModelId       = 280,
    kChaosSpeedId       = 281,
    kChaosCouplingId    = 282,

    // Macros (400-419)
    kMacro1Id           = 400,
    kMacro2Id           = 401,
    kMacro3Id           = 402,
    kMacro4Id           = 403,

    // Modulation Routing (300-399)
    // Each routing: source, dest, amount, curve = 4 IDs
    // 32 routings × 4 = 128 IDs (300-427)
    kModRouting0SourceId = 300,
    kModRouting0DestId   = 301,
    kModRouting0AmountId = 302,
    kModRouting0CurveId  = 303,
    // ... continue for 32 routings

    kNumParameters = 10000
};

// Per-band parameter offsets (add to band base: 1000 + band*100)
enum BandParameterOffsets : uint8_t {
    kBandGainOffset     = 0,
    kBandPanOffset      = 1,
    kBandSoloOffset     = 2,
    kBandBypassOffset   = 3,
    kBandMuteOffset     = 4,
    kBandMorphXOffset   = 5,
    kBandMorphYOffset   = 6,
    kBandMorphModeOffset = 7,
    kBandActiveNodesOffset = 8,

    // Per-node offsets within band (node 0-3 × 10 params each)
    kNode0TypeOffset    = 10,
    kNode0DriveOffset   = 11,
    kNode0MixOffset     = 12,
    kNode0ToneOffset    = 13,
    kNode0BiasOffset    = 14,
    kNode0FoldsOffset   = 15,
    kNode0BitDepthOffset = 16,
    kNode0PosXOffset    = 17,
    kNode0PosYOffset    = 18,
    // ... repeat for nodes 1-3 at offsets 20, 30, 40
};

// Helper functions
constexpr Steinberg::Vst::ParamID getBandParamId(int band, BandParameterOffsets offset) {
    return 1000 + band * 100 + offset;
}

constexpr Steinberg::Vst::ParamID getNodeParamId(int band, int node, int nodeOffset) {
    return 1000 + band * 100 + 10 + node * 10 + nodeOffset;
}
```

---

## 2. editor.uidesc Structure

### 2.1 Color Definitions

```xml
<?xml version="1.0" encoding="UTF-8"?>
<vstgui-ui-description version="1">
  <colors>
    <!-- Background colors -->
    <color name="background" rgba="#1a1a2eff"/>
    <color name="panel" rgba="#252538ff"/>
    <color name="section" rgba="#2d2d44ff"/>
    <color name="header" rgba="#1e1e32ff"/>

    <!-- Text colors -->
    <color name="text" rgba="#e8e8f0ff"/>
    <color name="text-dim" rgba="#8888aaff"/>
    <color name="text-accent" rgba="#89ddffff"/>

    <!-- Accent colors (from ui-mockups.md) -->
    <color name="accent-primary" rgba="#89ddffff"/>
    <color name="accent-secondary" rgba="#c792eaff"/>

    <!-- Band colors (8 bands) -->
    <color name="band-1" rgba="#ff6b35ff"/>
    <color name="band-2" rgba="#4ecdc4ff"/>
    <color name="band-3" rgba="#95e86bff"/>
    <color name="band-4" rgba="#c792eaff"/>
    <color name="band-5" rgba="#ffcb6bff"/>
    <color name="band-6" rgba="#ff5370ff"/>
    <color name="band-7" rgba="#89ddffff"/>
    <color name="band-8" rgba="#f78c6cff"/>

    <!-- Distortion category colors -->
    <color name="cat-saturation" rgba="#ff6b35ff"/>
    <color name="cat-wavefold" rgba="#4ecdc4ff"/>
    <color name="cat-digital" rgba="#95e86bff"/>
    <color name="cat-rectify" rgba="#c792eaff"/>
    <color name="cat-dynamic" rgba="#ffcb6bff"/>
    <color name="cat-hybrid" rgba="#ff5370ff"/>
    <color name="cat-experimental" rgba="#89ddffff"/>

    <!-- Control colors -->
    <color name="slider-track" rgba="#404060ff"/>
    <color name="slider-fill" rgba="#89ddffff"/>
    <color name="knob-bg" rgba="#303048ff"/>
    <color name="border" rgba="#505070ff"/>

    <!-- State colors -->
    <color name="active" rgba="#89ddffff"/>
    <color name="bypass" rgba="#666680ff"/>
    <color name="solo" rgba="#ffcb6bff"/>
    <color name="mute" rgba="#ff5370ff"/>
  </colors>
```

### 2.2 Font Definitions

```xml
  <fonts>
    <font name="title-font" font-name="Segoe UI" size="18" bold="true"/>
    <font name="section-font" font-name="Segoe UI" size="14" bold="true"/>
    <font name="label-font" font-name="Segoe UI" size="10"/>
    <font name="value-font" font-name="Segoe UI" size="11"/>
    <font name="small-font" font-name="Segoe UI" size="9"/>
    <font name="band-font" font-name="Segoe UI" size="12" bold="true"/>
  </fonts>
```

### 2.3 Control Tags

All parameter IDs must be mapped to human-readable names for use in templates:

```xml
  <control-tags>
    <!-- Global Parameters -->
    <control-tag name="InputGain" tag="0"/>
    <control-tag name="OutputGain" tag="1"/>
    <control-tag name="GlobalMix" tag="2"/>
    <control-tag name="BandCount" tag="3"/>
    <control-tag name="OversampleMax" tag="4"/>

    <!-- Sweep Parameters -->
    <control-tag name="SweepEnable" tag="100"/>
    <control-tag name="SweepFrequency" tag="101"/>
    <control-tag name="SweepWidth" tag="102"/>
    <control-tag name="SweepIntensity" tag="103"/>
    <control-tag name="SweepMorphLink" tag="104"/>
    <control-tag name="SweepFalloff" tag="105"/>

    <!-- LFO 1 -->
    <control-tag name="LFO1Rate" tag="200"/>
    <control-tag name="LFO1Shape" tag="201"/>
    <control-tag name="LFO1Phase" tag="202"/>
    <control-tag name="LFO1Sync" tag="203"/>
    <control-tag name="LFO1NoteValue" tag="204"/>
    <control-tag name="LFO1Unipolar" tag="205"/>

    <!-- LFO 2 -->
    <control-tag name="LFO2Rate" tag="220"/>
    <control-tag name="LFO2Shape" tag="221"/>
    <control-tag name="LFO2Phase" tag="222"/>
    <control-tag name="LFO2Sync" tag="223"/>
    <control-tag name="LFO2NoteValue" tag="224"/>
    <control-tag name="LFO2Unipolar" tag="225"/>

    <!-- Envelope Follower -->
    <control-tag name="EnvFollowerAttack" tag="240"/>
    <control-tag name="EnvFollowerRelease" tag="241"/>
    <control-tag name="EnvFollowerSensitivity" tag="242"/>

    <!-- Macros -->
    <control-tag name="Macro1" tag="400"/>
    <control-tag name="Macro2" tag="401"/>
    <control-tag name="Macro3" tag="402"/>
    <control-tag name="Macro4" tag="403"/>

    <!-- Band 0 Parameters (1000-1099) -->
    <control-tag name="Band0Gain" tag="1000"/>
    <control-tag name="Band0Pan" tag="1001"/>
    <control-tag name="Band0Solo" tag="1002"/>
    <control-tag name="Band0Bypass" tag="1003"/>
    <control-tag name="Band0Mute" tag="1004"/>
    <control-tag name="Band0MorphX" tag="1005"/>
    <control-tag name="Band0MorphY" tag="1006"/>
    <control-tag name="Band0MorphMode" tag="1007"/>
    <control-tag name="Band0ActiveNodes" tag="1008"/>

    <!-- Band 0, Node 0 Parameters -->
    <control-tag name="Band0Node0Type" tag="1010"/>
    <control-tag name="Band0Node0Drive" tag="1011"/>
    <control-tag name="Band0Node0Mix" tag="1012"/>
    <control-tag name="Band0Node0Tone" tag="1013"/>
    <control-tag name="Band0Node0Bias" tag="1014"/>
    <control-tag name="Band0Node0Folds" tag="1015"/>
    <control-tag name="Band0Node0BitDepth" tag="1016"/>

    <!-- Band 0, Node 1 Parameters -->
    <control-tag name="Band0Node1Type" tag="1020"/>
    <control-tag name="Band0Node1Drive" tag="1021"/>
    <control-tag name="Band0Node1Mix" tag="1022"/>
    <control-tag name="Band0Node1Tone" tag="1023"/>
    <!-- ... continue for all nodes and bands -->

    <!-- Repeat for Bands 1-7 at offsets 1100, 1200, ... 1700 -->

    <!-- UI-Only Tags (visibility controllers, not parameters) -->
    <control-tag name="Band0ExpandedContainer" tag="9100"/>
    <control-tag name="Band1ExpandedContainer" tag="9101"/>
    <!-- ... -->
    <control-tag name="ModulationPanelContainer" tag="9200"/>
    <control-tag name="SweepControlsContainer" tag="9201"/>
  </control-tags>
```

### 2.4 Gradients

```xml
  <gradients>
    <gradient name="panel-bg">
      <color-stop rgba="#2d2d44ff" start="0"/>
      <color-stop rgba="#252538ff" start="1"/>
    </gradient>
    <gradient name="button-normal">
      <color-stop rgba="#404060ff" start="0"/>
      <color-stop rgba="#303048ff" start="1"/>
    </gradient>
    <gradient name="button-active">
      <color-stop rgba="#89ddffff" start="0"/>
      <color-stop rgba="#6abfdbff" start="1"/>
    </gradient>
    <gradient name="knob-ring">
      <color-stop rgba="#89ddffff" start="0"/>
      <color-stop rgba="#c792eaff" start="1"/>
    </gradient>
  </gradients>
```

---

## 3. Template Definitions

### 3.1 Main Editor Layout

```xml
  <!-- Main editor template -->
  <template name="editor" class="CViewContainer" origin="0, 0" size="1000, 600"
            background-color="background" transparent="false">

    <!-- Header Section -->
    <view class="CViewContainer" origin="0, 0" size="1000, 50" background-color="header">
      <view class="CTextLabel" origin="10, 10" size="150, 30" title="DISRUMPO"
            font="title-font" font-color="text" transparent="true"/>

      <!-- Global controls -->
      <view class="CKnob" control-tag="InputGain" origin="200, 8" size="34, 34"/>
      <view class="CTextLabel" origin="200, 42" size="34, 12" title="IN"
            font="small-font" font-color="text-dim" text-alignment="center"/>

      <view class="CKnob" control-tag="OutputGain" origin="250, 8" size="34, 34"/>
      <view class="CTextLabel" origin="250, 42" size="34, 12" title="OUT"
            font="small-font" font-color="text-dim" text-alignment="center"/>

      <view class="CKnob" control-tag="GlobalMix" origin="300, 8" size="34, 34"/>
      <view class="CTextLabel" origin="300, 42" size="34, 12" title="MIX"
            font="small-font" font-color="text-dim" text-alignment="center"/>

      <!-- Band count selector -->
      <view class="CSegmentButton" control-tag="BandCount" origin="400, 12" size="200, 26"
            style="horizontal" segment-names="1,2,3,4,5,6,7,8" selection-mode="kSingle"
            font="value-font" text-color="text" text-color-highlighted="background"
            frame-color="border" gradient="button-normal" gradient-highlighted="button-active"/>

      <!-- Preset browser button -->
      <view class="CTextButton" origin="900, 12" size="80, 26" title="Presets"
            font="value-font" text-color="text" frame-color="border" gradient="button-normal"/>
    </view>

    <!-- Spectrum Display (custom control) -->
    <view class="SpectrumDisplay" custom-view-name="SpectrumDisplay"
          origin="10, 60" size="980, 200"/>

    <!-- Band Strip Container -->
    <view class="CScrollView" origin="10, 270" size="700, 320"
          background-color="panel" container-size="700, 800">
      <!-- Dynamic band strips inserted here -->
    </view>

    <!-- Side Panel (Morph Pad + Modulation) -->
    <view class="CViewContainer" origin="720, 270" size="270, 320" background-color="panel">
      <!-- Morph Pad (custom control) -->
      <view class="MorphPad" custom-view-name="MorphPad"
            origin="10, 10" size="250, 200"/>

      <!-- Modulation quick access -->
      <view class="CViewContainer" origin="10, 220" size="250, 90" background-color="section">
        <view class="CTextLabel" origin="5, 5" size="240, 16" title="MODULATION"
              font="section-font" font-color="accent-primary"/>
        <!-- LFO indicators, routing summary -->
      </view>
    </view>
  </template>
```

### 3.2 Band Strip Template (Collapsed)

```xml
  <template name="BandStripCollapsed" class="CViewContainer" origin="0, 0" size="680, 60"
            background-color="section" transparent="false">

    <!-- Band header -->
    <view class="CTextLabel" origin="10, 5" size="60, 20" title="BAND 1"
          font="band-font" font-color="band-1" transparent="true"/>

    <!-- Distortion type dropdown -->
    <view class="COptionMenu" control-tag="Band0Node0Type" origin="80, 5" size="120, 22"
          font="value-font" font-color="text" back-color="knob-bg" frame-color="border"
          style-round-rect="true" round-rect-radius="3"/>

    <!-- Solo/Bypass/Mute toggles -->
    <view class="COnOffButton" control-tag="Band0Solo" origin="210, 5" size="22, 22"
          title="S" font="small-font" text-color="text" text-color-on="solo"/>
    <view class="COnOffButton" control-tag="Band0Bypass" origin="235, 5" size="22, 22"
          title="B" font="small-font" text-color="text" text-color-on="bypass"/>
    <view class="COnOffButton" control-tag="Band0Mute" origin="260, 5" size="22, 22"
          title="M" font="small-font" text-color="text" text-color-on="mute"/>

    <!-- Drive knob (quick access) -->
    <view class="CKnob" control-tag="Band0Node0Drive" origin="300, 5" size="40, 40"/>
    <view class="CTextLabel" origin="300, 45" size="40, 12" title="DRIVE"
          font="small-font" font-color="text-dim" text-alignment="center"/>

    <!-- Mix knob (quick access) -->
    <view class="CKnob" control-tag="Band0Node0Mix" origin="350, 5" size="40, 40"/>
    <view class="CTextLabel" origin="350, 45" size="40, 12" title="MIX"
          font="small-font" font-color="text-dim" text-alignment="center"/>

    <!-- Band level meter -->
    <view class="CVuMeter" origin="420, 10" size="200, 15"/>

    <!-- Expand button -->
    <view class="COnOffButton" origin="640, 15" size="30, 30" title="+"
          font="section-font" text-color="text"/>
  </template>
```

### 3.3 Band Strip Template (Expanded)

```xml
  <template name="BandStripExpanded" class="CViewContainer" origin="0, 0" size="680, 280"
            background-color="section" transparent="false">

    <!-- Include collapsed header -->
    <view template="BandStripCollapsed" origin="0, 0"/>

    <!-- Expanded content -->
    <view class="CViewContainer" origin="10, 65" size="660, 205" background-color="panel">

      <!-- Morph controls section -->
      <view class="CViewContainer" origin="5, 5" size="200, 195">
        <view class="CTextLabel" origin="0, 0" size="200, 16" title="MORPH"
              font="section-font" font-color="accent-primary"/>

        <!-- Mini morph pad -->
        <view class="MorphPad" custom-view-name="MorphPadMini"
              origin="10, 20" size="180, 120"/>

        <!-- Morph mode selector -->
        <view class="CSegmentButton" control-tag="Band0MorphMode" origin="10, 145" size="180, 22"
              style="horizontal" segment-names="1D,2D,Radial" selection-mode="kSingle"
              font="small-font"/>

        <!-- Active nodes selector -->
        <view class="CSegmentButton" control-tag="Band0ActiveNodes" origin="10, 170" size="180, 22"
              style="horizontal" segment-names="2,3,4" selection-mode="kSingle"
              font="small-font"/>
      </view>

      <!-- Type-specific parameters (view switcher) -->
      <view class="UIViewSwitchContainer" origin="210, 5" size="250, 195"
            template-switch-control="Band0Node0Type">
        <!-- Templates for each of 26 distortion types -->
        <!-- Index 0: Soft Clip -->
        <template name="SoftClipParams">
          <view class="CKnob" control-tag="Band0Node0Bias" origin="10, 20" size="50, 50"/>
          <view class="CTextLabel" origin="10, 70" size="50, 12" title="BIAS"/>
        </template>
        <!-- Index 1: Hard Clip -->
        <!-- ... templates for all 26 types ... -->
      </view>

      <!-- Output section -->
      <view class="CViewContainer" origin="465, 5" size="190, 195">
        <view class="CTextLabel" origin="0, 0" size="190, 16" title="OUTPUT"
              font="section-font" font-color="accent-primary"/>

        <view class="CKnob" control-tag="Band0Gain" origin="20, 30" size="60, 60"/>
        <view class="CTextLabel" origin="20, 90" size="60, 12" title="GAIN"/>

        <view class="CKnob" control-tag="Band0Pan" origin="100, 30" size="60, 60"/>
        <view class="CTextLabel" origin="100, 90" size="60, 12" title="PAN"/>

        <view class="CKnob" control-tag="Band0Node0Tone" origin="60, 120" size="60, 60"/>
        <view class="CTextLabel" origin="60, 180" size="60, 12" title="TONE"/>
      </view>
    </view>
  </template>
```

---

## 4. Controller Implementation

### 4.1 Controller Class Declaration

```cpp
// plugins/Disrumpo/src/controller/controller.h
#pragma once

#include "public.sdk/source/vst/vsteditcontroller.h"
#include "vstgui/plugin-bindings/vst3editor.h"

#include <memory>

namespace Disrumpo {

// Forward declarations
class SpectrumDisplay;
class MorphPad;

class Controller : public Steinberg::Vst::EditControllerEx1,
                   public VSTGUI::VST3EditorDelegate {
public:
    Controller() = default;
    ~Controller() override;

    // IPluginBase
    Steinberg::tresult PLUGIN_API initialize(FUnknown* context) override;
    Steinberg::tresult PLUGIN_API terminate() override;

    // IEditController
    Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) override;
    Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) override;

    // Parameter display
    Steinberg::tresult PLUGIN_API getParamStringByValue(
        Steinberg::Vst::ParamID id,
        Steinberg::Vst::ParamValue valueNormalized,
        Steinberg::Vst::String128 string) override;

    Steinberg::tresult PLUGIN_API getParamValueByString(
        Steinberg::Vst::ParamID id,
        Steinberg::Vst::TChar* string,
        Steinberg::Vst::ParamValue& valueNormalized) override;

    // VST3EditorDelegate - CRITICAL for custom controls
    VSTGUI::CView* createCustomView(
        VSTGUI::UTF8StringPtr name,
        const VSTGUI::UIAttributes& attributes,
        const VSTGUI::IUIDescription* description,
        VSTGUI::VST3Editor* editor) override;

    void didOpen(VSTGUI::VST3Editor* editor) override;
    void willClose(VSTGUI::VST3Editor* editor) override;

    static FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IEditController*>(new Controller());
    }

    DEFINE_INTERFACES
        DEF_INTERFACE(Steinberg::Vst::IEditController)
        DEF_INTERFACE(Steinberg::Vst::IEditController2)
    END_DEFINE_INTERFACES(EditController)

    DELEGATE_REFCOUNT(EditController)

private:
    // Parameter registration helpers
    void registerGlobalParams(Steinberg::Vst::ParameterContainer& params);
    void registerSweepParams(Steinberg::Vst::ParameterContainer& params);
    void registerModulationParams(Steinberg::Vst::ParameterContainer& params);
    void registerBandParams(Steinberg::Vst::ParameterContainer& params, int bandIndex);

    // Active editor (for visibility controllers)
    VSTGUI::VST3Editor* activeEditor_ = nullptr;

    // Visibility controllers (IDependent pattern)
    // These manage progressive disclosure
    std::vector<Steinberg::IPtr<Steinberg::FObject>> bandExpandedControllers_;
    std::vector<Steinberg::IPtr<Steinberg::FObject>> bandVisibilityControllers_;
    Steinberg::IPtr<Steinberg::FObject> modulationPanelController_;
    Steinberg::IPtr<Steinberg::FObject> sweepControlsController_;

    // Type-specific parameter visibility (per node, per band)
    // Shows different parameters based on distortion type selection
    std::vector<std::vector<Steinberg::IPtr<Steinberg::FObject>>> typeParamControllers_;
};

} // namespace Disrumpo
```

### 4.2 Parameter Registration

```cpp
// plugins/Disrumpo/src/controller/controller.cpp (partial)

Steinberg::tresult PLUGIN_API Controller::initialize(FUnknown* context) {
    Steinberg::tresult result = EditControllerEx1::initialize(context);
    if (result != Steinberg::kResultTrue) return result;

    // Global parameters
    registerGlobalParams(parameters);

    // Sweep parameters
    registerSweepParams(parameters);

    // Modulation parameters
    registerModulationParams(parameters);

    // Per-band parameters (8 bands)
    for (int b = 0; b < 8; ++b) {
        registerBandParams(parameters, b);
    }

    return Steinberg::kResultTrue;
}

void Controller::registerGlobalParams(Steinberg::Vst::ParameterContainer& params) {
    // Input Gain: -24 to +24 dB, default 0 dB
    params.addParameter(
        STR16("Input Gain"), STR16("dB"), 0,
        0.5,  // normalized: (0 - (-24)) / (24 - (-24)) = 0.5
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        kInputGainId, 0, STR16("InGain")
    );

    // Output Gain: -24 to +24 dB, default 0 dB
    params.addParameter(
        STR16("Output Gain"), STR16("dB"), 0, 0.5,
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        kOutputGainId, 0, STR16("OutGain")
    );

    // Global Mix: 0-100%, default 100%
    params.addParameter(
        STR16("Mix"), STR16("%"), 0, 1.0,
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        kGlobalMixId, 0, STR16("Mix")
    );

    // Band Count: 1-8, default 4
    auto* bandCountParam = new Steinberg::Vst::StringListParameter(
        STR16("Band Count"), kBandCountId, nullptr,
        Steinberg::Vst::ParameterInfo::kCanAutomate |
        Steinberg::Vst::ParameterInfo::kIsList
    );
    for (int i = 1; i <= 8; ++i) {
        bandCountParam->appendString(STR16(std::to_string(i).c_str()));
    }
    bandCountParam->setNormalized(3.0 / 7.0);  // Default: 4 bands (index 3)
    params.addParameter(bandCountParam);

    // Oversample Max: 1x, 2x, 4x, 8x
    auto* osParam = new Steinberg::Vst::StringListParameter(
        STR16("Max Oversample"), kOversampleMaxId, nullptr,
        Steinberg::Vst::ParameterInfo::kCanAutomate |
        Steinberg::Vst::ParameterInfo::kIsList
    );
    osParam->appendString(STR16("1x"));
    osParam->appendString(STR16("2x"));
    osParam->appendString(STR16("4x"));
    osParam->appendString(STR16("8x"));
    osParam->setNormalized(2.0 / 3.0);  // Default: 4x (index 2)
    params.addParameter(osParam);
}

void Controller::registerBandParams(Steinberg::Vst::ParameterContainer& params, int band) {
    const int baseId = 1000 + band * 100;
    std::string prefix = "Band " + std::to_string(band + 1) + " ";

    // Band Gain
    params.addParameter(
        STR16((prefix + "Gain").c_str()), STR16("dB"), 0, 0.5,
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        baseId + kBandGainOffset
    );

    // Band Pan
    params.addParameter(
        STR16((prefix + "Pan").c_str()), STR16(""), 0, 0.5,
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        baseId + kBandPanOffset
    );

    // Solo/Bypass/Mute (discrete toggles)
    params.addParameter(
        STR16((prefix + "Solo").c_str()), nullptr, 1, 0.0,
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        baseId + kBandSoloOffset
    );

    params.addParameter(
        STR16((prefix + "Bypass").c_str()), nullptr, 1, 0.0,
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        baseId + kBandBypassOffset
    );

    params.addParameter(
        STR16((prefix + "Mute").c_str()), nullptr, 1, 0.0,
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        baseId + kBandMuteOffset
    );

    // Morph X/Y
    params.addParameter(
        STR16((prefix + "Morph X").c_str()), nullptr, 0, 0.5,
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        baseId + kBandMorphXOffset
    );

    params.addParameter(
        STR16((prefix + "Morph Y").c_str()), nullptr, 0, 0.5,
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        baseId + kBandMorphYOffset
    );

    // Morph Mode: 1D Linear, 2D Planar, 2D Radial
    auto* morphMode = new Steinberg::Vst::StringListParameter(
        STR16((prefix + "Morph Mode").c_str()),
        baseId + kBandMorphModeOffset
    );
    morphMode->appendString(STR16("1D Linear"));
    morphMode->appendString(STR16("2D Planar"));
    morphMode->appendString(STR16("2D Radial"));
    morphMode->setNormalized(0.5);  // Default: 2D Planar
    params.addParameter(morphMode);

    // Per-node parameters (4 nodes per band)
    for (int node = 0; node < 4; ++node) {
        registerNodeParams(params, band, node);
    }
}

void Controller::registerNodeParams(Steinberg::Vst::ParameterContainer& params,
                                     int band, int node) {
    const int baseId = 1000 + band * 100 + 10 + node * 10;
    std::string prefix = "B" + std::to_string(band + 1) +
                         "N" + std::to_string(node + 1) + " ";

    // Distortion Type (26 types)
    auto* typeParam = new Steinberg::Vst::StringListParameter(
        STR16((prefix + "Type").c_str()),
        baseId + 0,  // kNode*TypeOffset
        nullptr,
        Steinberg::Vst::ParameterInfo::kCanAutomate |
        Steinberg::Vst::ParameterInfo::kIsList
    );
    // Add all 26 distortion type names
    typeParam->appendString(STR16("Soft Clip"));
    typeParam->appendString(STR16("Hard Clip"));
    typeParam->appendString(STR16("Tube"));
    typeParam->appendString(STR16("Tape"));
    typeParam->appendString(STR16("Fuzz"));
    typeParam->appendString(STR16("Asym Fuzz"));
    typeParam->appendString(STR16("Sine Fold"));
    typeParam->appendString(STR16("Tri Fold"));
    typeParam->appendString(STR16("Serge Fold"));
    typeParam->appendString(STR16("Full Rectify"));
    typeParam->appendString(STR16("Half Rectify"));
    typeParam->appendString(STR16("Bitcrush"));
    typeParam->appendString(STR16("Sample Reduce"));
    typeParam->appendString(STR16("Quantize"));
    typeParam->appendString(STR16("Temporal"));
    typeParam->appendString(STR16("Ring Sat"));
    typeParam->appendString(STR16("Feedback"));
    typeParam->appendString(STR16("Aliasing"));
    typeParam->appendString(STR16("Bitwise"));
    typeParam->appendString(STR16("Chaos"));
    typeParam->appendString(STR16("Formant"));
    typeParam->appendString(STR16("Granular"));
    typeParam->appendString(STR16("Spectral"));
    typeParam->appendString(STR16("Fractal"));
    typeParam->appendString(STR16("Stochastic"));
    typeParam->appendString(STR16("Allpass Res"));
    params.addParameter(typeParam);

    // Common parameters
    params.addParameter(
        STR16((prefix + "Drive").c_str()), nullptr, 0, 0.1,
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        baseId + 1  // kNode*DriveOffset
    );

    params.addParameter(
        STR16((prefix + "Mix").c_str()), STR16("%"), 0, 1.0,
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        baseId + 2  // kNode*MixOffset
    );

    params.addParameter(
        STR16((prefix + "Tone").c_str()), STR16("Hz"), 0, 0.5,
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        baseId + 3  // kNode*ToneOffset
    );

    // Type-specific parameters (always registered, visibility controlled by UI)
    params.addParameter(
        STR16((prefix + "Bias").c_str()), nullptr, 0, 0.5,
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        baseId + 4  // kNode*BiasOffset
    );

    params.addParameter(
        STR16((prefix + "Folds").c_str()), nullptr, 0, 0.0,
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        baseId + 5  // kNode*FoldsOffset
    );

    params.addParameter(
        STR16((prefix + "Bit Depth").c_str()), nullptr, 0, 1.0,
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        baseId + 6  // kNode*BitDepthOffset
    );
}
```

### 4.3 Custom View Creation

```cpp
VSTGUI::CView* Controller::createCustomView(
    VSTGUI::UTF8StringPtr name,
    const VSTGUI::UIAttributes& attributes,
    const VSTGUI::IUIDescription* description,
    VSTGUI::VST3Editor* editor) {

    // SpectrumDisplay - real-time FFT with band visualization
    if (strcmp(name, "SpectrumDisplay") == 0) {
        VSTGUI::CRect size;
        if (attributes.getRect("size", size)) {
            auto* spectrum = new SpectrumDisplay(size);
            spectrum->setController(this);
            spectrum->setNumBands(static_cast<int>(
                getParamNormalized(kBandCountId) * 7 + 1));
            return spectrum;
        }
    }

    // MorphPad - 2D XY pad for morph control
    if (strcmp(name, "MorphPad") == 0 || strcmp(name, "MorphPadMini") == 0) {
        VSTGUI::CRect size;
        if (attributes.getRect("size", size)) {
            auto* pad = new MorphPad(size);
            pad->setController(this);

            // Get band index from attributes if specified
            int bandIndex = 0;
            attributes.getIntAttribute("band-index", bandIndex);
            pad->setBandIndex(bandIndex);

            return pad;
        }
    }

    return nullptr;  // Let VSTGUI handle standard controls
}
```

### 4.4 Visibility Controllers (Progressive Disclosure)

```cpp
void Controller::didOpen(VSTGUI::VST3Editor* editor) {
    activeEditor_ = editor;

    // Create visibility controllers for progressive disclosure

    // Band visibility based on band count
    for (int b = 0; b < 8; ++b) {
        // Show band b only if bandCount > b
        // Threshold: (b) / 7.0 means band b is visible when normalized >= b/7
        float threshold = static_cast<float>(b) / 7.0f;

        auto* controller = new ContainerVisibilityController(
            &activeEditor_,
            parameters.getParameter(kBandCountId),
            9100 + b,  // Tag of a control in the band container
            threshold,
            false  // showWhenBelow = false: show when value >= threshold
        );
        bandVisibilityControllers_.push_back(controller);
    }

    // Band expanded state visibility
    for (int b = 0; b < 8; ++b) {
        // Each band has an expand toggle parameter
        // When expanded, show the expanded container
        auto* controller = new ContainerVisibilityController(
            &activeEditor_,
            parameters.getParameter(getBandParamId(b, kBandExpandedOffset)),
            9100 + b,  // Expanded container tag
            0.5f,
            false  // Show when expanded (value >= 0.5)
        );
        bandExpandedControllers_.push_back(controller);
    }

    // Modulation panel visibility (expand toggle)
    modulationPanelController_ = new ContainerVisibilityController(
        &activeEditor_,
        parameters.getParameter(kModulationPanelExpandedId),
        9200,
        0.5f, false
    );
}

void Controller::willClose(VSTGUI::VST3Editor* editor) {
    // CRITICAL: Deactivate all visibility controllers before editor closes
    // This prevents use-after-free when deferred updates fire

    for (auto& controller : bandVisibilityControllers_) {
        if (auto* vc = dynamic_cast<ContainerVisibilityController*>(controller.get())) {
            vc->deactivate();
        }
    }
    bandVisibilityControllers_.clear();

    for (auto& controller : bandExpandedControllers_) {
        if (auto* vc = dynamic_cast<ContainerVisibilityController*>(controller.get())) {
            vc->deactivate();
        }
    }
    bandExpandedControllers_.clear();

    if (modulationPanelController_) {
        if (auto* vc = dynamic_cast<ContainerVisibilityController*>(
                modulationPanelController_.get())) {
            vc->deactivate();
        }
        modulationPanelController_ = nullptr;
    }

    activeEditor_ = nullptr;
}
```

---

## 5. UIViewSwitchContainer for Type-Specific Parameters

### 5.1 Overview

Each morph node has type-specific parameters that only apply to certain distortion types. Use `UIViewSwitchContainer` to show/hide the appropriate parameter panel.

### 5.2 Template Structure

```xml
<!-- In the expanded band template -->
<view class="UIViewSwitchContainer" origin="210, 25" size="250, 165"
      template-switch-control="Band0Node0Type"
      animation-style="fade" animation-time="100">

  <!-- Index 0: Soft Clip (Saturation) -->
  <template name="TypeParams_SoftClip">
    <view class="CKnob" control-tag="Band0Node0Bias" origin="10, 10" size="50, 50"/>
    <view class="CTextLabel" origin="10, 62" size="50, 12" title="Bias"
          font="small-font" font-color="text-dim" text-alignment="center"/>
  </template>

  <!-- Index 1: Hard Clip (Saturation) -->
  <template name="TypeParams_HardClip">
    <!-- Same as soft clip -->
  </template>

  <!-- Index 2: Tube (Saturation) -->
  <template name="TypeParams_Tube">
    <view class="CKnob" control-tag="Band0Node0Bias" origin="10, 10" size="50, 50"/>
    <view class="CTextLabel" origin="10, 62" size="50, 12" title="Bias"/>
    <view class="CKnob" control-tag="Band0Node0Sag" origin="70, 10" size="50, 50"/>
    <view class="CTextLabel" origin="70, 62" size="50, 12" title="Sag"/>
  </template>

  <!-- Index 6: Sine Fold (Wavefold) -->
  <template name="TypeParams_SineFold">
    <view class="CKnob" control-tag="Band0Node0Folds" origin="10, 10" size="50, 50"/>
    <view class="CTextLabel" origin="10, 62" size="50, 12" title="Folds"/>
    <view class="CKnob" control-tag="Band0Node0Shape" origin="70, 10" size="50, 50"/>
    <view class="CTextLabel" origin="70, 62" size="50, 12" title="Shape"/>
    <view class="CKnob" control-tag="Band0Node0Symmetry" origin="130, 10" size="50, 50"/>
    <view class="CTextLabel" origin="130, 62" size="50, 12" title="Sym"/>
  </template>

  <!-- Index 11: Bitcrush (Digital) -->
  <template name="TypeParams_Bitcrush">
    <view class="CKnob" control-tag="Band0Node0BitDepth" origin="10, 10" size="50, 50"/>
    <view class="CTextLabel" origin="10, 62" size="50, 12" title="Bits"/>
    <view class="CKnob" control-tag="Band0Node0SRRatio" origin="70, 10" size="50, 50"/>
    <view class="CTextLabel" origin="70, 62" size="50, 12" title="SR Ratio"/>
  </template>

  <!-- ... templates for all 26 types ... -->

  <!-- Index 19: Chaos (Experimental) -->
  <template name="TypeParams_Chaos">
    <view class="CKnob" control-tag="Band0Node0ChaosAmount" origin="10, 10" size="50, 50"/>
    <view class="CTextLabel" origin="10, 62" size="50, 12" title="Chaos"/>
    <view class="CKnob" control-tag="Band0Node0AttractorSpeed" origin="70, 10" size="50, 50"/>
    <view class="CTextLabel" origin="70, 62" size="50, 12" title="Speed"/>
    <view class="COptionMenu" control-tag="Band0Node0AttractorModel" origin="130, 10" size="80, 22"/>
    <view class="CTextLabel" origin="130, 34" size="80, 12" title="Model"/>
  </template>

</view>
```

---

## 6. State Serialization

### 6.1 Processor State Format

See [dsp-details.md](dsp-details.md) Section 2 for complete preset format.

### 6.2 Controller setComponentState

```cpp
Steinberg::tresult PLUGIN_API Controller::setComponentState(Steinberg::IBStream* state) {
    if (!state) return Steinberg::kResultFalse;

    Steinberg::IBStreamer streamer(state, kLittleEndian);

    // Read version
    int32 version;
    if (!streamer.readInt32(version)) return Steinberg::kResultFalse;

    // Read global parameters
    float inputGain, outputGain, globalMix;
    int32 bandCount, maxOversample;

    streamer.readFloat(inputGain);
    streamer.readFloat(outputGain);
    streamer.readFloat(globalMix);
    streamer.readInt32(bandCount);
    streamer.readInt32(maxOversample);

    // Convert to normalized and update
    setParamNormalized(kInputGainId, (inputGain + 24.0f) / 48.0f);
    setParamNormalized(kOutputGainId, (outputGain + 24.0f) / 48.0f);
    setParamNormalized(kGlobalMixId, globalMix);
    setParamNormalized(kBandCountId, (bandCount - 1) / 7.0f);
    setParamNormalized(kOversampleMaxId, maxOversample / 3.0f);

    // Read sweep parameters
    bool sweepEnabled;
    float sweepFreq, sweepWidth, sweepIntensity;
    int32 sweepMorphLink;

    streamer.readBool(sweepEnabled);
    streamer.readFloat(sweepFreq);
    streamer.readFloat(sweepWidth);
    streamer.readFloat(sweepIntensity);
    streamer.readInt32(sweepMorphLink);

    setParamNormalized(kSweepEnableId, sweepEnabled ? 1.0 : 0.0);
    // ... continue for all parameters

    // Read per-band state (fixed 8 bands for format stability)
    for (int b = 0; b < 8; ++b) {
        syncBandStateToController(streamer, b);
    }

    // Read modulation routings
    int32 routingCount;
    streamer.readInt32(routingCount);
    for (int r = 0; r < kMaxModRoutings; ++r) {
        syncRoutingToController(streamer, r);
    }

    return Steinberg::kResultTrue;
}
```

---

## 7. Implementation Checklist

### 7.1 Files to Create

| File | Purpose |
|------|---------|
| `src/plugin_ids.h` | Parameter IDs, FUIDs, ranges |
| `src/controller/controller.h` | Controller declaration |
| `src/controller/controller.cpp` | Controller implementation |
| `src/controller/views/spectrum_display.h/.cpp` | Custom SpectrumDisplay |
| `src/controller/views/morph_pad.h/.cpp` | Custom MorphPad |
| `src/controller/views/band_strip.h/.cpp` | Band strip container |
| `resources/editor.uidesc` | Complete UI definition |

### 7.2 editor.uidesc Sections

- [ ] `<colors>` - All color definitions
- [ ] `<fonts>` - All font definitions
- [ ] `<control-tags>` - All parameter ID mappings
- [ ] `<gradients>` - Background and button gradients
- [ ] `<template name="editor">` - Main layout
- [ ] `<template name="BandStripCollapsed">` - Collapsed band
- [ ] `<template name="BandStripExpanded">` - Expanded band
- [ ] `<template name="TypeParams_*">` - 26 type-specific panels
- [ ] `<template name="ModulationPanel">` - Modulation section
- [ ] `<template name="SweepPanel">` - Sweep controls
- [ ] `<template name="PresetBrowser">` - Preset UI

### 7.3 Controller Features

- [ ] Parameter registration for all ~450 parameters
- [ ] `createCustomView()` for SpectrumDisplay, MorphPad
- [ ] Visibility controllers for progressive disclosure
- [ ] `didOpen()` / `willClose()` lifecycle
- [ ] `setComponentState()` for preset loading
- [ ] `getParamStringByValue()` for parameter display
- [ ] UIViewSwitchContainer wiring for type-specific params

---

## Revision History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0 | 2026-01-27 | Claude | Initial VSTGUI implementation spec |
