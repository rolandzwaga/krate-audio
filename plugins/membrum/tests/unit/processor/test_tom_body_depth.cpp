// ==============================================================================
// test_tom_body_depth.cpp -- Tom-depth fix acceptance tests
// ==============================================================================
// Guards the "thin/toy toms" fix (docs/tom-depth-implementation-plan.md):
//   Fix A -- downward "tonk" pitch glide onto the natural f0 + articulate onset
//   Fix B -- frequency-dependent damping collapses the 100-500 Hz mid cluster
//   Fix D -- per-pad level grades DOWN with size (bigger tom = louder/weightier)
//   Fix A.4 -- Membrane + Mallet stays classified as Tom (not Kick) with a
//              pitch env active
//
// Two layers of assertion:
//   1. Config-level (fast, no render) -- the preset data is what the fix sets.
//   2. Render-level (MIDI 41 floor tom through the full Processor) -- the sound
//      actually has the low body / downward glide / mid-cluster collapse.
//
// Path note: toms use the Mallet exciter, so they always take processBlockFast
// (useSlowPath == feedbackExciter). The fast path refreshes pitch at BLOCK rate,
// so a large-block vs 1-sample render legitimately diverges during the glide --
// we therefore assert determinism at a FIXED block size and robustness at the
// ENVELOPE level across block sizes, never large-vs-1-sample bit-exactness.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/default_kit.h"
#include "dsp/pad_config.h"
#include "dsp/pad_category.h"
#include "dsp/exciter_type.h"
#include "dsp/body_model_type.h"
#include "processor/processor.h"
#include "audio_features.h"

#include <krate/dsp/primitives/fft.h>

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstevents.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

using namespace Membrum;
using Catch::Approx;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kSampleRate = 48000.0;

// --------------------------------------------------------------------------
// Full-Processor render harness (mirrors test_render_perceptual.cpp).
// --------------------------------------------------------------------------
class NoteEventList : public Steinberg::Vst::IEventList {
public:
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void**) override {
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }
    Steinberg::int32 PLUGIN_API getEventCount() override {
        return static_cast<Steinberg::int32>(events_.size());
    }
    Steinberg::tresult PLUGIN_API getEvent(Steinberg::int32 i, Steinberg::Vst::Event& e) override {
        if (i < 0 || i >= static_cast<Steinberg::int32>(events_.size()))
            return Steinberg::kResultFalse;
        e = events_[static_cast<std::size_t>(i)];
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API addEvent(Steinberg::Vst::Event& e) override {
        events_.push_back(e);
        return Steinberg::kResultOk;
    }
    void noteOn(Steinberg::int16 midi) {
        Steinberg::Vst::Event e{};
        e.type = Steinberg::Vst::Event::kNoteOnEvent;
        e.noteOn.pitch = midi;
        e.noteOn.velocity = 1.0f;
        events_.push_back(e);
    }
private:
    std::vector<Steinberg::Vst::Event> events_;
};

// Render a single note through a fresh Processor at a fixed block size.
std::vector<float> renderNoteMono(Steinberg::int16 midi, double seconds, int block) {
    using namespace Steinberg;
    using namespace Steinberg::Vst;

    Membrum::Processor proc;
    proc.initialize(nullptr);
    ProcessSetup setup{};
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = block;
    setup.sampleRate = kSampleRate;
    proc.setupProcessing(setup);
    proc.setActive(true);

    std::vector<float> outL(static_cast<std::size_t>(block));
    std::vector<float> outR(static_cast<std::size_t>(block));
    float* chans[2] = {outL.data(), outR.data()};
    AudioBusBuffers outBus{};
    outBus.numChannels = 2;
    outBus.channelBuffers32 = chans;

    NoteEventList events;
    events.noteOn(midi);

    ProcessData data{};
    data.processMode = kRealtime;
    data.symbolicSampleSize = kSample32;
    data.numOutputs = 1;
    data.outputs = &outBus;
    data.numSamples = block;

    const auto totalSamples = static_cast<std::size_t>(seconds * kSampleRate);
    std::vector<float> mono;
    mono.reserve(totalSamples + static_cast<std::size_t>(block));
    std::size_t done = 0;
    bool first = true;
    while (done < totalSamples) {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        data.inputEvents = first ? &events : nullptr;
        proc.process(data);
        for (int i = 0; i < block; ++i)
            mono.push_back(0.5f * (outL[static_cast<std::size_t>(i)] + outR[static_cast<std::size_t>(i)]));
        done += static_cast<std::size_t>(block);
        first = false;
    }
    proc.setActive(false);
    proc.terminate();
    return mono;
}

