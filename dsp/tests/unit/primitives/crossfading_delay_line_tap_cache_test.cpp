// ==============================================================================
// CrossfadingDelayLine's cached live tap against the frozen read, bit for bit
// ==============================================================================
// Outside a crossfade CrossfadingDelayLine::read() reads ONE tap whose position does not move (setDelaySamples only
// retargets the idle tap), yet it re-derived that tap's clamp / floor / index split on every sample through the
// out-of-line DelayLine::readLinear(float). The live tap's DelayLine::LinearTap is now cached wherever the live position
// or its identity changes. FeedbackEcology_CpuBudget (a), paired pinned A/B, 3 rounds (2026-11-13): 104 187-109 956
// ns/block with the per-sample split, 80 046-83 158 with the cache, against the 106 667 ns gated line.
//
// The claim, bit for bit: the live class against a frozen copy of the class as of fd0de402, over one long
// randomised call sequence per rate: smooth moves, jumps that start crossfades, retargets mid-crossfade, delays past
// both clamps, integer delays, snaps (mid-crossfade too, and FeedbackEcology's snap-to-current), resets,
// crossfade-time changes, write-only spans and a re-prepare to a shorter buffer. FeedbackEcology end to end was
// measured bit-identical to fd0de402 when the cache landed (MSVC /fp:fast and /fp:precise); it is not pinned here,
// because a stored render would assert one compiler version's floating point forever.
// A bit-identity claim between separately compiled computations: it lives in dsp_ieee_tests, /fp:precise on MSVC.

#include <krate/dsp/core/crossfade_utils.h>
#include <krate/dsp/primitives/crossfading_delay_line.h>
#include <krate/dsp/primitives/delay_line.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

using Krate::DSP::CrossfadingDelayLine;
using Krate::DSP::DelayLine;

namespace {

// =============================================================================
// CrossfadingDelayLine as of fd0de402, verbatim but for the name and the comments
// =============================================================================
class FrozenXfadeDelay {
public:
    static constexpr float kDefaultCrossfadeTimeMs = 20.0f;
    static constexpr float kMinCrossfadeTimeMs = 5.0f;
    static constexpr float kMaxCrossfadeTimeMs = 100.0f;
    static constexpr float kCrossfadeThresholdSamples = 100.0f;

    void prepare(double sampleRate, float maxDelaySeconds) noexcept {
        sampleRate_ = sampleRate;
        delayLine_.prepare(sampleRate, maxDelaySeconds);
        tapADelaySamples_ = 0.0f;
        tapBDelaySamples_ = 0.0f;
        targetDelaySamples_ = 0.0f;
        tapAGain_ = 1.0f;
        tapBGain_ = 0.0f;
        activeIsTapA_ = true;
        crossfading_ = false;
        crossfadePosition_ = 0.0f;
        setCrossfadeTime(kDefaultCrossfadeTimeMs);
    }

    void reset() noexcept {
        delayLine_.reset();
        tapADelaySamples_ = targetDelaySamples_;
        tapBDelaySamples_ = targetDelaySamples_;
        tapAGain_ = 1.0f;
        tapBGain_ = 0.0f;
        activeIsTapA_ = true;
        crossfading_ = false;
        crossfadePosition_ = 0.0f;
    }

    void setCrossfadeTime(float timeMs) noexcept {
        crossfadeTimeMs_ = std::clamp(timeMs, kMinCrossfadeTimeMs, kMaxCrossfadeTimeMs);
        crossfadeIncrement_ = Krate::DSP::crossfadeIncrement(crossfadeTimeMs_, sampleRate_);
    }

