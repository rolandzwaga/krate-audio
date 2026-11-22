#pragma once

// ==============================================================================
// View-sync rules shared by the three controller view paths (fix plan T4.5, R-108)
// ==============================================================================
// setParamNormalized (live edit), syncAllViews (state load) and verifyView (a
// view built on editor open) must show the same thing for the same parameter
// value. Each rule lives here once, tied to the count constant it depends on,
// so adding a waveform or condition or changing the display tempo changes all
// three paths together.
// ==============================================================================

#include "parameters/dropdown_mappings.h"
#include "ui/arp_condition_lane.h"
#include "ui/lfo_waveform_display.h"

#include <krate/dsp/core/note_value.h>
#include <krate/dsp/primitives/polyblep_oscillator.h>

#include "vstgui/lib/cview.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Ruinae {

static_assert(static_cast<int>(Krate::DSP::OscWaveform::Pulse) < kOscWaveformCount,
              "the PW knob rule needs Pulse inside the OSC waveform dropdown");

/// OSC Waveform dropdown index from its normalized value.
[[nodiscard]] inline int oscWaveformIndexFromNormalized(double normalized) {
    return std::clamp(static_cast<int>(normalized * (kOscWaveformCount - 1) + 0.5), 0,
                      kOscWaveformCount - 1);
}

/// PW knob alpha (068-osc-type-params FR-016): full only for the Pulse waveform.
[[nodiscard]] inline float oscPwKnobAlpha(double waveformNormalized) {
    return oscWaveformIndexFromNormalized(waveformNormalized) ==
                   static_cast<int>(Krate::DSP::OscWaveform::Pulse)
               ? 1.0f
               : 0.3f;
}

/// Dims an oscillator's PW knob from its waveform parameter (no-op without the knob).
inline void applyOscPwDimming(VSTGUI::CView* pwKnob, double waveformNormalized) {
    if (pwKnob) pwKnob->setAlphaValue(oscPwKnobAlpha(waveformNormalized));
}

/// Condition lane step index from a condition-step parameter's normalized value.
[[nodiscard]] inline uint8_t conditionIndexFromNormalized(double normalized) {
    constexpr int kMaxIndex = Krate::Plugins::ArpConditionLane::kConditionCount - 1;
    return static_cast<uint8_t>(
        std::clamp(static_cast<int>(std::round(normalized * kMaxIndex)), 0, kMaxIndex));
}

/// Beats per second the mod-source displays animate at when tempo-synced (the
/// editor has no host tempo, so every display uses the LFO display's default).
[[nodiscard]] constexpr float displayBeatsPerSecond() {
    return Krate::Plugins::LfoWaveformDisplay::kDefaultBpm / 60.0f;
}

/// Rate in Hz of a tempo-synced mod-source display for a note-value parameter.
[[nodiscard]] inline float syncedDisplayRateHz(double noteValueNormalized) {
    const int nvIdx = static_cast<int>(
        noteValueNormalized * (Krate::DSP::kNoteValueDropdownCount - 1) + 0.5);
    const auto mapping = Krate::DSP::getNoteValueFromDropdown(nvIdx);
    return displayBeatsPerSecond() / Krate::DSP::getBeatsForNote(mapping.note, mapping.modifier);
}

}  // namespace Ruinae
