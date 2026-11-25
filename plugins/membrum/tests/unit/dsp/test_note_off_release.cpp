// ==============================================================================
// Note-off release in seconds (fix plan T1.10: MB-269, MB-107, MB-012, MB-034)
// ==============================================================================
// DrumVoice::noteOff() and fastReleaseDamp() used to shorten the release by
// ModalResonatorBank::damp(scale): one multiply of every mode's per-sample
// pole radius. A per-sample factor is a decay rate of -ln(scale) * fs, so the
// release halved each time the sample rate doubled (MB-012), and because the
// pool calls noteOff() again on every quiet block it compounded once per block
// (MB-034). The release now lives in the bank as an added decay rate in s^-1
// that every later retune keeps (MB-269) and that a second call never raises
// past its first value.
//
// "Release time" here: seconds from the note-off sample until a centred 5 ms
// running RMS of the output stays the given number of dB below the render's
// peak.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "dsp/body_model_type.h"
#include "dsp/drum_voice.h"
#include "dsp/exciter_type.h"
#include "dsp/pad_config.h"
#include "dsp/pad_config_apply.h"
#include "voice_pool/voice_pool.h"

#include <krate/dsp/core/db_utils.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace {

using Membrum::BodyModelType;
using Membrum::ExciterType;
using Membrum::PadConfig;

// Copied from specs/membrum-audit/probes/audit_probes.cpp.txt (detPad).
PadConfig detPad(BodyModelType b = BodyModelType::Membrane, ExciterType e = ExciterType::Impulse,
                 float size = 0.5f)
{
    PadConfig c;
    c.bodyModel = b;
    c.exciterType = e;
    c.size = size;
    c.noiseLayerMix = 0.0f;
    c.clickLayerMix = 0.0f;
    return c;
}

// Copied from specs/membrum-audit/probes/audit_probes.cpp.txt (hzToPitchNorm).
double hzToPitchNorm(double hz) { return std::log(hz / 20.0) / std::log(100.0); }

struct ReleaseMeasure
{
    bool   allFinite   = true;
    float  peak        = 0.0f;
    double releaseS    = -1.0;  // -1: the tail had not fallen below the bar by the render end
    double levelAtOffDb = -300.0;  // 5 ms RMS at the note-off, dB re the peak
};

ReleaseMeasure measureRelease(const std::vector<float>& x, long noteOffSample, double sampleRate,
                              double dbBelowPeak)
{
    ReleaseMeasure m;
    const long n = static_cast<long>(x.size());
    std::vector<double> cumSq(static_cast<std::size_t>(n) + 1, 0.0);
    for (long i = 0; i < n; ++i)
    {
        const float v = x[static_cast<std::size_t>(i)];
        if (!Krate::DSP::detail::isFinite(v))
            m.allFinite = false;
        m.peak = std::max(m.peak, std::fabs(v));
        cumSq[static_cast<std::size_t>(i) + 1] =
            cumSq[static_cast<std::size_t>(i)] + static_cast<double>(v) * static_cast<double>(v);
    }
    const double bar = static_cast<double>(m.peak) * std::pow(10.0, -dbBelowPeak / 20.0);
    const double barSq = bar * bar;
    const long window = std::max(1L, static_cast<long>(std::lround(0.005 * sampleRate)));
    const long half = window / 2;

    long lastAbove = noteOffSample - 1;
    for (long i = noteOffSample; i < n; ++i)
    {
        const long a = std::max(0L, i - half);
        const long b = std::min(n, i - half + window);
        const double meanSq = (cumSq[static_cast<std::size_t>(b)] - cumSq[static_cast<std::size_t>(a)])
                              / static_cast<double>(b - a);
        if (i == noteOffSample && m.peak > 0.0f)
            m.levelAtOffDb = 10.0 * std::log10(std::max(meanSq, 1.0e-30))
                             - 20.0 * std::log10(static_cast<double>(m.peak));
        if (meanSq >= barSq)
            lastAbove = i;
    }
    // The tail must have ended at least one window before the render's end.
    if (lastAbove < n - window)
        m.releaseS = static_cast<double>(lastAbove + 1 - noteOffSample) / sampleRate;
    return m;
}

