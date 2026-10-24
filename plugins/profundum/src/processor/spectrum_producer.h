#pragma once

// ==============================================================================
// Profundum Phase 12 - Profundum::ProfundumSpectrumProducer (FR-071, FR-073 -
// FR-075; plan 4.3, 5.2 - 5.4)
// ==============================================================================
// Plugin-local, header-only. The processor owns one. Once per completed engine
// control interval it picks the focus voice (plan 4.3.2), feeds that voice's
// character and Anchor taps into a pitch-synchronous window of
// N = clamp(round(8 fs / f0), 64, 2^20) samples, and at the window's end stages
// one ProfundumSpectrumFrame: the power at n * f0 for the first count partials
// (plan 5.3), measured by a double-precision Goertzel bank under a periodic
// Hann window, plus the unwindowed Anchor power.
//
// P_n = 8 (|L_n|^2 + |R_n|^2) / N^2   (a centred sine of amplitude a reads a^2)
// P_anchor = (sum aL^2 + sum aR^2) / N
//
// RT: fixed-size members only; no allocation after construction, no locks, no
// exceptions, no I/O. No transcendental call per sample: the Hann window is a
// rotating phasor (renormalised every 4096 samples) and the Goertzel
// coefficients come from the Chebyshev recurrence, so std::cos / std::sin run
// only at window start. Finiteness is checked by Krate::DSP::detail::isFinite
// (bit pattern, fast-math immune) only.
// ==============================================================================

#include "processor/goertzel_bank_simd.h"
#include "processor/spectrum_frame.h"

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/systems/profundum_engine.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <span>

namespace Profundum {

class ProfundumSpectrumProducer {
public:
    static constexpr int kPeriodsPerWindow = 8;                   // K (FR-075)
    static constexpr double kPitchStableTolerance = 1.0e-3;       // relative f0 drift flag (plan 5.2)
    static constexpr double kSilenceRepublishSeconds = 0.25;      // a silent producer re-publishes silence at 4 Hz
    static constexpr std::uint32_t kMinWindowSamples = 64;
    static constexpr std::uint32_t kMaxWindowSamples = 1u << 20;  // 2^20 (plan 5.2)
    static constexpr std::size_t kIntervalSamples = 32;           // engine control interval (plan 4.3.1)

    /// Control thread: stores fs, then reset().
    void prepare(double sampleRate) noexcept {
        fs_ = (Krate::DSP::detail::isFinite(sampleRate) && sampleRate > 0.0) ? sampleRate : 48000.0;
        silenceRepublishSamples_ = static_cast<std::uint64_t>(std::llround(kSilenceRepublishSeconds * fs_));
        reset();
    }

    /// Bounded: window off, focus none, note-sequence table cleared. sequence_ stays monotonic per producer.
    void reset() noexcept {
        windowActive_ = false;
        N_ = 0;
        k_ = 0;
        count_ = 0;
        f0Latched_ = 0.0;
        maxRelDev_ = 0.0;
        invalid_ = false;
        poly_ = false;
        coef_.fill(0.0);
        for (std::size_t h = 0; h < 2; ++h) {
            gL_[h].fill(0.0);
            gR_[h].fill(0.0);
        }
        hannC_ = 1.0;
        hannS_ = 0.0;
        rotC_ = 1.0;
        rotS_ = 0.0;
        anchorSq_ = 0.0;
        noteSeq_.fill(0);
        arrivalCounter_ = 0;
        focus_ = -1;
        focusOfWindow_ = -1;
        staged_ = ProfundumSpectrumFrame{};
        hasStaged_ = false;
        windows_ = 0;
        invalidWindows_ = 0;
        silentReported_ = false;
        silentSince_ = 0;
    }

    /// Audio thread, before engine noteOn: noteSeq_[note] = ++arrivalCounter_.
    void noteOnArrived(std::uint8_t note) noexcept {
        if (note < noteSeq_.size())
            noteSeq_[note] = ++arrivalCounter_;
    }