    void setDelaySamples(float delaySamples) noexcept {
        const float clampedDelay = std::max(0.0f, delaySamples);
        targetDelaySamples_ = clampedDelay;
        if (activeIsTapA_) {
            tapBDelaySamples_ = clampedDelay;
        } else {
            tapADelaySamples_ = clampedDelay;
        }
        if (crossfading_) {
            return;
        }
        const float activePosition = activeIsTapA_ ? tapADelaySamples_ : tapBDelaySamples_;
        const float driftFromActive = std::abs(clampedDelay - activePosition);
        if (driftFromActive >= kCrossfadeThresholdSamples) {
            crossfading_ = true;
            crossfadePosition_ = 0.0f;
        }
    }

    void setDelayMs(float delayMs) noexcept {
        setDelaySamples(delayMs * 0.001f * static_cast<float>(sampleRate_));
    }

    void snapToDelaySamples(float delaySamples) noexcept {
        const float clampedDelay = std::max(0.0f, delaySamples);
        targetDelaySamples_ = clampedDelay;
        tapADelaySamples_ = clampedDelay;
        tapBDelaySamples_ = clampedDelay;
        tapAGain_ = 1.0f;
        tapBGain_ = 0.0f;
        activeIsTapA_ = true;
        crossfading_ = false;
        crossfadePosition_ = 0.0f;
    }

    void write(float sample) noexcept { delayLine_.write(sample); }

    [[nodiscard]] float read() noexcept {
        if (!crossfading_) {
            return delayLine_.readLinear(activeIsTapA_ ? tapADelaySamples_ : tapBDelaySamples_);
        }
        const float tapAOutput = delayLine_.readLinear(tapADelaySamples_);
        const float tapBOutput = delayLine_.readLinear(tapBDelaySamples_);
        float output = tapAOutput * tapAGain_ + tapBOutput * tapBGain_;
        if (crossfading_) {
            crossfadePosition_ += crossfadeIncrement_;
            float fadeOut;
            float fadeIn;
            Krate::DSP::equalPowerGains(crossfadePosition_, fadeOut, fadeIn);
            if (activeIsTapA_) {
                tapAGain_ = fadeOut;
                tapBGain_ = fadeIn;
                if (crossfadePosition_ >= 1.0f) {
                    tapAGain_ = 0.0f;
                    tapBGain_ = 1.0f;
                    activeIsTapA_ = false;
                    crossfading_ = false;
                    crossfadePosition_ = 0.0f;
                    tapADelaySamples_ = targetDelaySamples_;
                }
            } else {
                tapBGain_ = fadeOut;
                tapAGain_ = fadeIn;
                if (crossfadePosition_ >= 1.0f) {
                    tapBGain_ = 0.0f;
                    tapAGain_ = 1.0f;
                    activeIsTapA_ = true;
                    crossfading_ = false;
                    crossfadePosition_ = 0.0f;
                    tapBDelaySamples_ = targetDelaySamples_;
                }
            }
        }
        return output;
    }

    [[nodiscard]] float process(float input) noexcept {
        write(input);
        return read();
    }

    [[nodiscard]] bool isCrossfading() const noexcept { return crossfading_; }

