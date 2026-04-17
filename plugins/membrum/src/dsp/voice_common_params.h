#pragma once

// ==============================================================================
// VoiceCommonParams -- Phase 2 (data-model.md §3.1)
// ==============================================================================
// Common parameter bundle passed from DrumVoice to body mapping helpers.
// Phase 1 parameters are identical to their DrumVoice counterparts; Phase 2
// adds modeStretch and decaySkew (routed from UnnaturalZone).
// ==============================================================================

namespace Membrum {

struct VoiceCommonParams
{
    // Phase 1 parameters
    float material   = 0.0f;   // [0, 1]
    float size       = 0.0f;   // [0, 1]
    float decay      = 0.0f;   // [0, 1]
    float strikePos  = 0.0f;   // [0, 1]
    float level      = 0.0f;   // [0, 1]

    // Phase 2 additions (passed to body mappers)
    float modeStretch = 1.0f;  // [0.5, 2.0]   default 1.0 (no stretch)
    float decaySkew   = 0.0f;  // [-1.0, 1.0]  default 0.0 (uniform decay)

    // Phase 7: parallel noise layer + always-on click transient.
    // All values are the normalized [0, 1] PadConfig originals; the layer
    // components denormalize internally.
    float noiseLayerMix        = 0.0f;
    float noiseLayerCutoff     = 0.5f;
    float noiseLayerResonance  = 0.2f;
    float noiseLayerDecay      = 0.3f;
    float noiseLayerColor      = 0.5f;
    float clickLayerMix        = 0.0f;
    float clickLayerContactMs  = 0.3f;
    float clickLayerBrightness = 0.6f;
};

} // namespace Membrum