// --------------------------------------------------------------------------
// Analysis helpers.
// --------------------------------------------------------------------------

// Dominant frequency in [lo,hi] Hz over a window [start, start+win) via a
// parabolic-interpolated FFT peak. Used to read the glide direction.
double dominantHz(const std::vector<float>& mono, std::size_t start, std::size_t win,
                  double sr, double lo, double hi) {
    if (start >= mono.size())
        return 0.0;
    win = std::min(win, mono.size() - start);
    std::size_t nfft = 1;
    while (nfft < win)
        nfft <<= 1;
    nfft = std::min<std::size_t>(nfft, 16384);

    Krate::DSP::FFT fft;
    fft.prepare(nfft);
    std::vector<float> frame(nfft, 0.0f);
    for (std::size_t i = 0; i < win && i < nfft; ++i) {
        const double w = 0.5 * (1.0 - std::cos(2.0 * kPi * static_cast<double>(i) /
                                               static_cast<double>(win - 1)));
        frame[i] = mono[start + i] * static_cast<float>(w);
    }
    std::vector<Krate::DSP::Complex> spec(nfft / 2 + 1);
    fft.forward(frame.data(), spec.data());

    const double binHz = sr / static_cast<double>(nfft);
    auto kLo = std::max<std::size_t>(1, static_cast<std::size_t>(std::floor(lo / binHz)));
    auto kHi = std::min(spec.size() - 1, static_cast<std::size_t>(std::ceil(hi / binHz)));
    std::size_t kPeak = kLo;
    double mPeak = -1.0;
    for (std::size_t k = kLo; k <= kHi; ++k) {
        const double m = spec[k].magnitude();
        if (m > mPeak) { mPeak = m; kPeak = k; }
    }
    double f = static_cast<double>(kPeak) * binHz;
    if (kPeak > kLo && kPeak < kHi) {
        const double a = spec[kPeak - 1].magnitude();
        const double b = spec[kPeak].magnitude();
        const double c = spec[kPeak + 1].magnitude();
        const double denom = a - 2.0 * b + c;
        if (std::fabs(denom) > 1e-12) {
            const double d = 0.5 * (a - c) / denom;
            f = (static_cast<double>(kPeak) + d) * binHz;
        }
    }
    return f;
}

