# Quick Reference

[← Back to Architecture Index](README.md)

---

## Layer Inclusion Rules

| Your Code In | Can Include |
|--------------|-------------|
| Layer 0 | stdlib only |
| Layer 1 | Layer 0 |
| Layer 2 | Layers 0-1 |
| Layer 3 | Layers 0-2 |
| Layer 4 | Layers 0-3 |
| Plugin | All DSP layers |

---

## Common Include Patterns

```cpp
#include <krate/dsp/core/db_utils.h>           // Layer 0
#include <krate/dsp/primitives/delay_line.h>   // Layer 1
#include <krate/dsp/processors/saturation.h>   // Layer 2
#include <krate/dsp/systems/delay_engine.h>    // Layer 3
#include <krate/dsp/effects/tape_delay.h>      // Layer 4
```

---

## Before Creating New Components

```bash
# Search for existing implementations (ODR prevention)
grep -r "class YourClassName" dsp/ plugins/
grep -r "struct YourStructName" dsp/ plugins/
```

Two classes with the same name in the same namespace = undefined behavior (garbage values, mysterious test failures).

---

## Shared Plugin Libraries

| Library | Namespace | Purpose |
|---------|-----------|---------|
| `KrateDSP` | `Krate::DSP` | Shared DSP primitives and processors |
| `KratePluginsShared` | `Krate::Plugins` | Shared preset management and UI components |

### Plugins Using KratePluginsShared

| Plugin | Preset Config | State Stream Strategy | Spec |
|--------|---------------|----------------------|------|
| Iterum | `makeIterumPresetConfig()` | Host delegation | 010 |
| Disrumpo | `makeDisrumpoPresetConfig()` | Controller-side serialization | 010 |
| Ruinae | `makeRuinaePresetConfig()` | Host delegation | 083 |

See [Plugin Architecture > Preset Browser Integration Registry](plugin-architecture.md#preset-browser-integration-registry) for details on state stream strategies and integration gotchas.

### Adding Preset Support to a New Plugin

```cpp
// 1. Create config: plugins/myplugin/src/preset/myplugin_preset_config.h
auto config = Krate::Plugins::PresetManagerConfig{
    .processorUID = kProcessorUID,
    .pluginName = "MyPlugin",
    .pluginCategoryDesc = "Effect",
    .subcategoryNames = {"Category1", "Category2", ...}
};

// 2. Link in CMakeLists.txt
target_link_libraries(MyPlugin PRIVATE KratePluginsShared)

// 3. Initialize in Controller::initialize()
presetManager_ = std::make_unique<Krate::Plugins::PresetManager>(config, ...);
```