    [[nodiscard]] float getCurrentDelaySamples() const noexcept {
        return tapADelaySamples_ * tapAGain_ + tapBDelaySamples_ * tapBGain_;
    }

private:
    DelayLine delayLine_;
    float tapADelaySamples_ = 0.0f;
    float tapBDelaySamples_ = 0.0f;
    float targetDelaySamples_ = 0.0f;
    float tapAGain_ = 1.0f;
    float tapBGain_ = 0.0f;
    bool activeIsTapA_ = true;
    bool crossfading_ = false;
    float crossfadePosition_ = 0.0f;
    float crossfadeIncrement_ = 0.01f;
    float crossfadeTimeMs_ = kDefaultCrossfadeTimeMs;
    double sampleRate_ = 44100.0;
};

[[nodiscard]] std::uint32_t bitsOf(float v) noexcept { return std::bit_cast<std::uint32_t>(v); }

struct SequenceCounts {
    std::size_t samplesCompared = 0;
    std::size_t cachedReads = 0;       ///< reads outside a crossfade: the cached path
    std::size_t crossfadeStarts = 0;
    std::size_t crossfadeEnds = 0;
    std::size_t retargetsMidFade = 0;
    std::size_t pastMaxRequests = 0;   ///< delay requests past the buffer's maximum (the read clamp)
    std::size_t negativeRequests = 0;  ///< below zero (setDelaySamples' clamp)
    std::size_t integerRequests = 0;
    std::size_t snaps = 0;
    std::size_t snapsMidFade = 0;
    std::size_t resets = 0;
    std::size_t fadeTimeChanges = 0;
    std::size_t writeOnlySpans = 0;
    std::size_t mismatches = 0;
    std::size_t firstMismatch = 0;
};

/// One randomised sequence against both classes. Every call goes to both; every sample's read is compared.
void runSequence(double fs, std::uint32_t seed, SequenceCounts& c) {
    CrossfadingDelayLine live;
    FrozenXfadeDelay frozen;
    float maxSeconds = 0.25f;
    live.prepare(fs, maxSeconds);
    frozen.prepare(fs, maxSeconds);

    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    std::uniform_real_distribution<float> bipolar(-1.0f, 1.0f);

    constexpr std::size_t kChunk = 64;
    constexpr std::size_t kChunks = 9000;  // 576 000 samples per rate
    const auto maxSamples = [&] { return static_cast<float>(fs) * maxSeconds; };
    float phase = 0.0f;
    float base = 0.4f * maxSamples();

    const auto mismatch = [&](float a, float b) {
        if (bitsOf(a) != bitsOf(b)) {
            if (c.mismatches == 0) c.firstMismatch = c.samplesCompared;
            ++c.mismatches;
        }
    };

    for (std::size_t chunk = 0; chunk < kChunks; ++chunk) {
        bool readOnlyChunk = false;
        if (chunk == kChunks / 2) {
            // A re-prepare to a shorter buffer, mid-sequence (the live tap sat hundreds of samples back): both taps
            // return to 0 and the read clamp's bound changes. The chunk after it only reads, so a cached tap left
            // over from before the prepare would be read rather than replaced by the next crossfade.
            maxSeconds = 0.05f;
            live.prepare(fs, maxSeconds);
            frozen.prepare(fs, maxSeconds);
            base = 0.4f * maxSamples();
            readOnlyChunk = true;
        }

        const bool wasFading = frozen.isCrossfading();
        const float r = readOnlyChunk ? 1.0f : unit(rng);  // 1.0f: no call before the chunk's reads
        bool writeOnly = false;
        bool hasRequest = true;
        bool inMs = false;
        float request = 0.0f;
        if (r < 0.50f) {
            // A smooth move around a wandering centre, at control rate (FeedbackEcology's pattern).
            phase += 0.05f;
            // Mostly under the 100-sample crossfade threshold, so the live tap is read through the cache for long
            // stretches; the drift of the centre still starts a fade now and then.
            base = std::clamp(base + 4.0f * bipolar(rng), 0.0f, maxSamples());
            request = base + 30.0f * std::sin(phase) + bipolar(rng);
            inMs = unit(rng) < 0.5f;
        } else if (r < 0.55f) {
            request = unit(rng) * 1.3f * maxSamples();  // a jump; past the maximum ~23 % of the time
        } else if (r < 0.58f) {
            request = -unit(rng) * 50.0f;
        } else if (r < 0.61f) {
            request = std::floor(unit(rng) * maxSamples());  // exactly on an integer: frac == 0
            ++c.integerRequests;
        } else {
            hasRequest = false;
        }

        if (hasRequest) {
            if (inMs) {
                const float ms = request * 1000.0f / static_cast<float>(fs);
                live.setDelayMs(ms);
                frozen.setDelayMs(ms);
            } else {
                live.setDelaySamples(request);
                frozen.setDelaySamples(request);
            }
            if (request > maxSamples()) ++c.pastMaxRequests;
            if (request < 0.0f) ++c.negativeRequests;
            if (wasFading) ++c.retargetsMidFade;
            if (!wasFading && frozen.isCrossfading()) ++c.crossfadeStarts;
        } else if (r < 0.625f) {
            const float to = unit(rng) * 1.1f * maxSamples();
            live.snapToDelaySamples(to);
            frozen.snapToDelaySamples(to);
            ++c.snaps;
            if (wasFading) ++c.snapsMidFade;
        } else if (r < 0.64f) {
            // FeedbackEcology::clearLoopAudio's snap to the current (gain-weighted) position.
            const float cur = frozen.getCurrentDelaySamples();
            mismatch(live.getCurrentDelaySamples(), cur);
            live.snapToDelaySamples(cur);
            frozen.snapToDelaySamples(cur);
            ++c.snaps;
            if (wasFading) ++c.snapsMidFade;
        } else if (r < 0.65f) {
            live.reset();
            frozen.reset();
            ++c.resets;
        } else if (r < 0.665f) {
            // 2-20 ms with the odd 150: past both crossfade-time clamps.
            const float ms = (unit(rng) < 0.1f) ? 150.0f : 2.0f + unit(rng) * 18.0f;
            live.setCrossfadeTime(ms);
            frozen.setCrossfadeTime(ms);
            ++c.fadeTimeChanges;
        } else if (r < 0.68f) {
            writeOnly = true;  // FeedbackEcology's read-mute window: write, no read
            ++c.writeOnlySpans;
        }

        for (std::size_t s = 0; s < kChunk; ++s) {
            const float x = bipolar(rng);
            if (writeOnly) {
                live.write(x);
                frozen.write(x);
                continue;
            }
            const bool fadingBefore = frozen.isCrossfading();
            if (!fadingBefore) ++c.cachedReads;
            mismatch(live.process(x), frozen.process(x));
            ++c.samplesCompared;
            if (live.isCrossfading() != frozen.isCrossfading()) ++c.mismatches;
            if (fadingBefore && !frozen.isCrossfading()) ++c.crossfadeEnds;
        }
    }
}

constexpr std::array<double, 2> kRates{44100.0, 96000.0};

}  // namespace

