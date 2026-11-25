// ==============================================================================
// MetersBlock -- Phase 6 DataExchange Payload for Kit-Column Meters
// ==============================================================================
// Contract: specs/141-membrum-phase6-ui/contracts/meters_data_exchange.h
// Spec: specs/141-membrum-phase6-ui/spec.md (FR-043, FR-044)
// Data model: specs/141-membrum-phase6-ui/data-model.md section 6
//
// Producer: Processor, audio thread, once per process() via
//   DataExchangeHandler::getCurrentOrNewBlock() / sendCurrentBlock(). peakL,
//   peakR and padGlowBuckets are the maxima over every block since the last
//   block that was sent (a block the queue cannot take is folded into the
//   next); busActiveMask carries the host's output-bus activation (T2.31).
// Consumer: Controller::onDataExchangeBlocksReceived() (UI thread), which
//   folds every block received between two UI ticks; the poll timer consumes
//   the fold through Controller::consumeMetersForUiTick().
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

    // Per-pad glow buckets [0..31] -- snapshot of PadGlowPublisher taken on the
    // audio thread each block. Transported across the separate-component
    // boundary so the UI can light up pads in response to audio amplitude.
    std::uint8_t  padGlowBuckets[32] = {};

    // T2.31 (MB-100): output-bus activation as the processor sees it (the host
    // calls activateBus on the processor only). Bit b set = output bus b is
    // active; bit 0 (main) is always set.
    std::uint16_t busActiveMask = 1;
};

// 46 bytes of fields, padded to the 4-byte alignment of the floats.
static_assert(sizeof(MetersBlock) == 48,
              "MetersBlock layout must match DataExchange contract");

// User context ID for the MetersBlock queue (chosen so it does not collide
// with any existing Membrum message IDs).
inline constexpr std::uint32_t kMetersDataExchangeUserContextId = 0x4D425452u; // 'MBTR'

} // namespace Membrum