double spreadRatio(const std::vector<double>& v)
{
    const auto [lo, hi] = std::minmax_element(v.begin(), v.end());
    return (*lo > 0.0) ? (*hi / *lo) : 1.0e9;
}

// DrumVoice alone, host blocks of `block` samples, the block split at the
// note-off so it lands on its exact sample.
std::vector<float> renderVoiceWithNoteOff(const PadConfig& cfg, float velocity, double sampleRate,
                                          long noteOffSample, long totalSamples, int block)
{
    auto v = std::make_unique<Membrum::DrumVoice>();
    v->prepare(sampleRate, 0u);
    Membrum::applyPadConfigToVoice(*v, cfg);
    v->noteOn(velocity);

    std::vector<float> out(static_cast<std::size_t>(totalSamples), 0.0f);
    long rendered = 0;
    bool offSent = false;
    while (rendered < totalSamples)
    {
        if (!offSent && rendered >= noteOffSample)
        {
            v->noteOff();
            offSent = true;
        }
        long limit = totalSamples - rendered;
        if (!offSent)
            limit = std::min(limit, noteOffSample - rendered);
        const int n = static_cast<int>(std::min<long>(block, limit));
        v->processBlock(out.data() + rendered, n);
        rendered += n;
    }
    return out;
}

struct PoolRender
{
    std::vector<float> left;
    long retiredAt = -1;  // first sample index (block end) at which no voice was active after the note-off
};

// VoicePool at `sampleRate`, pad 0 = `cfg`, note 36 velocity 1, the voice's
// release (DrumVoice::noteOff, as its auto-release fires it) at
// `noteOffSample` (the block split there), host blocks of `block` samples.
//
// The FIRST block is at least kOnsetBlock samples. The pool auto-releases a
// voice whose host-block peak is below 1e-3, and the amp envelope restarts
// from 0 with a 0.5 ms (24-sample) attack, so an 8-sample first block fires a
// note-off at the onset and the hit is released before it is heard (MB-052,
// fixed by T1.13). These cases measure the release after the MIDI note-off,
// so the onset block is kept long enough not to trip that; every later block,
// including all of the release, has the nominal size.
constexpr int kOnsetBlock = 64;

PoolRender renderPoolWithNoteOff(const PadConfig& cfg, double sampleRate, long noteOffSample,
                                 long totalSamples, int block)
{
    const int maxBlock = std::max(block, kOnsetBlock);
    auto pool = std::make_unique<Membrum::VoicePool>();
    pool->prepare(sampleRate, maxBlock);
    pool->padConfigMut(0) = cfg;
    pool->noteOn(36, 1.0f);

    PoolRender r;
    r.left.assign(static_cast<std::size_t>(totalSamples), 0.0f);
    std::vector<float> scratchR(static_cast<std::size_t>(maxBlock), 0.0f);
    std::vector<float> scratchL(static_cast<std::size_t>(maxBlock), 0.0f);
    long rendered = 0;
    bool offSent = false;
    while (rendered < totalSamples)
    {
        if (!offSent && rendered >= noteOffSample)
        {
            // The voice's own release (DrumVoice::noteOff), the one its
            // auto-release fires. A MIDI note-off no longer releases a drum
            // voice (FR-114, fix plan T2.9), so pool->noteOff(36) would leave
            // nothing to measure.
            pool->forEachMainVoice([](Membrum::DrumVoice& v) {
                if (v.isActive())
                    v.noteOff();
            });
            offSent = true;
        }
        long limit = totalSamples - rendered;
        if (!offSent)
            limit = std::min(limit, noteOffSample - rendered);
        const int nominal = (rendered == 0) ? maxBlock : block;
        const int n = static_cast<int>(std::min<long>(nominal, limit));
        std::fill(scratchL.begin(), scratchL.end(), 0.0f);
        std::fill(scratchR.begin(), scratchR.end(), 0.0f);
        pool->processBlock(scratchL.data(), scratchR.data(), n);
        std::copy(scratchL.begin(), scratchL.begin() + n,
                  r.left.begin() + static_cast<std::ptrdiff_t>(rendered));
        rendered += n;
        if (offSent && r.retiredAt < 0 && !pool->isAnyVoiceActive())
            r.retiredAt = rendered;
    }
    return r;
}

} // namespace