    /// Audio thread, once per completed engine interval (controlGridPhase() == 0 after a render): focus + feed.
    void onInterval(const Krate::DSP::ProfundumEngine& engine) noexcept {
        using Engine = Krate::DSP::ProfundumEngine;
        const bool poly = engine.appliedVoiceMode() == Engine::VoiceMode::Poly;

        // Focus rule (plan 4.3.2).
        int focus = -1;
        if (!poly) {
            focus = engine.voiceRenderedThisInterval(0) ? 0 : -1;
        } else {
            std::uint64_t bestSeq = 0;
            for (std::size_t v = 0; v < Engine::kMaxVoices; ++v) {
                if (!engine.voiceRenderedThisInterval(v) || !engine.voiceForDisplay(v).held())
                    continue;
                const int note = engine.voiceMidiNote(v);
                if (note < 0 || note >= static_cast<int>(noteSeq_.size()))
                    continue;
                const std::uint64_t seq = noteSeq_[static_cast<std::size_t>(note)];
                if (focus < 0 || seq > bestSeq) {
                    focus = static_cast<int>(v);
                    bestSeq = seq;
                }
            }
            if (focus < 0 && focus_ >= 0 && engine.voiceRenderedThisInterval(static_cast<std::size_t>(focus_)))
                focus = focus_;
        }
        focus_ = focus;

        const std::uint64_t end = engine.samplePosition();
        if (focus < 0) {
            feedSilence(end);
            return;
        }
        const Krate::DSP::ProfundumVoice& voice = engine.voiceForDisplay(static_cast<std::size_t>(focus));
        if (voice.intervalSamplesRendered() != kIntervalSamples)
            return;  // incomplete interval: not fed
        feedInterval(voice.characterTapL(), voice.characterTapR(), voice.anchorTapL(), voice.anchorTapR(),
                     voice.currentCoreF0(), static_cast<std::uint8_t>(focus), poly,
                     end >= kIntervalSamples ? end - kIntervalSamples : 0);
    }

    /// The seam that unit tests inject through: one interval's taps of the focus voice; f0Hz = currentCoreF0().
    void feedInterval(std::span<const float> cL, std::span<const float> cR, std::span<const float> aL,
                      std::span<const float> aR, float f0Hz, std::uint8_t focusVoice, bool poly,
                      std::uint64_t intervalStartSample) noexcept {
        using Krate::DSP::detail::isFinite;
        const std::size_t n = std::min(std::min(cL.size(), cR.size()), std::min(aL.size(), aR.size()));
        const double f0 = static_cast<double>(f0Hz);
        if (!f0Usable(f0)) {
            feedSilence(intervalStartSample + n);
            return;
        }
        if (!windowActive_ || static_cast<int>(focusVoice) != focusOfWindow_) {
            startWindow(f0, focusVoice, poly);
        } else {
            const double dev = std::abs(f0 / f0Latched_ - 1.0);
            if (isFinite(dev))
                maxRelDev_ = std::max(maxRelDev_, dev);
            else
                invalid_ = true;
        }

        // Segments of at most kIntervalSamples that never cross a window end: first the windowed inputs, then the
        // Goertzel bank over the whole segment (goertzelBankRun, the Highway kernel, T040).
        std::array<double, kIntervalSamples> xl{};
        std::array<double, kIntervalSamples> xr{};
        std::size_t i = 0;
        while (i < n) {
            const std::size_t m = std::min({n - i, kIntervalSamples, static_cast<std::size_t>(N_ - k_)});
            for (std::size_t j = 0; j < m; ++j) {
                const float l = cL[i + j];
                const float r = cR[i + j];
                const float al = aL[i + j];
                const float ar = aR[i + j];
                xl[j] = 0.0;
                xr[j] = 0.0;
                if (isFinite(l) && isFinite(r) && isFinite(al) && isFinite(ar)) {
                    const double w = 0.5 - 0.5 * hannC_;
                    xl[j] = w * static_cast<double>(l);
                    xr[j] = w * static_cast<double>(r);
                    const double dal = static_cast<double>(al);
                    const double dar = static_cast<double>(ar);
                    anchorSq_ += dal * dal + dar * dar;
                } else {
                    invalid_ = true;  // the window publishes nothing; the sample is taken as 0
                }

                // Periodic Hann phasor rotation (plan 5.2).
                const double c = hannC_ * rotC_ - hannS_ * rotS_;
                const double s = hannS_ * rotC_ + hannC_ * rotS_;
                hannC_ = c;
                hannS_ = s;
                ++k_;
                if ((k_ & 4095u) == 0u) {
                    const double mag2 = hannC_ * hannC_ + hannS_ * hannS_;
                    if (mag2 > 0.0) {
                        const double inv = 1.0 / std::sqrt(mag2);
                        hannC_ *= inv;
                        hannS_ *= inv;
                    }
                }
            }
            goertzelBankRun(xl.data(), xr.data(), m, coef_.data(), gL_[0].data(), gL_[1].data(), gR_[0].data(),
                            gR_[1].data(), static_cast<std::size_t>(count_));
            i += m;
            if (k_ == N_) {
                finishWindow(intervalStartSample + i);
                startWindow(f0, focusVoice, poly);
            }
        }
    }

