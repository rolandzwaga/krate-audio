// ==============================================================================
// Pitch envelope, tension and Material Morph on the block path (fix plan T1.11:
// MB-008, MB-026, MB-151, MB-159, MB-237)
// ==============================================================================
// Every retune ends in ModalResonatorBank::updateModes, which moves only the
// coefficient targets. The bank smooths toward them with a PER-SAMPLE 2 ms
// one-pole, but applies that step once per processBlock call, and DrumVoice
// called it once per host block: the sounding pitch reached ~1.1 % of the way
// per host block, so the 808 sweep never sounded and the render depended on the
// host buffer size. The note-on seed was an unsnapped updateModes, so the start
// pitch never sounded either.
//
// DrumVoice now retunes on a fixed 32-sample grid of voice time and snaps the
// bank after each retune, so these renders must agree at every host block size.
// "Live mode-0" is the frequency the bank's mode 0 is RUNNING at
// (getLiveModeFrequency), not its target.
//
// T1.12 (MB-028, MB-161), at the end of the file: Material Morph and live
// parameter edits keep the pitch-envelope and tension scaling.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "dsp/body_model_type.h"
#include "dsp/default_kit.h"
#include "dsp/drum_voice.h"
#include "dsp/exciter_type.h"
#include "dsp/pad_config.h"
#include "dsp/pad_config_apply.h"

#include <artifact_detection.h>
#include <krate/dsp/core/db_utils.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <memory>
#include <numbers>
#include <utility>
#include <vector>