TEST_CASE("Note-off release time does not depend on the sample rate", "[drum_voice][release]")
{
    PadConfig cfg = detPad(BodyModelType::Membrane, ExciterType::Impulse);
    cfg.decay = 1.0f;
    cfg.material = 1.0f;

    const double rates[] = {44100.0, 96000.0, 192000.0};
    std::vector<double> times;
    for (const double sr : rates)
    {
        const long off = static_cast<long>(std::lround(0.1 * sr));
        const long total = off + static_cast<long>(std::lround(1.0 * sr));
        const auto out = renderVoiceWithNoteOff(cfg, 1.0f, sr, off, total, 64);
        const auto m = measureRelease(out, off, sr, 60.0);
        INFO("fs " << sr << " Hz: peak " << m.peak << ", release to -60 dB "
                   << m.releaseS * 1000.0 << " ms");
        REQUIRE(m.allFinite);
        REQUIRE(m.peak > 1.0e-3f);
        REQUIRE(m.releaseS > 0.0);
        times.push_back(m.releaseS);
    }
    INFO("release ms at 44.1 / 96 / 192 kHz: " << times[0] * 1000.0 << " / " << times[1] * 1000.0
                                               << " / " << times[2] * 1000.0);
    CHECK(spreadRatio(times) <= 1.10);
    // The 44.1 kHz calibration is kept: within 10 % of the MB-012 probe's
    // 38.9 ms (this measurement read 36.1 / 20.3 / 11.2 ms at 44.1 / 96 /
    // 192 kHz on the code before T1.10).
    CHECK(times[0] >= 0.9 * 0.0389);
    CHECK(times[0] <= 1.1 * 0.0389);
}

TEST_CASE("Repeated auto-release does not compound the note-off release", "[voice_pool][release]")
{
    constexpr double kSr = 48000.0;
    const PadConfig cfg = detPad(BodyModelType::Membrane, ExciterType::Impulse);
    const long off = static_cast<long>(std::lround(0.1 * kSr));
    const long total = off + static_cast<long>(std::lround(2.0 * kSr));

    std::vector<double> times;
    for (const int block : {32, 512})
    {
        const auto r = renderPoolWithNoteOff(cfg, kSr, off, total, block);
        const auto m = measureRelease(r.left, off, kSr, 80.0);
        INFO("block " << block << ": peak " << m.peak << ", release to -80 dB "
                      << m.releaseS * 1000.0 << " ms");
        REQUIRE(m.allFinite);
        REQUIRE(m.peak > 1.0e-3f);
        REQUIRE(m.releaseS > 0.0);
        times.push_back(m.releaseS);
    }
    INFO("release ms at block 32 / 512: " << times[0] * 1000.0 << " / " << times[1] * 1000.0);
    CHECK(spreadRatio(times) <= 1.10);
}