// T60 (s) of the partial at f0, estimated by log-linear regression of the
// per-frame Goertzel magnitude (dB) from the envelope peak down to -45 dB.
double fundamentalT60(const std::vector<float>& mono, double f0, double sr) {
    const std::size_t win = 4096;
    const std::size_t hop = 2048;
    if (mono.size() < win)
        return 0.0;
    const double coeff = 2.0 * std::cos(2.0 * kPi * f0 / sr);

    std::vector<double> t;
    std::vector<double> db;
    for (std::size_t start = 0; start + win <= mono.size(); start += hop) {
        double s1 = 0.0;
        double s2 = 0.0;
        for (std::size_t i = 0; i < win; ++i) {
            const double s0 = static_cast<double>(mono[start + i]) + coeff * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        const double power = s1 * s1 + s2 * s2 - coeff * s1 * s2;
        const double mag = std::sqrt(std::max(0.0, power));
        t.push_back(static_cast<double>(start + win / 2) / sr);
        db.push_back(20.0 * std::log10(std::max(mag, 1e-12)));
    }
    if (db.size() < 3)
        return 0.0;

    std::size_t pk = 0;
    double pkv = -1e9;
    for (std::size_t i = 0; i < db.size(); ++i)
        if (db[i] > pkv) { pkv = db[i]; pk = i; }

    std::vector<double> xs;
    std::vector<double> ys;
    for (std::size_t i = pk; i < db.size(); ++i) {
        if (db[i] < pkv - 45.0)
            break;
        xs.push_back(t[i]);
        ys.push_back(db[i]);
    }
    if (xs.size() < 3)
        return 0.0;

    const double n = static_cast<double>(xs.size());
    double sx = 0.0;
    double sy = 0.0;
    double sxx = 0.0;
    double sxy = 0.0;
    for (std::size_t i = 0; i < xs.size(); ++i) {
        sx += xs[i];
        sy += ys[i];
        sxx += xs[i] * xs[i];
        sxy += xs[i] * ys[i];
    }
    const double denom = n * sxx - sx * sx;
    if (std::fabs(denom) < 1e-12)
        return 0.0;
    const double slope = (n * sxy - sx * sy) / denom;  // dB per second
    if (slope >= 0.0)
        return 1e9;  // not decaying within the render -> effectively infinite
    return -60.0 / slope;
}

// Per-frame RMS (dBFS) in `frameLen`-sample frames.
std::vector<double> frameRmsDb(const std::vector<float>& mono, std::size_t frameLen) {
    std::vector<double> out;
    for (std::size_t start = 0; start + frameLen <= mono.size(); start += frameLen) {
        double sumSq = 0.0;
        for (std::size_t i = 0; i < frameLen; ++i)
            sumSq += static_cast<double>(mono[start + i]) * mono[start + i];
        const double rms = std::sqrt(sumSq / static_cast<double>(frameLen));
        out.push_back(Krate::Test::linToDbfs(rms));
    }
    return out;
}

// Natural fundamental for a Membrane pad of the given normalized size.
double naturalF0(double size) {
    return 500.0 * std::pow(0.1, size);
}

}  // namespace

// ==============================================================================
// Config-level asserts (fast, no render).
// ==============================================================================

TEST_CASE("Tom pitch env glides DOWN onto natural f0", "[tom_depth]") {
    std::array<PadConfig, kNumPads> pads{};
    DefaultKit::apply(pads);

    const int   pad[6]  = {5, 7, 9, 11, 12, 14};
    const float size[6] = {0.8f, 0.7f, 0.6f, 0.5f, 0.45f, 0.4f};
    auto fromNorm = [](float norm) { return 20.0f * std::pow(100.0f, norm); };

    for (int i = 0; i < 6; ++i) {
        const PadConfig& c = pads[static_cast<std::size_t>(pad[i])];
        INFO("tom pad index: " << pad[i]);
        REQUIRE(c.tsPitchEnvTime > 0.0f);                       // env enabled
        const float endHz   = fromNorm(c.tsPitchEnvEnd);
        const float startHz = fromNorm(c.tsPitchEnvStart);
        const float f0 = static_cast<float>(naturalF0(size[i]));
        REQUIRE(endHz   == Approx(f0).epsilon(0.05));           // END == natural f0
        REQUIRE(startHz > endHz);                               // DOWNWARD glide
        REQUIRE(startHz < f0 * 1.15f);                          // subtle, not a boing
        // PadConfig stores the NORMALIZED b3 (denorm b3 = norm*1e-3 in mapper).
        REQUIRE(c.bodyDampingB3 == Approx(0.15f));              // Fix B (norm)
        REQUIRE(c.tensionModAmt == Approx(0.25f));              // Fix A.2
        // bodyDampingB1 must stay sentinel so the fundamental keeps its long ring.
        REQUIRE(c.bodyDampingB1 < 0.0f);
    }
}

TEST_CASE("Tom level grades down with size (size->weight)", "[tom_depth]") {
    std::array<PadConfig, kNumPads> pads{};
    DefaultKit::apply(pads);
    REQUIRE(pads[5].level  > pads[14].level);   // floor louder than high
    REQUIRE(pads[5].level  <= 0.95f);           // softClip headroom
    REQUIRE(pads[7].level  > pads[12].level);   // monotonic across the row
    REQUIRE(pads[9].level  > pads[14].level);
}