namespace {

using Membrum::BodyModelType;
using Membrum::DrumVoice;
using Membrum::ExciterType;
using Membrum::PadConfig;

// Copied from specs/membrum-audit/probes/audit_probes.cpp.txt.
PadConfig detPad(BodyModelType b = BodyModelType::Membrane,
                 ExciterType e = ExciterType::Impulse, float size = 0.5f)
{
    PadConfig c;
    c.bodyModel     = b;
    c.exciterType   = e;
    c.size          = size;
    c.noiseLayerMix = 0.0f;
    c.clickLayerMix = 0.0f;
    return c;
}

// Copied from specs/membrum-audit/probes/audit_probes.cpp.txt.
double hzToPitchNorm(double hz) { return std::log(hz / 20.0) / std::log(100.0); }

double goertzelMagnitude(const float* x, std::size_t n, double sr, double hz)
{
    const double omega = 2.0 * std::numbers::pi * hz / sr;
    const double coeff = 2.0 * std::cos(omega);
    double s1 = 0.0;
    double s2 = 0.0;
    for (std::size_t i = 0; i < n; ++i)
    {
        const double s = static_cast<double>(x[i]) + coeff * s1 - s2;
        s2 = s1;
        s1 = s;
    }
    const double re = s1 - s2 * std::cos(omega);
    const double im = s2 * std::sin(omega);
    return std::sqrt(re * re + im * im);
}

// Goertzel peak over [loHz, hiHz] in 0.1 Hz steps, over [t0, t1) seconds.
double peakHz(const std::vector<float>& x, double sr, double t0, double t1,
              double loHz, double hiHz)
{
    const auto from = static_cast<std::size_t>(t0 * sr);
    const auto to   = std::min(x.size(), static_cast<std::size_t>(t1 * sr));
    double best = loHz;
    double bestMag = -1.0;
    const int steps = static_cast<int>(std::lround((hiHz - loHz) / 0.1));
    for (int i = 0; i <= steps; ++i)
    {
        const double f = loHz + 0.1 * static_cast<double>(i);
        const double m = goertzelMagnitude(x.data() + from, to - from, sr, f);
        if (m > bestMag)
        {
            bestMag = m;
            best = f;
        }
    }
    return best;
}

double maxAbsDiff(const std::vector<float>& a, const std::vector<float>& b)
{
    const std::size_t n = std::min(a.size(), b.size());
    double d = 0.0;
    for (std::size_t i = 0; i < n; ++i)
        d = std::max(d, static_cast<double>(std::fabs(a[i] - b[i])));
    return d;
}

bool allFinite(const std::vector<float>& x)
{
    return std::all_of(x.begin(), x.end(),
                       [](float s) { return Krate::DSP::detail::isFinite(s); });
}

double cents(double f, double ref) { return 1200.0 * std::log2(f / ref); }

float liveMode0(DrumVoice& v)
{
    return v.getBodyBankForTest().getSharedBank().getLiveModeFrequency(0);
}

// Modes the bank has culled at its Nyquist guard (target frequency 0).
int culledModes(DrumVoice& v)
{
    const auto& bank = v.getBodyBankForTest().getSharedBank();
    int n = 0;
    for (int k = 0; k < bank.getNumModes(); ++k)
        if (bank.getModeFrequency(k) <= 0.0f)
            ++n;
    return n;
}

struct Render
{
    std::vector<float> out;
    // (samples rendered so far, live mode-0 after that block)
    std::vector<std::pair<long, float>> mode0;
    float startMode0 = 0.0f;
    int culledAtStart = 0;
    int culledAtEnd = 0;
};

// blockSize 0 = DrumVoice::process() per sample.
Render renderVoice(const PadConfig& cfg, float vel, double sr, long total, int blockSize)
{
    Render r;
    auto v = std::make_unique<DrumVoice>();
    v->prepare(sr, 0u);
    Membrum::applyPadConfigToVoice(*v, cfg);
    v->noteOn(vel);
    r.startMode0 = liveMode0(*v);
    r.culledAtStart = culledModes(*v);
    r.out.assign(static_cast<std::size_t>(total), 0.0f);
    if (blockSize == 0)
    {
        for (long i = 0; i < total; ++i)
            r.out[static_cast<std::size_t>(i)] = v->process();
        r.culledAtEnd = culledModes(*v);
        return r;
    }
    long pos = 0;
    while (pos < total)
    {
        const int n = static_cast<int>(std::min<long>(blockSize, total - pos));
        v->processBlock(r.out.data() + pos, n);
        pos += n;
        r.mode0.emplace_back(pos, liveMode0(*v));
    }
    r.culledAtEnd = culledModes(*v);
    return r;
}

// The default-kit kick (pad 0, note 36: 160 -> 50 Hz over 20 ms) with the
// layers and the tension glide off, as MB-237's probe set it.
PadConfig defaultKick()
{
    std::array<PadConfig, Membrum::kNumPads> pads{};
    Membrum::DefaultKit::apply(pads);
    PadConfig c = pads[0];
    c.noiseLayerMix = 0.0f;
    c.clickLayerMix = 0.0f;
    c.tensionModAmt = 0.0f;
    return c;
}

} // namespace

