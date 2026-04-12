#pragma once

// Backward-compat helper for Phase 3 tests that used SharedParams API.
// Sets the given parameters on ALL 32 pad configs so any MIDI note
// in [36, 67] will use these values (equivalent to old shared behavior).

#include "voice_pool/voice_pool.h"
#include "dsp/pad_config.h"
#include "dsp/exciter_type.h"
#include "dsp/body_model_type.h"

namespace Membrum {
namespace TestHelpers {

inline void setAllPadsVoiceParams(VoicePool& pool,
                                  float material, float size,
                                  float decay, float strikePos,
                                  float level) noexcept
{
    for (int p = 0; p < kNumPads; ++p)
    {
        pool.setPadConfigField(p, kPadMaterial, material);
        pool.setPadConfigField(p, kPadSize, size);
        pool.setPadConfigField(p, kPadDecay, decay);
        pool.setPadConfigField(p, kPadStrikePosition, strikePos);
        pool.setPadConfigField(p, kPadLevel, level);
    }
}

inline void setAllPadsExciterType(VoicePool& pool, ExciterType type) noexcept
{
    for (int p = 0; p < kNumPads; ++p)
        pool.setPadConfigSelector(p, kPadExciterType, static_cast<int>(type));
}

inline void setAllPadsBodyModel(VoicePool& pool, BodyModelType model) noexcept
{
    for (int p = 0; p < kNumPads; ++p)
        pool.setPadConfigSelector(p, kPadBodyModel, static_cast<int>(model));
}

} // namespace TestHelpers
} // namespace Membrum