TEST_CASE("Tom pads classify as Tom, not Kick", "[tom_depth]") {
    // Rule 0: Membrane + Mallet stays a Tom even with an active pitch env.
    PadConfig c{};
    c.bodyModel      = BodyModelType::Membrane;
    c.exciterType    = ExciterType::Mallet;
    c.tsPitchEnvTime = 0.3f;  // pitch env active
    REQUIRE(classifyPad(c) == PadCategory::Tom);

    // And the six default-kit toms classify as Tom (they now carry a pitch env).
    std::array<PadConfig, kNumPads> pads{};
    DefaultKit::apply(pads);
    for (int p : {5, 7, 9, 11, 12, 14}) {
        INFO("tom pad index: " << p);
        REQUIRE(classifyPad(pads[static_cast<std::size_t>(p)]) == PadCategory::Tom);
    }
}

TEST_CASE("Tom takes the fast path (slow path is Feedback-only)", "[tom_depth]") {
    // useSlowPath == feedbackExciter; a tom uses the Mallet exciter, so it
    // always takes processBlockFast (the slow path is never exercised).
    std::array<PadConfig, kNumPads> pads{};
    DefaultKit::apply(pads);
    REQUIRE(pads[5].exciterType == ExciterType::Mallet);
}

// ==============================================================================
// Render-level asserts (MIDI 41 floor tom, natural f0 ~79 Hz).
// ==============================================================================

TEST_CASE("Tom render: low body dominant, mid cluster collapsed", "[tom_depth][render]") {
    const auto mono = renderNoteMono(41, 2.0, 512);
    const auto f = Krate::Test::extractAudioFeatures(mono, kSampleRate);
    INFO("floor-tom features: " << Krate::Test::formatFeatures(f));

    REQUIRE(f.peakDbfs > -40.0);      // audible
    // Fix B: the inharmonic 100-500 Hz mid cluster collapses (was ~0.317).
    REQUIRE(f.band[1] < 0.20);
    // The 79 Hz fundamental lives in the 20-100 Hz band -> low weight dominant.
    REQUIRE(f.band[0] > 0.60);
    // Low, body-forward spectral centroid (was ~160 Hz pre-fix).
    REQUIRE(f.centroidHz < 160.0);
}

TEST_CASE("Tom render: fundamental sits at the natural pitch (not up-tuned)",
          "[tom_depth][render]") {
    const auto mono = renderNoteMono(41, 2.0, 512);
    // Sustained portion (0.5-1.0 s): fundamental should be ~79 Hz, not up-tuned.
    const auto tailHz = dominantHz(mono, static_cast<std::size_t>(0.5 * kSampleRate),
                                   static_cast<std::size_t>(0.5 * kSampleRate),
                                   kSampleRate, 40.0, 250.0);
    INFO("sustained fundamental Hz: " << tailHz);
    const double f0 = naturalF0(0.8);  // ~79 Hz
    REQUIRE(tailHz == Approx(f0).epsilon(0.15));
}

TEST_CASE("Tom render: pitch glides DOWNWARD", "[tom_depth][render]") {
    const auto mono = renderNoteMono(41, 2.0, 64);  // small block: fine glide
    // Measure the fundamental AFTER the attack transient has settled (the click
    // + still-building modes corrupt low-band peak detection in the first ~70 ms)
    // but while the glide is still in flight, vs the fully-settled tail. Windows
    // long enough (>=120 ms) that the ~80 Hz fundamental is the clean dominant
    // peak in [40, 250] Hz.
    const auto earlyHz = dominantHz(mono, static_cast<std::size_t>(0.08 * kSampleRate),
                                    static_cast<std::size_t>(0.12 * kSampleRate),
                                    kSampleRate, 40.0, 250.0);
    const auto lateHz  = dominantHz(mono, static_cast<std::size_t>(0.60 * kSampleRate),
                                    static_cast<std::size_t>(0.50 * kSampleRate),
                                    kSampleRate, 40.0, 250.0);
    INFO("early Hz: " << earlyHz << "  late Hz: " << lateHz);
    // Downward glide: the fundamental is still higher mid-glide than once settled.
    REQUIRE(earlyHz > lateHz + 1.0);
}