TEST_CASE("Pitch envelope sweep sounds on the block path at every host block size (SC-009)",
          "[pitch_env][drum_voice][timing]")
{
    const PadConfig kick = defaultKick();
    constexpr float kVel = 100.0f / 127.0f;

    struct Rate
    {
        double sr;
        std::vector<int> blocks;
    };
    const std::array<Rate, 2> rates{{
        {44100.0, {1, 8, 32, 64, 128, 256, 512, 1024, 2048}},
        {96000.0, {8, 64, 2048}},
    }};

    for (const Rate& rate : rates)
    {
        const long total = static_cast<long>(rate.sr * 0.4);
        const Render ref8 = renderVoice(kick, kVel, rate.sr, total, 8);
        REQUIRE(allFinite(ref8.out));

        // (b) Start pitch: the bank runs at the envelope's start frequency the
        // moment noteOn returns.
        INFO("sr=" << rate.sr << " start live mode-0=" << ref8.startMode0 << " Hz");
        CHECK(ref8.startMode0 >= 0.85f * 160.0f);

        double perSampleHz = 0.0;
        if (rate.sr == 44100.0)
        {
            const Render ps = renderVoice(kick, kVel, rate.sr, total, 0);
            REQUIRE(allFinite(ps.out));
            perSampleHz = peakHz(ps.out, rate.sr, 0.150, 0.350, 30.0, 120.0);
            INFO("process() settled pitch " << perSampleHz << " Hz");
            CHECK(std::fabs(perSampleHz - 50.0) <= 0.03 * 50.0);

            // (c) SC-009 on the block path: live mode-0 at 20 ms.
            float at20 = 0.0f;
            const long s20 = static_cast<long>(0.020 * rate.sr);
            for (const auto& [pos, f] : ref8.mode0)
                if (pos <= s20)
                    at20 = f;
            INFO("block-8 live mode-0 at 20 ms = " << at20 << " Hz");
            CHECK(std::fabs(at20 - 50.0f) <= 0.10f * 50.0f);
        }

        for (int bs : rate.blocks)
        {
            const Render r = (bs == 8) ? ref8 : renderVoice(kick, kVel, rate.sr, total, bs);
            REQUIRE(allFinite(r.out));

            // (a) Settled pitch.
            const double hz = peakHz(r.out, rate.sr, 0.150, 0.350, 30.0, 120.0);
            INFO("sr=" << rate.sr << " block=" << bs << " settled pitch " << hz << " Hz");
            CHECK(std::fabs(hz - 50.0) <= 0.03 * 50.0);
            if (perSampleHz > 0.0)
                CHECK(std::fabs(hz - perSampleHz) <= 0.01 * perSampleHz);

            // (c) Trajectory against the block-8 render at the same sample index.
            if (bs == 64 || bs == 1024)
            {
                double worst = 0.0;
                long worstAt = 0;
                std::size_t j = 0;
                for (const auto& [pos, f] : r.mode0)
                {
                    while (j < ref8.mode0.size() && ref8.mode0[j].first < pos)
                        ++j;
                    REQUIRE(j < ref8.mode0.size());
                    REQUIRE(ref8.mode0[j].first == pos);
                    const double c = std::fabs(cents(f, ref8.mode0[j].second));
                    if (c > worst)
                    {
                        worst = c;
                        worstAt = pos;
                    }
                }
                INFO("block=" << bs << " worst live mode-0 deviation from block 8: "
                     << worst << " cents at sample " << worstAt);
                CHECK(worst <= 50.0);
            }

            // (d) Identity with the block-8 render.
            const double d = maxAbsDiff(r.out, ref8.out);
            INFO("block=" << bs << " max |diff| vs block 8 = " << d);
            CHECK(d <= 3.0e-5);
        }
    }
}

TEST_CASE("Tension glide is independent of the host block size",
          "[tension][drum_voice][timing]")
{
    PadConfig c = detPad(BodyModelType::Membrane, ExciterType::Impulse, 0.4f);
    c.tensionModAmt = 1.0f;
    constexpr double sr = 44100.0;
    const long total = static_cast<long>(sr * 0.2);

    const Render ref32 = renderVoice(c, 1.0f, sr, total, 32);
    REQUIRE(allFinite(ref32.out));
    std::vector<double> peaks;
    for (int bs : {32, 256, 1024, 2048, 0})
    {
        const Render r = (bs == 32) ? ref32 : renderVoice(c, 1.0f, sr, total, bs);
        REQUIRE(allFinite(r.out));
        const double hz = peakHz(r.out, sr, 0.050, 0.100, 150.0, 260.0);
        peaks.push_back(hz);
        INFO("block=" << bs << " (0 = process()) peak " << hz << " Hz");
        CHECK(hz > 150.0);
        if (bs != 0)
        {
            const double d = maxAbsDiff(r.out, ref32.out);
            INFO("block=" << bs << " max |diff| vs block 32 = " << d);
            CHECK(d <= 3.0e-5);
        }
    }
    const auto [lo, hi] = std::minmax_element(peaks.begin(), peaks.end());
    INFO("tension peak spread " << *lo << " .. " << *hi << " Hz");
    CHECK((*hi - *lo) <= 0.01 * *lo);
}

