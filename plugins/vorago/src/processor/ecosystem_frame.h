#pragma once

// ==============================================================================
// Vorago Phase 13 - Vorago::EcosystemFrame (spec C-2, FR-020, plan section 3.1)
// ==============================================================================
// Plain-old-data snapshot of the focus voice's ecosystem, published once per
// frame from the processor to the controller's EcosystemView.
//
// Shared by processor and controller (like plugin_ids.h): this header includes
// only the standard library -- never a processor, controller or dsp/ header.
//
// One-way, processor -> controller. Sent as raw bytes (memcpy'd into a
// DataExchange block or an IMessage binary attribute) in native endianness;
// the exact 1072-byte layout below is pinned by static_asserts.
// ==============================================================================

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace Vorago {

inline constexpr std::size_t kMaxFrameAgents = 48;  // == EcosystemEngine::kMaxAgents (asserted in the builder)
inline constexpr std::size_t kMaxFrameLinks = 64;
inline constexpr std::uint32_t kEcosystemFrameUserContextId = 0x5645434Fu;  // 'VECO'

struct EcosystemFrame {
    std::uint32_t sequence = 0;
    std::uint8_t activeVoices = 0;
    std::uint8_t focusVoice = 0;
    std::uint8_t agentCount = 0;
    std::uint8_t linkCount = 0;
    float voiceLevel = 0.0f;
    float agentX[kMaxFrameAgents] = {};
    float agentY[kMaxFrameAgents] = {};
    float agentGlow[kMaxFrameAgents] = {};
    std::uint8_t agentKind[kMaxFrameAgents] = {};
    std::uint8_t agentDormant[kMaxFrameAgents] = {};
    std::uint8_t linkA[kMaxFrameLinks] = {};
    std::uint8_t linkB[kMaxFrameLinks] = {};
    float linkStrength[kMaxFrameLinks] = {};
    float linkFlowScale = 0.0f;
};
static_assert(sizeof(EcosystemFrame) == 1072, "C-2 layout: 8+4+3*192+48+48+64+64+256+4");
static_assert(std::is_trivially_copyable_v<EcosystemFrame>);
static_assert(std::is_standard_layout_v<EcosystemFrame>);
static_assert(offsetof(EcosystemFrame, linkStrength) == 812);   // the uint8 run ends 4-aligned
static_assert(offsetof(EcosystemFrame, linkFlowScale) == 1068);

}  // namespace Vorago