TEST_CASE("Tom render: fundamental rings long (T60 >= 1.4 s)", "[tom_depth][render]") {
    const auto mono = renderNoteMono(41, 2.5, 512);
    const double f0 = naturalF0(0.8);  // ~79 Hz
    const double t60 = fundamentalT60(mono, f0, kSampleRate);
    INFO("fundamental T60 (s): " << t60);
    REQUIRE(t60 >= 1.4);
}

TEST_CASE("Tom render: articulate onset (2-8 kHz energy in first 20 ms)",
          "[tom_depth][render]") {
    const auto mono = renderNoteMono(41, 0.5, 512);
    const std::size_t win = static_cast<std::size_t>(0.02 * kSampleRate);
    const std::vector<float> onset(mono.begin(),
                                   mono.begin() + static_cast<std::ptrdiff_t>(std::min(win, mono.size())));
    const auto f = Krate::Test::extractAudioFeatures(onset, kSampleRate);
    INFO("onset features: " << Krate::Test::formatFeatures(f));
    REQUIRE(f.band[3] > 0.0);  // measurable 2-8 kHz click energy at the strike
}

// ==============================================================================
// Path / block-size robustness (replaces the invalid Fast-vs-Slow equivalence).
// ==============================================================================

TEST_CASE("Tom render is deterministic at a fixed block size", "[tom_depth][render]") {
    // Same config, same block size, two fresh renders -> bit-identical (the
    // noise/click PRNGs are seeded by voiceId, which is identical for both).
    const auto a = renderNoteMono(41, 0.5, 64);
    const auto b = renderNoteMono(41, 0.5, 64);
    REQUIRE(a.size() == b.size());
    for (std::size_t n = 0; n < a.size(); ++n)
        REQUIRE(a[n] == b[n]);
}

TEST_CASE("Tom render is block-size robust at the envelope level", "[tom_depth][render]") {
    // The fast path refreshes pitch at BLOCK rate, so block 64 vs 512 is NOT
    // sample-exact during the glide -- compare at the envelope/spectral level.
    const auto a = renderNoteMono(41, 1.5, 64);
    const auto b = renderNoteMono(41, 1.5, 512);

    const std::size_t frameLen = static_cast<std::size_t>(0.02 * kSampleRate);  // 20 ms
    const auto rmsA = frameRmsDb(a, frameLen);
    const auto rmsB = frameRmsDb(b, frameLen);
    const std::size_t frames = std::min(rmsA.size(), rmsB.size());
    REQUIRE(frames > 10);
    std::size_t compared = 0;
    for (std::size_t fidx = 2; fidx < frames; ++fidx) {  // skip the attack transient
        // Only compare while the signal is well above the decay floor: deep in
        // the tail (< -45 dBFS) the block-rate pitch refresh legitimately
        // diverges the two renders' phase/amplitude, which is not a regression.
        if (rmsA[fidx] < -45.0 || rmsB[fidx] < -45.0)
            continue;
        INFO("frame " << fidx << " rms64=" << rmsA[fidx] << " rms512=" << rmsB[fidx]);
        REQUIRE(rmsA[fidx] == Approx(rmsB[fidx]).margin(1.0));
        ++compared;
    }
    REQUIRE(compared > 10);  // ensure the gate didn't skip everything

    // Sustained-tail fundamental agrees within a couple Hz.
    const auto tailA = dominantHz(a, static_cast<std::size_t>(0.7 * kSampleRate),
                                  static_cast<std::size_t>(0.5 * kSampleRate), kSampleRate, 40.0, 250.0);
    const auto tailB = dominantHz(b, static_cast<std::size_t>(0.7 * kSampleRate),
                                  static_cast<std::size_t>(0.5 * kSampleRate), kSampleRate, 40.0, 250.0);
    REQUIRE(tailA == Approx(tailB).margin(2.0));
}
