#pragma once

// ==============================================================================
// DisplayData - Processor-to-Controller Display Data Transfer
// ==============================================================================
// FR-048: IMessage-based display data pipeline
// POD struct sent as binary payload via IMessage from processor to controller.
// ==============================================================================

#include <cstdint>

namespace Innexus {

struct DisplayData
{
    float partialAmplitudes[48]{};    // Linear amplitudes [0.0, ~1.0]
    uint8_t partialActive[48]{};      // 1 = active, 0 = filtered/attenuated
    float f0 = 0.0f;                  // Fundamental frequency (Hz)
    float f0Confidence = 0.0f;        // [0.0, 1.0]
    uint8_t slotOccupied[8]{};        // 1 = memory slot occupied
    float evolutionPosition = 0.0f;   // Combined morph position [0.0, 1.0]
    float manualMorphPosition = 0.0f; // Manual knob value [0.0, 1.0]
    float mod1Phase = 0.0f;           // LFO phase [0.0, 1.0]
    float mod2Phase = 0.0f;           // LFO phase [0.0, 1.0]
    bool mod1Active = false;          // Modulator 1 enabled & depth > 0
    bool mod2Active = false;          // Modulator 2 enabled & depth > 0
    uint32_t frameCounter = 0;        // Monotonic, incremented per new frame
};

} // namespace Innexus
