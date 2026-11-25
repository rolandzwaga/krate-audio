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

/// T2.6 (MB-053): main voices that still render while their slot is not
/// Active (a "ghost": no auto-release, note-off or voice count reaches it).
inline int countGhosts(VoicePool& pool) noexcept
{
    int n = 0;
    for (int s = 0; s < kMaxVoices; ++s)
    {
        if (pool.mainVoiceRef(s).isActive() &&
            pool.voiceMeta(s).state != VoiceSlotState::Active)
            ++n;
    }
    return n;
}

} // namespace TestHelpers
} // namespace Membrum
