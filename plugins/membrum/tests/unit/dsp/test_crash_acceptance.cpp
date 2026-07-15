// ==============================================================================
// Crash-cymbal acceptance tests (CRASH-REDESIGN-PLAN.md Phase 0)
// ==============================================================================
// Renders the DEFAULT-KIT crash (pad 13, MIDI 49) through the full Membrum
// Processor and asserts the perceptual signatures a real crash MUST have:
//   AC-1  long wash        -- audible energy still present ~1.5 s after strike
//   AC-2  bloom            -- HF energy fraction RISES after onset, peaking in
//                            [20 ms, 400 ms] (delayed-HF energy cascade)
//   AC-3  freq-dep decay   -- low band rings far longer than high band, and
//                            band decay is monotone dark-through-tail
//   AC-4  no pitch         -- no salient pitch in the sustained tail (no drone)
//   AC-5  velocity regime  -- a soft hit is darker / blooms less than a hard hit
//
// These encode the research summary in CRASH-REDESIGN-PLAN.md §2. The metrics
// are computed here (not in audio_features.h) because they are TIME-WINDOWED
// (spectrogram-style), which the whole-render AudioFeatures summary can't give.
//
// Rendering path note: the Processor renders the compiled-in DEFAULT kit, whose
// crash is DrumTemplate::Cymbal in default_kit.h. Phase 1 tunes that template to
// the crash target, so these tests move from failing -> passing across the
// redesign phases (see the plan). Thresholds are deliberately generous: they
// separate "gong/chime" from "crash", not fine tuning.
// ==============================================================================
#include <catch2/catch_test_macros.hpp>

#include "processor/processor.h"

#include <krate/dsp/primitives/fft.h>

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstevents.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int    kBlock      = 512;
constexpr int    kCrashNote  = 49;  // pad 13, Crash Cymbal 1

class NoteEventList : public IEventList {
public:
    tresult PLUGIN_API queryInterface(const TUID, void**) override { return kNoInterface; }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
    int32 PLUGIN_API getEventCount() override { return static_cast<int32>(events_.size()); }
    tresult PLUGIN_API getEvent(int32 i, Event& e) override {
        if (i < 0 || i >= static_cast<int32>(events_.size())) return kResultFalse;
        e = events_[static_cast<size_t>(i)];
        return kResultOk;
    }
    tresult PLUGIN_API addEvent(Event& e) override { events_.push_back(e); return kResultOk; }
    void noteOn(int16 midi, float vel) {
        Event e{};
        e.type = Event::kNoteOnEvent;
        e.noteOn.pitch = midi;
        e.noteOn.velocity = vel;
        events_.push_back(e);
    }
private:
    std::vector<Event> events_;
};

// Render a single note through a fresh default-kit Processor into a mono buffer.
std::vector<float> renderMono(int16 midi, float velocity, double seconds) {
    Membrum::Processor proc;
    proc.initialize(nullptr);
    ProcessSetup setup{};
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = kBlock;
    setup.sampleRate = kSampleRate;
    proc.setupProcessing(setup);
    proc.setActive(true);

    std::vector<float> outL(kBlock), outR(kBlock);
    float* chans[2] = {outL.data(), outR.data()};
    AudioBusBuffers outBus{};
    outBus.numChannels = 2;
    outBus.channelBuffers32 = chans;

    NoteEventList events;
    events.noteOn(midi, velocity);

    ProcessData data{};
    data.processMode = kRealtime;
    data.symbolicSampleSize = kSample32;
    data.numOutputs = 1;
    data.outputs = &outBus;
    data.numSamples = kBlock;

    const auto total = static_cast<size_t>(seconds * kSampleRate);
    std::vector<float> mono;
    mono.reserve(total);
    size_t done = 0;
    bool first = true;
    while (done < total) {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        data.inputEvents = first ? &events : nullptr;
        proc.process(data);
        for (int i = 0; i < kBlock; ++i)
            mono.push_back(0.5f * (outL[static_cast<size_t>(i)] + outR[static_cast<size_t>(i)]));
        done += static_cast<size_t>(kBlock);
        first = false;
    }
    proc.setActive(false);
    proc.terminate();
    return mono;
}

// ---- Spectrogram: per-hop band energies -------------------------------------
struct Spectro {
    double hopSec = 0.0;
    std::vector<double> total;                 // total energy per hop
    std::vector<std::array<double, 6>> band;   // per-band energy per hop
    // Band edges (Hz): [0]=lowMid 300-1500, [1]=mid 1500-4k, [2]=hi 4k-8k,
    //                  [3]=vhi 8k-16k, [4]=hf 6k-16k (bloom band), [5]=body 500-2k
};

double bandEnergy(const std::vector<Krate::DSP::Complex>& spec, double binHz,
                  double lo, double hi) {
    double e = 0.0;
    for (size_t k = 1; k < spec.size(); ++k) {
        const double f = static_cast<double>(k) * binHz;
        if (f >= lo && f < hi) { const double m = spec[k].magnitude(); e += m * m; }
    }
    return e;
}