TEST_CASE("CrossfadingDelayLine tap cache: every read is the frozen read bit for bit",
          "[primitives][crossfading_delay][tap_cache]") {
    for (const double fs : kRates) {
        CAPTURE(fs);
        SequenceCounts c;
        runSequence(fs, fs > 50000.0 ? 0xFADEu : 0xC0DEu, c);
        CAPTURE(c.samplesCompared, c.cachedReads, c.crossfadeStarts, c.crossfadeEnds, c.retargetsMidFade,
                c.pastMaxRequests, c.negativeRequests, c.integerRequests, c.snaps, c.snapsMidFade, c.resets,
                c.fadeTimeChanges, c.writeOnlySpans, c.firstMismatch);

        CHECK(c.mismatches == 0u);

        // The sequence reached every path it exists to compare (floors at about half of what 0xC0DE / 0xFADE reach).
        CHECK(c.samplesCompared > 500000u);
        CHECK(c.cachedReads > c.samplesCompared / 5u);
        CHECK(c.cachedReads < c.samplesCompared);
        CHECK(c.crossfadeStarts > 250u);
        CHECK(c.crossfadeEnds > 125u);
        CHECK(c.retargetsMidFade > 1000u);
        CHECK(c.pastMaxRequests > 40u);
        CHECK(c.negativeRequests > 100u);
        CHECK(c.integerRequests > 100u);
        CHECK(c.snapsMidFade > 50u);
        CHECK(c.resets > 40u);
        CHECK(c.fadeTimeChanges > 50u);
        CHECK(c.writeOnlySpans > 50u);
    }
}
