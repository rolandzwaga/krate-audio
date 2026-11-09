#pragma once

// ==============================================================================
// String Formatting Helpers for VST3 Parameter Display
// ==============================================================================

#include "pluginterfaces/base/ftypes.h"
#include "pluginterfaces/vst/vsttypes.h"
#include "dsp/band_state.h"
#include "dsp/shape_slot_descriptors.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Disrumpo {

static inline void intToString128(int value, Steinberg::Vst::String128 dest) {
    char temp[32];
    snprintf(temp, sizeof(temp), "%d", value);
    for (int i = 0; temp[i] && i < 127; ++i) {
        dest[i] = static_cast<Steinberg::Vst::TChar>(temp[i]);
    }
    dest[std::min(static_cast<int>(strlen(temp)), 127)] = 0;
}

static inline void floatToString128(double value, int precision, Steinberg::Vst::String128 dest) {
    char temp[64];
    snprintf(temp, sizeof(temp), "%.*f", precision, value);
    for (int i = 0; temp[i] && i < 127; ++i) {
        dest[i] = static_cast<Steinberg::Vst::TChar>(temp[i]);
    }
    dest[std::min(static_cast<int>(strlen(temp)), 127)] = 0;
}

static inline void appendToString128(Steinberg::Vst::String128 dest, const Steinberg::Vst::TChar* suffix) {
    int len = 0;
    while (dest[len] && len < 127) ++len;
    int suffixLen = 0;
    while (suffix[suffixLen] && (len + suffixLen) < 127) {
        dest[len + suffixLen] = suffix[suffixLen];
        ++suffixLen;
    }
    dest[len + suffixLen] = 0;
}

static inline void asciiToString128(const char* text, Steinberg::Vst::String128 dest) {
    int i = 0;
    for (; text[i] && i < 127; ++i) {
        dest[i] = static_cast<Steinberg::Vst::TChar>(text[i]);
    }
    dest[i] = 0;
}

static inline void appendAsciiToString128(Steinberg::Vst::String128 dest, const char* suffix) {
    int len = 0;
    while (dest[len] && len < 127) ++len;
    for (int i = 0; suffix[i] && len < 127; ++i, ++len) {
        dest[len] = static_cast<Steinberg::Vst::TChar>(suffix[i]);
    }
    dest[len] = 0;
}

/// @brief Format shape slot @p slot of a @p distType node at value @p v.
/// Reads the descriptor table the processor maps with (dsp/shape_slot_descriptors.h,
/// D-090). @p slots are the node's ten slot values (nullptr: none known); Ring
/// Saturation's carrier slot reads its Freq mode from them.
/// @return true if the slot has a format, false to fall through to the default.
static inline bool formatShapeSlot(DistortionType distType, int slot, float v, const float* slots,
                                   Steinberg::Vst::String128 string) {
    const int t = static_cast<int>(distType);
    if (t < 0 || t >= kDistortionTypeCount || slot < 0 || slot >= kShapeSlotDescriptorCount) {
        return false;
    }
    const ShapeSlotDescriptor& d = shapeSlotDescriptor(distType, slot);
    switch (d.map) {
        case SlotMap::Unused:
            return false;
        case SlotMap::Toggle:
            asciiToString128(v >= 0.5f ? "On" : "Off", string);
            return true;
        case SlotMap::Index:
        case SlotMap::Pow2:
            if (d.labels != nullptr) {
                const int entries = static_cast<int>(d.span) + 1;
                asciiToString128(d.labels[std::clamp(shapeSlotIndex(d, v), 0, entries - 1)], string);
            } else {
                intToString128(shapeSlotInt(d, v), string);
                appendAsciiToString128(string, d.unit);
            }
            return true;
        case SlotMap::Linear:
        case SlotMap::Bipolar: {
            const float range = d.map == SlotMap::Bipolar ? 2.0f * d.span : d.span;
            int precision = 3;  // about 1/1000 of the range or finer
            if (range >= 100.0f) {
                precision = 0;
            } else if (range >= 10.0f) {
                precision = 1;
            } else if (range >= 1.0f) {
                precision = 2;
            }
            floatToString128(shapeSlotFloat(d, v), precision, string);
            appendAsciiToString128(string, d.unit);
            return true;
        }
        case SlotMap::Norm:
            break;
    }

    switch (d.display) {
        case SlotDisplay::CurveName: {
            asciiToString128(kWaveshapeCurveNames[static_cast<size_t>(waveshapeCurveIndex(v))], string);
            return true;
        }
        case SlotDisplay::TemporalHoldMs:
            floatToString128(temporalHoldMs(v), 0, string);
            appendAsciiToString128(string, " ms");
            return true;
        case SlotDisplay::RingCarrier: {
            const int mode = slots == nullptr
                ? 0
                : std::clamp(shapeSlotInt(shapeSlotDescriptor(distType, kRingSatFreqModeSlot),
                                          slots[kRingSatFreqModeSlot]), 0, 3);
            if (mode == kRingSatHarmonicFreqMode) {
                intToString128(static_cast<int>(ringSatHarmonicRatio(v)), string);
                appendAsciiToString128(string, "x");
            } else {
                const float freqHz = ringSatCarrierHz(v);
                floatToString128(freqHz, freqHz < 100.0f ? 1 : 0, string);
                appendAsciiToString128(string, " Hz");
            }
            return true;
        }
        case SlotDisplay::Plain:
            break;
    }
    intToString128(static_cast<int>(std::round(v * 100.0f)), string);
    appendAsciiToString128(string, "%");
    return true;
}

} // namespace Disrumpo
