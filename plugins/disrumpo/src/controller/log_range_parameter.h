#pragma once

// ==============================================================================
// LogRangeParameter: a RangeParameter whose normalized <-> plain mapping is a
// log curve from state_codec.h (crossover log10, sweep frequency log2).
// ==============================================================================
// The processor decodes these parameters on a log scale. A plain linear
// RangeParameter made the host's text, its reset-to-default and every
// controller toPlain / toNormalized disagree with the Hz the DSP runs
// (audit D-086, D-051, D-101). Same ID, flags and min/max as the RangeParameter
// it replaces; the normalized value keeps the processor's meaning.
// toString / fromString are RangeParameter's, which go through the virtual
// toPlain / toNormalized below.
// ==============================================================================

#include "public.sdk/source/vst/vstparameters.h"

namespace Disrumpo {

class LogRangeParameter : public Steinberg::Vst::RangeParameter {
public:
    using MapFn = float (*)(float) noexcept;

    LogRangeParameter(const Steinberg::Vst::TChar* title, Steinberg::Vst::ParamID tag,
                      const Steinberg::Vst::TChar* units, Steinberg::Vst::ParamValue minPlain,
                      Steinberg::Vst::ParamValue maxPlain, Steinberg::Vst::ParamValue defaultPlain,
                      MapFn normToPlain, MapFn plainToNorm, Steinberg::int32 flags)
        : RangeParameter(title, tag, units, minPlain, maxPlain, defaultPlain, 0, flags),
          normToPlain_(normToPlain),
          plainToNorm_(plainToNorm) {
        // The base constructor normalized the default with its linear map.
        info.defaultNormalizedValue = valueNormalized = LogRangeParameter::toNormalized(defaultPlain);
    }

    Steinberg::Vst::ParamValue toPlain(Steinberg::Vst::ParamValue valueNormalized) const override {
        return static_cast<Steinberg::Vst::ParamValue>(normToPlain_(static_cast<float>(valueNormalized)));
    }

    Steinberg::Vst::ParamValue toNormalized(Steinberg::Vst::ParamValue plainValue) const override {
        return static_cast<Steinberg::Vst::ParamValue>(plainToNorm_(static_cast<float>(plainValue)));
    }

    OBJ_METHODS(LogRangeParameter, RangeParameter)

private:
    MapFn normToPlain_;
    MapFn plainToNorm_;
};

}  // namespace Disrumpo
