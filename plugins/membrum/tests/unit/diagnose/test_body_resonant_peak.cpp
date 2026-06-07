// Diagnostic spike (N-1 verification): measure the modal body bank's REALIZED
// peak relative to the Sum|a_k| impulse bound (getInputGainSum) that the current
// gain-staging code normalizes against:
//     bank.setOutputGain(kBodyHeadroom / getInputGainSum())
//
// The audit (AUDIT-signal-path-2026-06-07.md, open item N-1) claims the realized
// resonant peak runs ~10x over that bound. Research (J.O. Smith / Steiglitz
// "constant peak-gain resonator", CCRMA) predicts a single 2-pole mode's peak
// gain is 2/(1-R^2) -- it grows without bound as R->1 (long decay / high Q), and
// the t=0 in-phase impulse sum Sum|a_k| does NOT capture that resonant buildup.
//
// This spike measures, per modal body, the over-run factor
//     F = peak_raw / Sum|a_k|
// for three excitations (impulse / short strike / sustained sine) across a DECAY
// sweep, and prints the analytic worst-case max_k 2/(1-R_k^2). The decisive
// question for the N-1 fix is whether F is STABLE (a static per-body / analytic
// constant suffices) or VARIES with decay (a configure-time MEASURED
// normalization is required). It is read-only: no production code is changed.

#include <catch2/catch_test_macros.hpp>

#include "dsp/body_bank.h"
#include "dsp/body_model_type.h"
#include "dsp/voice_common_params.h"

#include <krate/dsp/processors/modal_resonator_bank.h>

#include <cmath>
#include <cstdio>
#include <numbers>
#include <vector>

using namespace Membrum;
using Krate::DSP::ModalResonatorBank;

namespace {

constexpr double kSR    = 48000.0;
constexpr int    kRender = 24000;  // 0.5 s
constexpr float  kBodyHeadroom = 0.25f;  // matches drum_voice.h

VoiceCommonParams makeParams(float decay, float modeStretch)
{
    VoiceCommonParams p;
    p.material    = 0.30f;
    p.size        = 0.80f;
    p.decay       = decay;
    p.strikePos   = 0.30f;
    p.level       = 1.0f;
    p.modeStretch = modeStretch;
    p.decaySkew   = 0.0f;
    p.airLoading  = 0.30f;
    return p;
}

// Configure the body fresh (clears sin/cos state) and run `excite` through the
// shared bank with the output stage set to RAW (gain 1.0, no soft-clip), so the
// returned peak is the true bank peak before any normalization / rail.
float measurePeakRaw(BodyBank& bb, const VoiceCommonParams& p,
                     const std::vector<float>& excite, float& sumAbsAOut)
{
    bb.configureForNoteOn(p, /*pitchEnvStartHz*/ 0.0f);
    auto& bank = bb.getSharedBank();
    bank.setOutputGain(1.0f);
    bank.setOutputSoftClipThreshold(0.0f);
    sumAbsAOut = bank.getInputGainSum();

    float peak = 0.0f;
    for (int i = 0; i < kRender; ++i) {
        const float in = (i < static_cast<int>(excite.size())) ? excite[i] : 0.0f;
        const float y = bb.processSample(in);
        peak = std::max(peak, std::abs(y));
    }
    return peak;
}

// Analytic worst-case single-mode resonant peak gain: max_k 2/(1-R_k^2), where
// R_k = exp(-(b1 + b3 f_k^2)/sr) is recovered from the public damping law +
// per-mode frequency getters.
float analyticMaxModeGain(const ModalResonatorBank& bank)
{
    const auto law = bank.getDampingLaw();
    const int n = bank.getNumModes();
    float worst = 0.0f;
    for (int k = 0; k < n; ++k) {
        const float f = bank.getModeFrequency(k);
        if (f <= 0.0f) continue;
        const float decayRate = law.b1 + law.b3 * f * f;
        const float R = std::exp(-decayRate / static_cast<float>(kSR));
        const float g = 2.0f / (1.0f - R * R);
        worst = std::max(worst, g);
    }
    return worst;
}

std::vector<float> impulseExc()
{
    std::vector<float> v(1, 1.0f);
    return v;
}

// ~2 ms unit-peak raised-cosine burst: a mallet-like multi-sample strike (the
// excitation class N-1 is about -- the bank rings UP over it rather than seeing
// a single in-phase impulse).
std::vector<float> strikeExc()
{
    const int len = static_cast<int>(0.002 * kSR);
    std::vector<float> v(static_cast<size_t>(len), 0.0f);
    for (int i = 0; i < len; ++i) {
        const float ph = static_cast<float>(i) / static_cast<float>(len);
        v[static_cast<size_t>(i)] =
            0.5f * (1.0f - std::cos(2.0f * std::numbers::pi_v<float> * ph));
    }
    return v;
}

// Sustained unit sine at the bank fundamental for 0.5 s: drives one mode toward
// its steady-state resonant peak (worst-case probe).
std::vector<float> sustainedExc(float f0)
{
    std::vector<float> v(static_cast<size_t>(kRender), 0.0f);
    for (int i = 0; i < kRender; ++i)
        v[static_cast<size_t>(i)] =
            std::sin(2.0f * std::numbers::pi_v<float> * f0
                     * static_cast<float>(i) / static_cast<float>(kSR));
    return v;
}

} // namespace

