# Data Model: Ruinae Preset Browser Integration

**Date**: 2026-02-27

## Overview

This feature adds no new data entities. All entities are already defined in the shared preset infrastructure. This document maps the existing entities to their Ruinae-specific usage.

## Entities

### PresetInfo (existing -- plugins/shared/src/preset/preset_info.h)

Represents a single preset with all metadata. No modifications needed.

| Field | Type | Description |
|-------|------|-------------|
| name | std::string | Display name of the preset |
| category | std::string | Top-level category (e.g., "Synth" for Ruinae) |
| subcategory | std::string | Subcategory (e.g., "Arp Acid", "Pads", "Leads") |
| filePath | std::string | Full filesystem path to .vstpreset file |
| isFactory | bool | true for factory presets, false for user presets |
| description | std::string | Optional description text |
| author | std::string | Optional author name |

### PresetManagerConfig (existing -- plugins/shared/src/preset/preset_manager_config.h)

Plugin-specific configuration for the preset system. Already configured for Ruinae via `makeRuinaePresetConfig()`.

| Field | Type | Ruinae Value |
|-------|------|-------------|
| processorUID | FUID | kProcessorUID (from plugin_ids.h) |
| pluginName | std::string | "Ruinae" |
| pluginCategoryDesc | std::string | "Synth" |
| subcategoryNames | vector<string> | "Pads", "Leads", "Bass", "Textures", "Rhythmic", "Experimental", "Arp Classic", "Arp Acid", "Arp Euclidean", "Arp Polymetric", "Arp Generative", "Arp Performance" |

### Tab Labels (existing -- plugins/ruinae/src/preset/ruinae_preset_config.h)

The 13 tab labels returned by `getRuinaeTabLabels()`:

| Index | Label | Has Factory Presets? |
|-------|-------|---------------------|
| 0 | All | Yes (shows all 14) |
| 1 | Pads | No |
| 2 | Leads | No |
| 3 | Bass | No |
| 4 | Textures | No |
| 5 | Rhythmic | No |
| 6 | Experimental | No |
| 7 | Arp Classic | Yes (3 presets) |
| 8 | Arp Acid | Yes (2 presets) |
| 9 | Arp Euclidean | Yes (3 presets) |
| 10 | Arp Polymetric | Yes (2 presets) |
| 11 | Arp Generative | Yes (2 presets) |
| 12 | Arp Performance | Yes (2 presets) |

## State Serialization Format

The Ruinae processor state (version 1) is serialized in the following deterministic order by `Processor::getState()`. This same format is parsed by `Controller::setComponentState()` and the new `loadComponentStateWithNotify()`.

### Serialization Order