TEST_CASE("Material Morph refresh is independent of the host block size",
          "[morph][drum_voice][timing]")
{
    PadConfig c = detPad(BodyModelType::Plate, ExciterType::Mallet);
    c.morphEnabled  = 1.0f;
    c.morphStart    = 0.0f;
    c.morphEnd      = 1.0f;
    c.morphDuration = (200.0f - 10.0f) / 1990.0f;  // 200 ms
    constexpr double sr = 48000.0;
    const long total = static_cast<long>(sr * 0.4);

    const Render ref8 = renderVoice(c, 1.0f, sr, total, 8);
    REQUIRE(allFinite(ref8.out));
    for (int bs : {32, 512, 2048})
    {
        const Render r = renderVoice(c, 1.0f, sr, total, bs);
        REQUIRE(allFinite(r.out));
        const double d = maxAbsDiff(r.out, ref8.out);
        INFO("block=" << bs << " max |diff| vs block 8 = " << d);
        CHECK(d <= 3.0e-5);
    }
}

TEST_CASE("Rising pitch envelope across the Nyquist guard does not click",
          "[pitch_env][drum_voice]")
{
    // Mode Stretch at its maximum (norm 1.0) spreads the partials so the 400 ->
    // 2000 Hz sweep pushes ringing modes across the bank's Nyquist guard
    // (ruling 2026-11-24, Q1). The bare strike onset is itself one detection,
    // so the bar is relative: the sweep adds no detection over the same pad
    // with the pitch envelope off.
    PadConfig plain = detPad(BodyModelType::Membrane, ExciterType::Impulse, 0.0f);
    plain.modeStretch = 1.0f;
    PadConfig swept = plain;
    swept.tsPitchEnvStart = static_cast<float>(hzToPitchNorm(400.0));
    swept.tsPitchEnvEnd   = static_cast<float>(hzToPitchNorm(2000.0));
    swept.tsPitchEnvTime  = 50.0f / 500.0f;  // 50 ms
    constexpr double sr = 44100.0;
    const long total = static_cast<long>(sr * 0.3);

    // Configured as the audit probes' click counter (audit_probes.cpp.txt).
    auto countClicks = [](const std::vector<float>& x, const char* label) {
        Krate::DSP::TestUtils::ClickDetectorConfig cfg;
        cfg.sampleRate         = static_cast<float>(sr);
        cfg.frameSize          = 512;
        cfg.hopSize            = 256;
        cfg.detectionThreshold = 5.0f;
        Krate::DSP::TestUtils::ClickDetector det(cfg);
        det.prepare();
        const auto clicks = det.detect(x.data(), x.size());
        for (const auto& k : clicks)
            UNSCOPED_INFO(label << ": click at sample " << k.sampleIndex << " |dy| " << k.amplitude);
        return clicks.size();
    };

    const Render rSwept = renderVoice(swept, 1.0f, sr, total, 64);
    const Render rPlain = renderVoice(plain, 1.0f, sr, total, 64);
    REQUIRE(allFinite(rSwept.out));
    REQUIRE(allFinite(rPlain.out));

    // Non-vacuity: the sweep really culls modes that rang at the start.
    INFO("culled modes with the sweep: " << rSwept.culledAtStart << " at note-on -> "
         << rSwept.culledAtEnd << " after the sweep");
    REQUIRE(rSwept.culledAtEnd > rSwept.culledAtStart);

    const std::size_t sweptClicks = countClicks(rSwept.out, "swept");
    const std::size_t plainClicks = countClicks(rPlain.out, "plain");
    INFO("clicks detected: " << sweptClicks << " with the sweep, " << plainClicks << " without");
    CHECK(sweptClicks <= plainClicks);
}