TEST_CASE("N-1 spike: modal body resonant peak vs Sum|a_k| bound", "[diagnose_n1]")
{
    struct BodyCase { const char* name; BodyModelType type; };
    const BodyCase bodies[] = {
        {"Membrane",  BodyModelType::Membrane},
        {"Plate",     BodyModelType::Plate},
        {"Shell",     BodyModelType::Shell},
        {"Bell",      BodyModelType::Bell},
        {"NoiseBody", BodyModelType::NoiseBody},
    };
    const float decays[]   = {0.20f, 0.50f, 0.85f, 0.98f};
    const float stretches[] = {1.0f, 1.5f};

    std::printf("\n[diagnose_n1] F = peak_raw / Sum|a_k|   (realized body peak with current code = F * %.2f)\n",
                static_cast<double>(kBodyHeadroom));
    std::printf("[diagnose_n1] %-9s %5s %6s | %7s %7s %7s | %8s\n",
                "body", "decay", "strtch",
                "F_imp", "F_strk", "F_sust", "max2/(1-R^2)");

    for (const auto& bc : bodies) {
        for (float stretch : stretches) {
            for (float decay : decays) {
                BodyBank bb;
                bb.prepare(kSR, /*voiceId*/ 0);
                bb.setBodyModel(bc.type);
                const auto p = makeParams(decay, stretch);

                float sumA = 1.0f;
                const float pImp  = measurePeakRaw(bb, p, impulseExc(), sumA);
                const float pStrk = measurePeakRaw(bb, p, strikeExc(),  sumA);
                // fundamental for the sustained probe
                const float f0 = bb.getSharedBank().getModeFrequency(0);
                const float pSust = measurePeakRaw(bb, p, sustainedExc(f0), sumA);
                const float gMax  = analyticMaxModeGain(bb.getSharedBank());

                std::printf("[diagnose_n1] %-9s %5.2f %6.2f | %7.2f %7.2f %7.2f | %8.1f\n",
                            bc.name, static_cast<double>(decay),
                            static_cast<double>(stretch),
                            static_cast<double>(pImp / sumA),
                            static_cast<double>(pStrk / sumA),
                            static_cast<double>(pSust / sumA),
                            static_cast<double>(gMax));
            }
        }
    }

    SUCCEED("diagnostic only");
}

