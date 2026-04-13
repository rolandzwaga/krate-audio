// ==============================================================================
// EditorSizePolicy -- Phase 6 Default / Compact editor template selection
// ==============================================================================
// Spec: specs/141-membrum-phase6-ui/spec.md (FR-001, FR-040)
// Data model: specs/141-membrum-phase6-ui/data-model.md section 8
//
// Session-scoped: kEditorSizeId is registered as an automatable VST3 parameter
// but is NOT serialised in the state blob and is NOT encoded in any preset
// format. Always resets to Default on instantiation and on setState. The user
// toggle drives VST3Editor::exchangeView(templateNameFor(newSize)) on the UI
// thread via the IDependent pattern.
// ==============================================================================

#pragma once

#include <cstdint>

namespace Membrum::UI {

enum class EditorSize : std::uint8_t
{
    Default = 0,
    Compact = 1
};

inline constexpr EditorSize kDefaultEditorSize = EditorSize::Default;

// Editor template dimensions (px, native scale).
inline constexpr int kDefaultWidth  = 1280;
inline constexpr int kDefaultHeight = 800;
inline constexpr int kCompactWidth  = 1024;
inline constexpr int kCompactHeight = 640;

[[nodiscard]] constexpr const char* templateNameFor(EditorSize s) noexcept
{
    return (s == EditorSize::Default) ? "EditorDefault" : "EditorCompact";
}

} // namespace Membrum::UI