// ==============================================================================
// Fix plan T1.12 (MB-028, MB-161): the morph refresh and the live parameter
// edits re-map the body's modes; the pitch-envelope / tension ratio must be
// applied on top of every such re-map, not lost to the last push.
// ==============================================================================
namespace {

// Live mode-0 once the render has reached `target` samples (blockSize 0 =
// process() per sample, so exactly `target`; otherwise the end of the first
// whole block that reaches it).
float mode0AtSample(const PadConfig& cfg, float vel, double sr, long target, int blockSize)
{
    auto v = std::make_unique<DrumVoice>();
    v->prepare(sr, 0u);
    Membrum::applyPadConfigToVoice(*v, cfg);
    v->noteOn(vel);
    std::vector<float> buf(static_cast<std::size_t>(std::max(blockSize, 1)), 0.0f);
    long pos = 0;
    while (pos < target)
    {
        if (blockSize == 0)
        {
            (void)v->process();
            ++pos;
        }
        else
        {
            v->processBlock(buf.data(), blockSize);
            pos += blockSize;
        }
    }
    return liveMode0(*v);
}

// Render `total` samples in blocks of `blockSize`; at `editAt` (a block
// boundary, -1 = never) call setDecay with the value the pad already has.
// mode0 records (samples rendered, live mode-0) after every block.
Render renderWithSameValueEdit(const PadConfig& cfg, double sr, long total,
                               int blockSize, long editAt)
{
    Render r;
    auto v = std::make_unique<DrumVoice>();
    v->prepare(sr, 0u);
    Membrum::applyPadConfigToVoice(*v, cfg);
    v->noteOn(1.0f);
    r.out.assign(static_cast<std::size_t>(total), 0.0f);
    long pos = 0;
    while (pos < total)
    {
        if (pos == editAt)
            v->setDecay(cfg.decay);
        const int n = static_cast<int>(std::min<long>(blockSize, total - pos));
        v->processBlock(r.out.data() + pos, n);
        pos += n;
        r.mode0.emplace_back(pos, liveMode0(*v));
    }
    return r;
}

float morphDurationNorm(float ms) { return (ms - 10.0f) / 1990.0f; }

} // namespace

