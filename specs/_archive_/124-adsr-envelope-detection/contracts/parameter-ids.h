// Contract: Parameter ID additions for ADSR Envelope
// Location: plugins/innexus/src/plugin_ids.h
// Added to ParameterIds enum after kAnalysisFeedbackDecayId = 711

    // ADSR Envelope (720-728) -- Spec 124
    kAdsrAttackId = 720,           // RangeParameter: 1-5000ms, log mapping, default 10ms
    kAdsrDecayId = 721,            // RangeParameter: 1-5000ms, log mapping, default 100ms
    kAdsrSustainId = 722,          // RangeParameter: 0.0-1.0, linear, default 1.0
    kAdsrReleaseId = 723,          // RangeParameter: 1-5000ms, log mapping, default 100ms
    kAdsrAmountId = 724,           // RangeParameter: 0.0-1.0, linear, default 0.0
    kAdsrTimeScaleId = 725,        // RangeParameter: 0.25-4.0, linear, default 1.0
    kAdsrAttackCurveId = 726,      // RangeParameter: -1.0 to +1.0, linear, default 0.0
    kAdsrDecayCurveId = 727,       // RangeParameter: -1.0 to +1.0, linear, default 0.0
    kAdsrReleaseCurveId = 728,     // RangeParameter: -1.0 to +1.0, linear, default 0.0

// Note: Attack/Decay/Sustain/Release MUST be consecutive (720-723) for ADSRDisplay::setAdsrBaseParamId()
// Note: Curve IDs MUST be consecutive (726-728) for ADSRDisplay::setCurveBaseParamId()
// Note: IDs 724 (Amount) and 725 (TimeScale) are intentionally placed between the two consecutive
//       groups. They are independent parameters that do not participate in ADSRDisplay's base-ID
//       lookup, so their position between the groups does not disrupt either consecutive run.
// Note: kAdsrAttackId through kAdsrReleaseId use logarithmic normalization for perceptual uniformity
