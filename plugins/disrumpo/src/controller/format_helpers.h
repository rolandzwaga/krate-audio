#pragma once

// ==============================================================================
// String Formatting Helpers for VST3 Parameter Display
// ==============================================================================

#include "pluginterfaces/base/ftypes.h"
#include "pluginterfaces/vst/vsttypes.h"
#include "dsp/band_state.h"

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

/// @brief Format a shape slot value as a string based on distortion type.
/// @return true if a custom format was applied, false to fall through to default.
static inline bool formatShapeSlot(DistortionType distType, int slot, float v,
                            Steinberg::Vst::String128 string) {
    if (distType == DistortionType::Temporal) {
        if (slot == 2) {
            static const char* const kWaveshapeNames[] = {
                "Tanh", "Atan", "Cubic", "Quintic",
                "RSqrt", "Erf", "HardClip", "Diode", "Tube"
            };
            int idx = std::clamp(static_cast<int>(v * 8.0f + 0.5f), 0, 8);
            const char* name = kWaveshapeNames[idx];
            for (int i = 0; name[i] && i < 127; ++i) {
                string[i] = static_cast<Steinberg::Vst::TChar>(name[i]);
                string[i + 1] = 0;
            }
            return true;
        }
        if (slot == 1 || slot == 5) {
            intToString128(static_cast<int>(std::round(v * 100.0f)), string);
            appendToString128(string, STR16("%"));
            return true;
        }
        if (slot == 3) {
            floatToString128(1.0 + v * 499.0, 0, string);
            appendToString128(string, STR16(" ms"));
            return true;
        }
        if (slot == 4) {
            floatToString128(10.0 + v * 4990.0, 0, string);
            appendToString128(string, STR16(" ms"));
            return true;
        }
        if (slot == 7) {
            floatToString128(1.0 + v * 499.0, 0, string);
            appendToString128(string, STR16(" ms"));
            return true;
        }
    }

    if (distType == DistortionType::RingSaturation) {
        if (slot == 1) {
            int stages = 1 + static_cast<int>(v * 3.0f + 0.5f);
            intToString128(stages, string);
            appendToString128(string, STR16("x"));
            return true;
        }
        if (slot == 2) {
            static const char* const kWaveshapeNames[] = {
                "Tanh", "Atan", "Cubic", "Quintic",
                "RSqrt", "Erf", "HardClip", "Diode", "Tube"
            };
            int idx = std::clamp(static_cast<int>(v * 8.0f + 0.5f), 0, 8);
            const char* name = kWaveshapeNames[idx];
            for (int i = 0; name[i] && i < 127; ++i) {
                string[i] = static_cast<Steinberg::Vst::TChar>(name[i]);
                string[i + 1] = 0;
            }
            return true;
        }
        if (slot == 4) {
            float bias = v * 2.0f - 1.0f;
            floatToString128(bias, 2, string);
            return true;
        }
        if (slot == 6) {
            float freqHz = 20.0f * std::pow(250.0, static_cast<double>(v));
            if (freqHz < 100.0f)
                floatToString128(freqHz, 1, string);
            else
                floatToString128(freqHz, 0, string);
            appendToString128(string, STR16(" Hz"));
            return true;
        }
    }

    if (distType == DistortionType::Bitcrush) {
        if (slot == 0) {
            floatToString128(1.0 + v * 15.0, 1, string);
            appendToString128(string, STR16(" bit"));
            return true;
        }
        if (slot == 1) {
            intToString128(static_cast<int>(std::round(v * 100.0f)), string);
            appendToString128(string, STR16("%"));
            return true;
        }
        if (slot == 3) {
            intToString128(static_cast<int>(std::round(v * 100.0f)), string);
            appendToString128(string, STR16("%"));
            return true;
        }
    }

    return false;
}

} // namespace Disrumpo