TEST_CASE("Material Morph keeps the pitch envelope and tension scaling",
          "[morph][pitch_env][drum_voice]")
{
    constexpr double sr = 48000.0;

    SECTION("sweep under morph")
    {
        // MB-028: 400 -> 100 Hz over 100 ms on a pad whose material morphs.
        PadConfig swept = detPad(BodyModelType::Membrane, ExciterType::Impulse);
        swept.tsPitchEnvStart = static_cast<float>(hzToPitchNorm(400.0));
        swept.tsPitchEnvEnd   = static_cast<float>(hzToPitchNorm(100.0));
        swept.tsPitchEnvTime  = 100.0f / 500.0f;  // 100 ms
        swept.morphEnabled    = 1.0f;
        swept.morphStart      = 0.2f;
        swept.morphEnd        = 0.8f;
        swept.morphDuration   = morphDurationNorm(300.0f);
        PadConfig plain = swept;
        plain.tsPitchEnvTime = 0.0f;

        const long at100 = static_cast<long>(0.100 * sr);
        for (int bs : {0, 256})
        {
            const float f      = mode0AtSample(swept, 1.0f, sr, at100, bs);
            const float fPlain = mode0AtSample(plain, 1.0f, sr, at100, bs);
            INFO("block=" << bs << " (0 = process()) live mode-0 at 100 ms: " << f
                 << " Hz with the sweep, " << fPlain << " Hz without");
            CHECK(std::fabs(f - 100.0f) <= 0.10f * 100.0f);
            CHECK(std::fabs(cents(f, fPlain)) > 100.0);
        }
    }

    SECTION("neutral morph changes nothing")
    {
        // MB-161: a morph that does not move the material must not move the
        // sound, with the pitch envelope and the tension glide both running.
        PadConfig off = detPad(BodyModelType::Membrane, ExciterType::Impulse);
        off.material        = 0.5f;
        off.tsPitchEnvStart = static_cast<float>(hzToPitchNorm(160.0));
        off.tsPitchEnvEnd   = static_cast<float>(hzToPitchNorm(50.0));
        off.tsPitchEnvTime  = 50.0f / 500.0f;  // 50 ms
        off.tensionModAmt   = 1.0f;
        PadConfig on = off;
        on.morphEnabled  = 1.0f;
        on.morphStart    = 0.5f;
        on.morphEnd      = 0.5f;
        on.morphDuration = morphDurationNorm(300.0f);

        const long total = static_cast<long>(sr * 0.4);
        for (int bs : {1, 64, 1024})
        {
            const Render rOff = renderVoice(off, 1.0f, sr, total, bs);
            const Render rOn  = renderVoice(on, 1.0f, sr, total, bs);
            REQUIRE(allFinite(rOff.out));
            REQUIRE(allFinite(rOn.out));
            const double d = maxAbsDiff(rOn.out, rOff.out);
            INFO("block=" << bs << " max |diff| neutral morph vs morph off = " << d);
            CHECK(d <= 3.0e-5);
        }
    }

    SECTION("live edit keeps the pitch")
    {
        // A same-value live edit after the sweep (dirty gate idle) re-maps the
        // modes; the pitch ratio must be re-applied to that push.
        PadConfig c = detPad(BodyModelType::Membrane, ExciterType::Impulse);
        c.tsPitchEnvStart = static_cast<float>(hzToPitchNorm(160.0));
        c.tsPitchEnvEnd   = static_cast<float>(hzToPitchNorm(50.0));
        c.tsPitchEnvTime  = 50.0f / 500.0f;  // 50 ms
        c.tensionModAmt   = 0.0f;

        constexpr int kBlock = 64;
        const long editAt = static_cast<long>(0.200 * sr);  // 9600 = 150 blocks
        REQUIRE(editAt % kBlock == 0);
        const long total = editAt + static_cast<long>(0.100 * sr);
        const Render r = renderWithSameValueEdit(c, sr, total, kBlock, editAt);
        REQUIRE(allFinite(r.out));

        float before = 0.0f;
        double worst = 0.0;
        for (const auto& [pos, f] : r.mode0)
        {
            if (pos == editAt)
                before = f;
            else if (pos > editAt)
                worst = std::max(worst, std::fabs(cents(f, before)));
        }
        INFO("live mode-0 before the edit " << before << " Hz; worst deviation over the next 100 ms "
             << worst << " cents");
        REQUIRE(before > 0.0f);
        CHECK(worst <= 1.0);
    }

    SECTION("live edit under morph keeps the sound")
    {
        // The live re-map must use the material the morph is at, and keep the
        // pitch ratio: a same-value edit after the morph and the sweep have
        // ended must not change one sample.
        PadConfig c = detPad(BodyModelType::Membrane, ExciterType::Impulse);
        c.material        = 0.5f;
        c.tsPitchEnvStart = static_cast<float>(hzToPitchNorm(160.0));
        c.tsPitchEnvEnd   = static_cast<float>(hzToPitchNorm(50.0));
        c.tsPitchEnvTime  = 50.0f / 500.0f;  // 50 ms
        c.morphEnabled    = 1.0f;
        c.morphStart      = 0.2f;
        c.morphEnd        = 0.8f;
        c.morphDuration   = morphDurationNorm(50.0f);

        constexpr int kBlock = 64;
        const long editAt = static_cast<long>(0.200 * sr);
        const long total = editAt + static_cast<long>(0.200 * sr);
        const Render ref  = renderWithSameValueEdit(c, sr, total, kBlock, -1);
        const Render edit = renderWithSameValueEdit(c, sr, total, kBlock, editAt);
        REQUIRE(allFinite(ref.out));
        REQUIRE(allFinite(edit.out));
        const double d = maxAbsDiff(edit.out, ref.out);
        INFO("max |diff| with a same-value setDecay at 200 ms = " << d);
        CHECK(d <= 3.0e-5);
    }
}