Spectro analyze(const std::vector<float>& mono, double sr) {
    constexpr size_t kFft = 2048;
    const size_t hop = static_cast<size_t>(0.025 * sr);  // 25 ms hops
    Krate::DSP::FFT fft;
    fft.prepare(kFft);
    std::vector<float> win(kFft), frame(kFft);
    for (size_t i = 0; i < kFft; ++i)
        win[i] = 0.5f * (1.0f - std::cos(2.0f * 3.14159265358979f *
                                         static_cast<float>(i) / static_cast<float>(kFft - 1)));
    std::vector<Krate::DSP::Complex> spec(kFft / 2 + 1);
    const double binHz = sr / static_cast<double>(kFft);

    Spectro s;
    s.hopSec = static_cast<double>(hop) / sr;
    for (size_t start = 0; start + kFft <= mono.size(); start += hop) {
        for (size_t i = 0; i < kFft; ++i) frame[i] = mono[start + i] * win[i];
        fft.forward(frame.data(), spec.data());
        double tot = 0.0;
        for (size_t k = 1; k < spec.size(); ++k) { const double m = spec[k].magnitude(); tot += m * m; }
        std::array<double, 6> b{};
        b[0] = bandEnergy(spec, binHz, 300.0, 1500.0);
        b[1] = bandEnergy(spec, binHz, 1500.0, 4000.0);
        b[2] = bandEnergy(spec, binHz, 4000.0, 8000.0);
        b[3] = bandEnergy(spec, binHz, 8000.0, 16000.0);
        b[4] = bandEnergy(spec, binHz, 6000.0, 16000.0);
        b[5] = bandEnergy(spec, binHz, 500.0, 2000.0);
        s.total.push_back(tot);
        s.band.push_back(b);
    }
    return s;
}

// Time (s) for a band's energy to fall 60 dB below its post-onset peak. Returns
// the render length if it never falls that far (i.e. still ringing).
double bandT60(const Spectro& s, int bandIdx) {
    double peak = 0.0; size_t peakHop = 0;
    for (size_t h = 0; h < s.band.size(); ++h)
        if (s.band[h][static_cast<size_t>(bandIdx)] > peak) { peak = s.band[h][static_cast<size_t>(bandIdx)]; peakHop = h; }
    if (peak <= 0.0) return 0.0;
    const double thresh = peak * 1e-6;  // -60 dB in energy
    for (size_t h = peakHop; h < s.band.size(); ++h)
        if (s.band[h][static_cast<size_t>(bandIdx)] < thresh)
            return static_cast<double>(h - peakHop) * s.hopSec;
    return static_cast<double>(s.band.size() - peakHop) * s.hopSec;
}

// HF energy fraction (6-16 kHz over total) at hop nearest tSec.
double hfRatioAt(const Spectro& s, double tSec) {
    const size_t h = std::min(s.band.size() - 1, static_cast<size_t>(tSec / s.hopSec));
    return s.total[h] > 0.0 ? s.band[h][4] / s.total[h] : 0.0;
}

// Peak HF fraction and its time, ignoring the first `skipSec` (click transient).
std::pair<double, double> hfRatioPeak(const Spectro& s, double skipSec, double untilSec) {
    double best = 0.0, bestT = 0.0;
    for (size_t h = 0; h < s.band.size(); ++h) {
        const double t = static_cast<double>(h) * s.hopSec;
        if (t < skipSec || t > untilSec) continue;
        const double r = s.total[h] > 0.0 ? s.band[h][4] / s.total[h] : 0.0;
        if (r > best) { best = r; bestT = t; }
    }
    return {best, bestT};
}

// Normalized-autocorrelation pitch salience over a window centred at tSec.
// Returns the max autocorr peak in the musical-pitch lag range [80, 2000] Hz.
double pitchSalience(const std::vector<float>& mono, double sr, double tSec) {
    const size_t win = static_cast<size_t>(0.100 * sr);   // 100 ms
    const size_t centre = static_cast<size_t>(tSec * sr);
    if (centre + win / 2 >= mono.size() || centre < win / 2) return 0.0;
    const size_t start = centre - win / 2;
    std::vector<double> x(win);
    double energy = 0.0;
    for (size_t i = 0; i < win; ++i) { x[i] = mono[start + i]; energy += x[i] * x[i]; }
    if (energy < 1e-12) return 0.0;
    const size_t lagMin = static_cast<size_t>(sr / 2000.0);
    const size_t lagMax = std::min(win - 1, static_cast<size_t>(sr / 80.0));
    double best = 0.0;
    for (size_t lag = lagMin; lag <= lagMax; ++lag) {
        double ac = 0.0;
        for (size_t i = 0; i + lag < win; ++i) ac += x[i] * x[i + lag];
        best = std::max(best, ac / energy);
    }
    return best;
}

double overallPeak(const std::vector<float>& mono) {
    double p = 0.0;
    for (float v : mono) p = std::max(p, std::fabs(static_cast<double>(v)));
    return p;
}