// Second spike: hold decay fixed (F_strike is already shown decay-independent)
// and sweep material / size / strikePos -- the axes NOT covered above -- to
// decide whether the strike over-run factor F_strike is stable enough for a
// STATIC per-body constant, or whether it varies enough that a configure-time
// MEASURED-strike normalization is required. One axis varied at a time; the
// others held at the makeParams default.
TEST_CASE("N-1 spike: F_strike across material/size/strikePos", "[diagnose_n1_axes]")
{
    struct BodyCase { const char* name; BodyModelType type; };
    const BodyCase bodies[] = {
        {"Membrane",  BodyModelType::Membrane},
        {"Plate",     BodyModelType::Plate},
        {"Shell",     BodyModelType::Shell},
        {"Bell",      BodyModelType::Bell},
        {"NoiseBody", BodyModelType::NoiseBody},
    };
    constexpr float kFixedDecay   = 0.85f;
    constexpr float kFixedStretch = 1.0f;

    struct StrikeStat { float F; float sumA; float rawPeak; int modes; };
    auto fStrikeFull = [&](BodyModelType type, float material, float size,
                           float strikePos) -> StrikeStat {
        BodyBank bb;
        bb.prepare(kSR, 0);
        bb.setBodyModel(type);
        VoiceCommonParams p = makeParams(kFixedDecay, kFixedStretch);
        p.material  = material;
        p.size      = size;
        p.strikePos = strikePos;
        float sumA = 1.0f;
        const float pk = measurePeakRaw(bb, p, strikeExc(), sumA);
        return {pk / sumA, sumA, pk, bb.getSharedBank().getNumActiveModes()};
    };
    auto fStrike = [&](BodyModelType type, float material, float size,
                       float strikePos) -> float {
        return fStrikeFull(type, material, size, strikePos).F;
    };

    const float materials[] = {0.0f, 0.5f, 1.0f};
    const float sizes[]     = {0.20f, 0.50f, 0.90f};
    const float strikes[]   = {0.10f, 0.50f, 0.90f};

    std::printf("\n[diagnose_n1_axes] F_strike sweeps (decay=%.2f, others at default)\n",
                static_cast<double>(kFixedDecay));
    std::printf("[diagnose_n1_axes] %-9s | mat0.0 mat0.5 mat1.0 | sz0.2 sz0.5 sz0.9 | strk.1 strk.5 strk.9 | span\n",
                "body");

    for (const auto& bc : bodies) {
        // Reference point: IDENTICAL to the first test's makeParams default
        // (material 0.30, size 0.80, strikePos 0.30, decay 0.85). Print Sum|a_k|
        // and active mode count to cross-check against test 1 (Membrane ~18).
        {
            BodyBank bb;
            bb.prepare(kSR, 0);
            bb.setBodyModel(bc.type);
            VoiceCommonParams p = makeParams(kFixedDecay, kFixedStretch);
            float sumA = 1.0f;
            const float pk = measurePeakRaw(bb, p, strikeExc(), sumA);
            std::printf("[diagnose_n1_axes]   ref %-9s F=%.2f  sumA=%.4f  activeModes=%d\n",
                        bc.name, static_cast<double>(pk / sumA),
                        static_cast<double>(sumA),
                        bb.getSharedBank().getNumActiveModes());
        }
        // Detail on the material axis: confirm F changes are real resonant
        // buildup (rawPeak rises) vs a Sum|a_k| collapse / numerical blowup.
        for (float mat : materials) {
            const auto st = fStrikeFull(bc.type, mat, 0.80f, 0.30f);
            std::printf("[diagnose_n1_axes]   detail %-9s mat=%.1f  F=%.2f  "
                        "sumA=%.3f  rawPeak=%.2f  modes=%d\n",
                        bc.name, static_cast<double>(mat),
                        static_cast<double>(st.F), static_cast<double>(st.sumA),
                        static_cast<double>(st.rawPeak), st.modes);
        }

        const float m0 = fStrike(bc.type, materials[0], 0.80f, 0.30f);
        const float m1 = fStrike(bc.type, materials[1], 0.80f, 0.30f);
        const float m2 = fStrike(bc.type, materials[2], 0.80f, 0.30f);
        const float s0 = fStrike(bc.type, 0.30f, sizes[0], 0.30f);
        const float s1 = fStrike(bc.type, 0.30f, sizes[1], 0.30f);
        const float s2 = fStrike(bc.type, 0.30f, sizes[2], 0.30f);
        const float k0 = fStrike(bc.type, 0.30f, 0.80f, strikes[0]);
        const float k1 = fStrike(bc.type, 0.30f, 0.80f, strikes[1]);
        const float k2 = fStrike(bc.type, 0.30f, 0.80f, strikes[2]);

        float lo = m0, hi = m0;
        for (float v : {m0, m1, m2, s0, s1, s2, k0, k1, k2}) {
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
        const float span = (lo > 1e-6f) ? hi / lo : 0.0f;

        std::printf("[diagnose_n1_axes] %-9s | %6.2f %6.2f %6.2f | %5.2f %5.2f %5.2f | "
                    "%6.2f %6.2f %6.2f | %4.2fx\n",
                    bc.name,
                    static_cast<double>(m0), static_cast<double>(m1), static_cast<double>(m2),
                    static_cast<double>(s0), static_cast<double>(s1), static_cast<double>(s2),
                    static_cast<double>(k0), static_cast<double>(k1), static_cast<double>(k2),
                    static_cast<double>(span));
    }

    SUCCEED("diagnostic only");
}