    /// No focus voice this interval (silence path, plan 4.3.4).
    void feedSilence(std::uint64_t intervalEndSample) noexcept {
        windowActive_ = false;
        focusOfWindow_ = -1;
        const bool due = !silentReported_ || intervalEndSample < silentSince_ ||
                         intervalEndSample - silentSince_ >= silenceRepublishSamples_;
        if (!due)
            return;
        ProfundumSpectrumFrame f{};
        f.sequence = ++sequence_;
        f.sampleRate = static_cast<float>(fs_);
        f.f0Hz = 0.0f;
        f.flags = kFrameFlagSilent;
        f.numPartials = 0;
        f.windowEndSample = intervalEndSample;
        staged_ = f;
        hasStaged_ = true;
        silentReported_ = true;
        silentSince_ = intervalEndSample;
    }

    /// Audio thread, end of process(): copies the newest staged frame once and returns true; false when none is staged.
    [[nodiscard]] bool takeFrame(ProfundumSpectrumFrame& out) noexcept {
        if (!hasStaged_)
            return false;
        out = staged_;
        hasStaged_ = false;
        return true;
    }

    // test reads
    [[nodiscard]] std::uint32_t windowsCompleted() const noexcept { return windows_; }  // SC-030
    [[nodiscard]] int focusVoice() const noexcept { return focus_; }                     // -1 none (SC-025)
    [[nodiscard]] std::uint32_t invalidWindows() const noexcept { return invalidWindows_; }  // windows dropped as non-finite

private:
    [[nodiscard]] bool f0Usable(double f0) const noexcept {
        return Krate::DSP::detail::isFinite(f0) && f0 > 0.0 && f0 < 0.5 * fs_;
    }

    /// Plan 5.3: count = min(64, floor((fs/2) / f0)), minus 1 if count * f0 >= fs/2. f0 is usable here.
    [[nodiscard]] int partialCount(double f0) const noexcept {
        const double nyquist = 0.5 * fs_;
        const double maxCount = static_cast<double>(kSpectrumMaxPartials);
        int c = static_cast<int>(std::min(maxCount, std::floor(nyquist / f0)));
        if (static_cast<double>(c) * f0 >= nyquist)
            --c;
        return std::max(c, 0);
    }

    void startWindow(double f0, std::uint8_t focusVoice, bool poly) noexcept {
        const double nD = std::round(static_cast<double>(kPeriodsPerWindow) * fs_ / f0);
        if (!(nD >= static_cast<double>(kMinWindowSamples)))
            N_ = kMinWindowSamples;
        else if (nD > static_cast<double>(kMaxWindowSamples))
            N_ = kMaxWindowSamples;
        else
            N_ = static_cast<std::uint32_t>(nD);
        count_ = partialCount(f0);

        // Goertzel coefficients c_n = 2 cos(n w1) by the Chebyshev recurrence (plan 5.2).
        const double w1 = 2.0 * std::numbers::pi * f0 / fs_;
        const double c1 = std::cos(w1);
        double cosPrev = 1.0;  // cos(0)
        double cosCur = c1;    // cos(w1)
        coef_.fill(0.0);
        for (int p = 0; p < count_; ++p) {
            coef_[static_cast<std::size_t>(p)] = 2.0 * cosCur;
            const double cosNext = 2.0 * c1 * cosCur - cosPrev;
            cosPrev = cosCur;
            cosCur = cosNext;
        }
        for (std::size_t h = 0; h < 2; ++h) {
            gL_[h].fill(0.0);
            gR_[h].fill(0.0);
        }

        const double delta = 2.0 * std::numbers::pi / static_cast<double>(N_);
        rotC_ = std::cos(delta);
        rotS_ = std::sin(delta);
        hannC_ = 1.0;
        hannS_ = 0.0;
        anchorSq_ = 0.0;
        k_ = 0;
        f0Latched_ = f0;
        maxRelDev_ = 0.0;
        invalid_ = false;
        poly_ = poly;
        focusOfWindow_ = static_cast<int>(focusVoice);
        windowActive_ = true;
    }