TEST_CASE("Note-off release survives the pitch envelope at every host block size",
          "[voice_pool][release][pitch_env]")
{
    constexpr double kSr = 48000.0;
    // decay = 1 and material = 1 (as in the sample-rate case above): with the
    // default decay 0.3 the voice has already fallen 60 dB below its peak, and
    // under the pool's quiet-block auto-release, before the 200 ms note-off,
    // so there is no release left to measure.
    PadConfig base = detPad(BodyModelType::Membrane, ExciterType::Impulse);
    base.decay = 1.0f;
    base.material = 1.0f;
    // The reference is the same pad with the pitch envelope held at the 80 Hz
    // end pitch (start = end), so both renders release at the pitch the swept
    // one ends at: the modal damping law depends on the frequency, and once the
    // sweep really sounds (T1.11) the plain pad's 158 Hz ring is no longer a
    // like-for-like reference (ruling 2026-11-24, Q2).
    PadConfig plain = base;
    plain.tsPitchEnvStart = static_cast<float>(hzToPitchNorm(80.0));
    plain.tsPitchEnvEnd = static_cast<float>(hzToPitchNorm(80.0));
    PadConfig swept = base;
    swept.tsPitchEnvStart = static_cast<float>(hzToPitchNorm(160.0));
    swept.tsPitchEnvEnd = static_cast<float>(hzToPitchNorm(80.0));
    // pad_config_apply.h: setPitchEnvTimeMs(tsPitchEnvTime * 500 ms).
    SECTION("sweep over before the note-off (30 ms)")
    {
        swept.tsPitchEnvTime = 30.0f / 500.0f;
    }
    SECTION("sweep still running at the note-off (400 ms): retunes after the note-off keep "
            "the release (MB-269)")
    {
        swept.tsPitchEnvTime = 400.0f / 500.0f;
    }
    plain.tsPitchEnvTime = swept.tsPitchEnvTime;

    const long off = static_cast<long>(std::lround(0.2 * kSr));
    const long total = off + static_cast<long>(std::lround(1.5 * kSr));

    std::vector<double> sweptTimes;
    std::vector<double> plainTimes;
    for (const int block : {8, 64, 1024})
    {
        const auto rSwept = renderPoolWithNoteOff(swept, kSr, off, total, block);
        const auto rPlain = renderPoolWithNoteOff(plain, kSr, off, total, block);
        const auto mSwept = measureRelease(rSwept.left, off, kSr, 60.0);
        const auto mPlain = measureRelease(rPlain.left, off, kSr, 60.0);
        const double retireS = rSwept.retiredAt < 0
                                   ? -1.0
                                   : static_cast<double>(rSwept.retiredAt - off) / kSr;
        INFO("block " << block << ": release to -60 dB with the pitch envelope "
                      << mSwept.releaseS * 1000.0 << " ms, held at 80 Hz " << mPlain.releaseS * 1000.0
                      << " ms; retired " << retireS << " s after the note-off; level at the note-off "
                      << mSwept.levelAtOffDb << " / " << mPlain.levelAtOffDb << " dB re peak");
        REQUIRE(mSwept.allFinite);
        REQUIRE(mPlain.allFinite);
        REQUIRE(mSwept.peak > 1.0e-3f);
        REQUIRE(mPlain.peak > 1.0e-3f);
        // Non-vacuity: the voice still sounds at the note-off.
        REQUIRE(mSwept.levelAtOffDb > -40.0);
        REQUIRE(mPlain.levelAtOffDb > -40.0);
        REQUIRE(mSwept.releaseS > 0.0);
        REQUIRE(mPlain.releaseS > 0.0);
        CHECK(spreadRatio({mSwept.releaseS, mPlain.releaseS}) <= 1.10);
        CHECK(rSwept.retiredAt >= 0);
        CHECK(retireS <= 1.0);
        sweptTimes.push_back(mSwept.releaseS);
        plainTimes.push_back(mPlain.releaseS);
    }
    INFO("release ms with the envelope at blocks 8 / 64 / 1024: " << sweptTimes[0] * 1000.0 << " / "
         << sweptTimes[1] * 1000.0 << " / " << sweptTimes[2] * 1000.0);
    CHECK(spreadRatio(sweptTimes) <= 1.10);
    CHECK(spreadRatio(plainTimes) <= 1.10);
}
