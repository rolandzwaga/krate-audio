// Contract: Controller v4 API changes for Membrum Phase 4
// Documents the changes to the Controller class for per-pad parameter
// registration and selected-pad proxy logic.

#pragma once

namespace Membrum {

// ============================================================
// Controller::initialize() changes:
// ============================================================
//
// 1. KEEP all existing global parameter registrations (100-252)
//    These become "selected pad proxy" parameters.
//
// 2. ADD kSelectedPadId (260) as RangeParameter stepped [0, 31]:
//    parameters.addParameter(
//        new RangeParameter(STR16("Selected Pad"), kSelectedPadId, nullptr,
//                           0.0, 31.0, 0.0, /*stepCount=*/31,
//                           ParameterInfo::kCanAutomate));
//
// 3. ADD 1024 per-pad parameters (32 pads x 32 active params):
//    for (int pad = 0; pad < kNumPads; ++pad) {
//        for (int offset = 0; offset < kPadActiveParamCount; ++offset) {
//            int paramId = padParamId(pad, offset);
//            // Register with name "Pad NN ParamName"
//            // Use appropriate type (RangeParameter or StringListParameter)
//        }
//    }

// ============================================================
// Selected Pad Proxy Logic (FR-012, FR-013, FR-091):
// ============================================================
//
// When kSelectedPadId changes:
//   1. Read the new pad index N
//   2. For each global param (kMaterialId, kSizeId, etc.):
//      a. Look up the corresponding per-pad param value:
//         padParamValue = getParamNormalized(padParamId(N, offset))
//      b. Update the global param to reflect the new pad:
//         setParamNormalized(globalParamId, padParamValue)
//
// When a global param changes (e.g., kMaterialId):
//   1. Read selectedPadIndex
//   2. Forward the change to the selected pad's per-pad parameter:
//      performEdit(padParamId(selectedPad, correspondingOffset), newValue)
//      setParamNormalized(padParamId(selectedPad, correspondingOffset), newValue)
//      endEdit(padParamId(selectedPad, correspondingOffset))

// ============================================================
// setComponentState() changes:
// ============================================================
//
// Reads v4 state blob and syncs all per-pad parameter values:
// 1. Read global settings (maxPolyphony, stealingPolicy)
// 2. For each of 32 pads: read PadConfig and update per-pad param values
// 3. Read selectedPadIndex and update proxy params
// 4. Sync global proxy params to reflect the selected pad's values

// ============================================================
// Parameter Count Summary:
// ============================================================
//
// Global params (Phase 1-3):           ~35  (IDs 100-252)
// Selected Pad:                          1  (ID 260)
// Per-pad params:                     1024  (32 pads x 32, IDs 1000-3047)
// Total:                             ~1060  parameters
//
// The host-generic editor will be crowded but functional.
// Custom UI in Phase 6 will provide a usable interface.

} // namespace Membrum
