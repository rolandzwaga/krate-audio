// ==============================================================================
// UiMode -- Phase 6 Acoustic / Extended editor mode
// ==============================================================================
// Spec: specs/141-membrum-phase6-ui/spec.md (FR-030, FR-081)
// Data model: specs/141-membrum-phase6-ui/data-model.md section 7
//
// Session-scoped: kUiModeId is registered as an automatable VST3 parameter but
// is NOT serialised in the state blob. On every plugin instantiation AND every
// setState the Controller resets it to kDefaultUiMode (Acoustic). Kit preset
// JSON MAY explicitly restore it via the preset-load callback.
// ==============================================================================

#pragma once

#include <cstdint>

namespace Membrum::UI {

enum class UiMode : std::uint8_t
{
    Acoustic = 0,
    Extended = 1
};

// Default mode on instantiation and after setState.
inline constexpr UiMode kDefaultUiMode = UiMode::Acoustic;

// Helper: normalised 0.0 -> Acoustic, 1.0 -> Extended.
[[nodiscard]] constexpr UiMode uiModeFromNormalized(float n) noexcept
{
    return (n < 0.5f) ? UiMode::Acoustic : UiMode::Extended;
}

[[nodiscard]] constexpr float uiModeToNormalized(UiMode m) noexcept
{
    return (m == UiMode::Acoustic) ? 0.0f : 1.0f;
}

} // namespace Membrum::UI