    /// Converts a measured power to the published float: non-finite or out of float range fails; values
    /// below the smallest normal float (and rounding negatives) publish as exactly 0, so no subnormal leaves.
    [[nodiscard]] static bool publishPower(double p, float& out) noexcept {
        if (!Krate::DSP::detail::isFinite(p) || p > static_cast<double>(std::numeric_limits<float>::max()))
            return false;
        out = p >= static_cast<double>(std::numeric_limits<float>::min()) ? static_cast<float>(p) : 0.0f;
        return true;
    }

    void finishWindow(std::uint64_t windowEndSample) noexcept {
        windowActive_ = false;
        bool ok = !invalid_;
        ProfundumSpectrumFrame f{};
        const double invN = 1.0 / static_cast<double>(N_);
        const double scale = 8.0 * invN * invN;
        const auto& s1L = gL_[0];
        const auto& s2L = gL_[1];
        const auto& s1R = gR_[0];
        const auto& s2R = gR_[1];
        for (std::size_t p = 0; ok && p < static_cast<std::size_t>(count_); ++p) {
            const double magL = s1L[p] * s1L[p] + s2L[p] * s2L[p] - coef_[p] * s1L[p] * s2L[p];
            const double magR = s1R[p] * s1R[p] + s2R[p] * s2R[p] - coef_[p] * s1R[p] * s2R[p];
            ok = publishPower(scale * (magL + magR), f.partialPower[p]);
        }
        if (ok)
            ok = publishPower(anchorSq_ * invN, f.anchorPower);
        if (!ok) {
            ++invalidWindows_;
            return;
        }
        f.sequence = ++sequence_;
        f.sampleRate = static_cast<float>(fs_);
        f.f0Hz = static_cast<float>(f0Latched_);
        f.windowSamples = N_;
        f.numPartials = static_cast<std::uint8_t>(count_);
        f.flags = static_cast<std::uint8_t>((maxRelDev_ > kPitchStableTolerance ? kFrameFlagPitchUnstable : 0) |
                                            (poly_ ? kFrameFlagPoly : 0));
        f.focusVoice = static_cast<std::uint8_t>(focusOfWindow_ < 0 ? 0 : focusOfWindow_);
        f.windowEndSample = windowEndSample;
        staged_ = f;
        hasStaged_ = true;
        ++windows_;
        silentReported_ = false;
    }

    // window
    double fs_ = 48000.0;
    bool windowActive_ = false;
    std::uint32_t N_ = 0, k_ = 0;
    int count_ = 0;
    double f0Latched_ = 0.0, maxRelDev_ = 0.0;
    bool invalid_ = false;
    bool poly_ = false;  // applied voice mode at window start (kFrameFlagPoly)
    std::array<double, kSpectrumMaxPartials> coef_{};
    // Goertzel state per channel: gX_[0] = s[k-1], gX_[1] = s[k-2].
    std::array<std::array<double, kSpectrumMaxPartials>, 2> gL_{}, gR_{};
    double hannC_ = 1.0, hannS_ = 0.0, rotC_ = 1.0, rotS_ = 0.0;  // periodic Hann phasor (plan 5.2)
    double anchorSq_ = 0.0;
    // focus
    std::array<std::uint64_t, 128> noteSeq_{};
    std::uint64_t arrivalCounter_ = 0;
    int focus_ = -1;
    int focusOfWindow_ = -1;
    // output
    ProfundumSpectrumFrame staged_{};
    bool hasStaged_ = false;
    std::uint32_t sequence_ = 0, windows_ = 0;
    std::uint32_t invalidWindows_ = 0;
    bool silentReported_ = false;
    std::uint64_t silentSince_ = 0;
    std::uint64_t silenceRepublishSamples_ = 12000;
};

}  // namespace Profundum