```
1.  int32: version (= 1)
2.  globalParams (saveGlobalParams / loadGlobalParamsToController)
3.  oscAParams (saveOscAParams / loadOscAParamsToController)
4.  oscBParams (saveOscBParams / loadOscBParamsToController)
5.  mixerParams (saveMixerParams / loadMixerParamsToController)
6.  filterParams (saveFilterParams / loadFilterParamsToController)
7.  distortionParams (saveDistortionParams / loadDistortionParamsToController)
8.  tranceGateParams (saveTranceGateParams / loadTranceGateParamsToController)
9.  ampEnvParams (saveAmpEnvParams / loadAmpEnvParamsToController)
10. filterEnvParams (saveFilterEnvParams / loadFilterEnvParamsToController)
11. modEnvParams (saveModEnvParams / loadModEnvParamsToController)
12. lfo1Params (saveLFO1Params / loadLFO1ParamsToController)
13. lfo2Params (saveLFO2Params / loadLFO2ParamsToController)
14. chaosModParams (saveChaosModParams / loadChaosModParamsToController)
15. modMatrixParams (saveModMatrixParams / loadModMatrixParamsToController)
16. globalFilterParams (saveGlobalFilterParams / loadGlobalFilterParamsToController)
17. delayParams (saveDelayParams / loadDelayParamsToController)
18. reverbParams (saveReverbParams / loadReverbParamsToController)
19. monoModeParams (saveMonoModeParams / loadMonoModeParamsToController)
20. voiceRoutes[16] (skip in controller -- processor-internal)
    - Per route: int8 source, int8 dest, float amount, int8 curve, float smoothMs, int8 scale, int8 bypass, int8 active
21. int8: delayEnabled
22. int8: reverbEnabled
23. phaserParams (savePhaserParams / loadPhaserParamsToController)
24. int8: phaserEnabled
25. lfo1ExtendedParams (saveLFO1ExtendedParams / loadLFO1ExtendedParamsToController)
26. lfo2ExtendedParams (saveLFO2ExtendedParams / loadLFO2ExtendedParamsToController)
27. macroParams (saveMacroParams / loadMacroParamsToController)
28. runglerParams (saveRunglerParams / loadRunglerParamsToController)
29. settingsParams (saveSettingsParams / loadSettingsParamsToController)
30. envFollowerParams (saveEnvFollowerParams / loadEnvFollowerParamsToController)
31. sampleHoldParams (saveSampleHoldParams / loadSampleHoldParamsToController)
32. randomParams (saveRandomParams / loadRandomParamsToController)
33. pitchFollowerParams (savePitchFollowerParams / loadPitchFollowerParamsToController)
34. transientParams (saveTransientParams / loadTransientParamsToController)
35. harmonizerParams (saveHarmonizerParams / loadHarmonizerParamsToController)
36. int8: harmonizerEnabled
37. arpParams (saveArpParams / loadArpParamsToController)
```

### Key Observations

1. **Voice routes (item 20)** are processor-internal data not exposed as parameters. `setComponentState()` reads and discards them. `loadComponentStateWithNotify()` must do the same.
2. **FX enable flags (items 21, 22, 24, 36)** are stored as int8 between parameter packs. They must be read individually and converted to normalized 0.0/1.0 for `editParamWithNotify`.
3. **Arp params (item 37)** include all lane step data (velocity, gate, pitch, ratchet, modifier, condition lane steps and lengths). These are the most complex and are fully handled by `loadArpParamsToController`.

## New Controller Fields

Added to `Ruinae::Controller` class in controller.h:

| Field | Type | Ownership | Purpose |
|-------|------|-----------|---------|
| presetBrowserView_ | Krate::Plugins::PresetBrowserView* | Frame-owned (raw ptr) | Reference to preset browser overlay |
| savePresetDialogView_ | Krate::Plugins::SavePresetDialogView* | Frame-owned (raw ptr) | Reference to save dialog overlay |

Note: `presetManager_` already exists as `std::unique_ptr<Krate::Plugins::PresetManager>`.

## New Controller Methods

| Method | Visibility | Signature | Purpose |
|--------|-----------|-----------|---------|
| openPresetBrowser | public | `void openPresetBrowser()` | Opens the preset browser overlay |
| closePresetBrowser | public | `void closePresetBrowser()` | Closes the preset browser overlay |
| openSavePresetDialog | public | `void openSavePresetDialog()` | Opens the save preset dialog overlay |
| createCustomView | public (override) | `CView* createCustomView(UTF8StringPtr, const UIAttributes&, const IUIDescription*, VST3Editor*)` | Creates PresetBrowserButton/SavePresetButton views |
| createComponentStateStream | private | `Steinberg::MemoryStream* createComponentStateStream()` | Gets serialized processor state via host |
| loadComponentStateWithNotify | private | `bool loadComponentStateWithNotify(Steinberg::IBStream* state)` | Loads state and notifies host of param changes |
| editParamWithNotify | private | `void editParamWithNotify(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value)` | Full edit cycle (beginEdit + setParamNormalized + performEdit + endEdit) |

## UI Layout Changes

### Top Bar (editor.uidesc)

Current top bar (x=0 to x=1400, y=0 to y=40):
- "RUINAE" title: x=12, size=100
- Tab selector: x=120, size=320 (ends at x=440)
- Free space: x=440 to x=1400

Additions:
- PresetBrowserButton: origin="460, 8" size="80, 25"
- SavePresetButton: origin="550, 8" size="60, 25"
