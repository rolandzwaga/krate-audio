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