double windowRmsDb(const std::vector<float>& mono, double sr, double tSec, double winSec) {
    const size_t n = static_cast<size_t>(winSec * sr);
    const size_t start = static_cast<size_t>(tSec * sr);
    if (start + n >= mono.size()) return -160.0;
    double sq = 0.0;
    for (size_t i = 0; i < n; ++i) sq += static_cast<double>(mono[start + i]) * mono[start + i];
    const double rms = std::sqrt(sq / static_cast<double>(n));
    return rms > 1e-9 ? 20.0 * std::log10(rms) : -160.0;
}

}  // namespace

TEST_CASE("Crash AC-1: wash sustains ~1.5 s", "[membrum][crash][acceptance]") {
    auto mono = renderMono(kCrashNote, 1.0f, 4.0);
    const double peakDb = 20.0 * std::log10(overallPeak(mono));
    const double bodyDb = windowRmsDb(mono, kSampleRate, 0.3, 0.1);  // post-attack body
    const double washDb = windowRmsDb(mono, kSampleRate, 1.4, 0.2);  // late tail
    INFO("peakDb=" << peakDb << " bodyDb@0.3s=" << bodyDb
         << " washRmsDb@1.4s=" << washDb);
    // The wash must still be AUDIBLE ~1.4 s after the strike -- a real crash
    // rings on for seconds. The pre-redesign crash had fallen to ~-71.5 dBFS
    // here (effectively gone, retired by ~1.75 s); the redesigned crash holds a
    // graceful multi-second decay. (NOTE: this is an ABSOLUTE floor, not
    // relative-to-peak: the render's peak is the ~28 ms attack BLOOM, not a
    // click, so relative-to-peak conflates attack loudness with wash sustain --
    // it scored the dead baseline and a good crash identically at ~-57 dB.)
    REQUIRE(washDb > -70.0);
    // Sanity: we are measuring the decayed tail, not the attack.
    REQUIRE(washDb < bodyDb - 10.0);
}

TEST_CASE("Crash AC-2: bloom -- HF fraction rises after onset", "[membrum][crash][acceptance]") {
    auto mono = renderMono(kCrashNote, 1.0f, 4.0);
    auto s = analyze(mono, kSampleRate);
    const double hfEarly = hfRatioAt(s, 0.005);           // ~onset
    auto [hfPeak, hfPeakT] = hfRatioPeak(s, 0.020, 0.600); // exclude click
    INFO("hfEarly=" << hfEarly << " hfPeak=" << hfPeak << " @t=" << hfPeakT << "s");
    // The HF fraction must reach its maximum AFTER the onset window (delayed
    // energy cascade), somewhere in [20 ms, 400 ms].
    REQUIRE(hfPeakT >= 0.020);
    REQUIRE(hfPeakT <= 0.400);
    REQUIRE(hfPeak > hfEarly * 1.05);
}

TEST_CASE("Crash AC-3: frequency-dependent decay (dark through tail)", "[membrum][crash][acceptance]") {
    auto mono = renderMono(kCrashNote, 1.0f, 4.0);
    auto s = analyze(mono, kSampleRate);
    const double t60Low = bandT60(s, 0);  // 300-1500 Hz
    const double t60Mid = bandT60(s, 1);  // 1.5-4 kHz
    const double t60Hi  = bandT60(s, 3);  // 8-16 kHz
    INFO("T60 low(300-1500)=" << t60Low << " mid(1.5-4k)=" << t60Mid
         << " hi(8-16k)=" << t60Hi);
    // Low band must ring substantially longer than the high band, and decay must
    // be monotone (dark-through-tail): low >= mid >= high.
    REQUIRE(t60Low > 1.5);
    REQUIRE(t60Low >= t60Mid - 0.1);
    REQUIRE(t60Mid >= t60Hi - 0.1);
    REQUIRE(t60Low > t60Hi + 0.5);
}

TEST_CASE("Crash AC-4: no salient pitch in the tail", "[membrum][crash][acceptance]") {
    auto mono = renderMono(kCrashNote, 1.0f, 4.0);
    const double sal = pitchSalience(mono, kSampleRate, 0.8);
    INFO("pitchSalience@0.8s=" << sal);
    // A drone / harmonic stack would give a high autocorrelation peak. A crash's
    // dense inharmonic tail should not.
    REQUIRE(sal < 0.5);
}

TEST_CASE("Crash AC-5: soft hit blooms less than hard hit", "[membrum][crash][acceptance]") {
    auto hard = renderMono(kCrashNote, 1.0f, 4.0);
    auto soft = renderMono(kCrashNote, 0.3f, 4.0);
    auto sh = analyze(hard, kSampleRate);
    auto ss = analyze(soft, kSampleRate);
    auto [hardPeak, hardT] = hfRatioPeak(sh, 0.020, 0.600);
    auto [softPeak, softT] = hfRatioPeak(ss, 0.020, 0.600);
    INFO("hardBloomPeak=" << hardPeak << " softBloomPeak=" << softPeak);
    // Soft hit stays darker: its bloom HF-fraction peak is below the hard hit's.
    REQUIRE(softPeak <= hardPeak * 1.02);
}
