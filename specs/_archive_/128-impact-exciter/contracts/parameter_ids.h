// Contract: New parameter IDs for Impact Exciter (spec 128)
// Location: plugins/innexus/src/plugin_ids.h (append to ParameterIds enum)
// Range: 805-809 (within Physical Modelling 800-899 block)

// Impact Exciter Parameters (Spec 128)
kExciterTypeId = 805,           // StringListParameter: "Residual"/"Impact"/"Bow", default 0
kImpactHardnessId = 806,        // RangeParameter: 0.0-1.0, default 0.5
kImpactMassId = 807,            // RangeParameter: 0.0-1.0, default 0.3
kImpactBrightnessId = 808,      // RangeParameter: plain -1.0 to +1.0, norm 0.0-1.0, default 0.0 (norm 0.5)
kImpactPositionId = 809,        // RangeParameter: 0.0-1.0, default 0.13

// Also add enum:
// enum class ExciterType : int {
//     Residual = 0,
//     Impact = 1,
//     Bow = 2      // Reserved for Phase 4
// };
