// ==============================================================================
// MetersBlock -- Phase 6 DataExchange Payload for Kit-Column Meters
// ==============================================================================
// Spec: specs/141-membrum-phase6-ui/spec.md (FR-043, FR-044)
// Plan: specs/141-membrum-phase6-ui/plan.md
// Data model: specs/141-membrum-phase6-ui/data-model.md section 6
//
// Purpose:
//   Deliver compound meter values (stereo peak, active-voice count, CPU usage)
//   from the audio thread to the UI thread via Steinberg::Vst::IDataExchange
//   handler. 12 bytes/block, fixed layout; one block per audio block published
//   via DataExchangeHandler::sendMainSynchronously().
//
// Threading contract:
//   - Producer: Processor, audio thread, once per processBlock().
//   - Consumer: Controller::onDataExchangeBlocksReceived() (UI thread; see
//     Innexus processor.cpp:229-236 and controller.cpp:1703-1740 for lifecycle).
// ==============================================================================

#pragma once

#include <cstdint>

namespace Membrum {

struct MetersBlock
{
    float         peakL         = 0.0f;   // linear [0..1], main output L
    float         peakR         = 0.0f;   // linear [0..1], main output R
    std::uint16_t activeVoices  = 0;      // 0..16
    std::uint16_t cpuPermille   = 0;      // 0..1000 (tenths of a percent)
};

static_assert(sizeof(MetersBlock) == 12,
              "MetersBlock layout must match DataExchange contract");

// User context ID for the MetersBlock queue (chosen so it does not collide
// with any existing Membrum message IDs).
inline constexpr std::uint32_t kMetersDataExchangeUserContextId = 0x4D425452u; // 'MBTR'

} // namespace Membrum
