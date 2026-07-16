// ==============================================================================
// Hand-clap acceptance tests (HAND-CLAP-PLAN Section 5)
// ==============================================================================
// Renders the DEFAULT-KIT hand clap (pad 3, MIDI 39) through the full Membrum
// Processor and asserts the perceptual signatures a clap MUST have:
//   AC-1  multi-burst      -- 3-4 resolvable onsets in the first 45 ms,
//                             spaced 7-14 ms apart, fast (<2 ms) first attack
//   AC-2  no discrete partial -- broadband spectrum, no single ringing mode
//                             (guards the "copper triangle" failure)
//   AC-3  spectral placement -- centroid 1.2-2.5 kHz, energy centred 500-2000 Hz
//   AC-4  diffuse tail     -- ~120-400 ms T60, monotone decay, no ring plateau
//   AC-5  velocity regime  -- louder + non-darker with velocity, burst timing
//                             velocity-independent
//
// These encode the analysis in the clap-fix plan: a hand clap is a multi-impulse
// broadband-noise event, NOT a struck resonant body. The pre-fix pad (generic
// DrumTemplate::Perc = Mallet + Plate) produced exactly ONE onset with a
// dominant ~403 Hz partial (autocorrelation 0.92) and centroid ~687 Hz -- these
// tests fail on that render and pass on the multi-burst redesign. Thresholds
// are deliberately generous: they separate "clap" from "metal", not fine-tune.
// ==============================================================================
#include <catch2/catch_test_macros.hpp>

#include "processor/processor.h"

#include <krate/dsp/primitives/fft.h>

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstevents.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int    kBlock      = 512;
constexpr int    kClapNote   = 39;  // pad 3, Hand Clap

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

// ---- Amplitude envelope: rectify + one-pole smooth (~1 ms) ------------------
std::vector<float> smoothedEnvelope(const std::vector<float>& mono, double sr,
                                    double tauSec = 0.001) {
    const float a = static_cast<float>(std::exp(-1.0 / (tauSec * sr)));
    std::vector<float> env(mono.size());
    float state = 0.0f;
    for (size_t i = 0; i < mono.size(); ++i) {
        const float rect = std::fabs(mono[i]);
        state = rect + a * (state - rect);
        // One-pole tracks fast upward too slowly for a <1 ms attack; use a
        // peak-hold style follower: jump up instantly, smooth downward.
        if (rect > state) state = rect;
        env[i] = state;
    }
    return env;
}

// Count local maxima in env[0, windowSec) that exceed `relThresh * globalPeak`,
// merging maxima closer than `minSepSec` (keep the larger). Returns peak sample
// indices, ascending.
std::vector<size_t> burstPeaks(const std::vector<float>& env, double sr,
                               double windowSec, double relThresh,
                               double minSepSec) {
    const size_t n = std::min(env.size(), static_cast<size_t>(windowSec * sr));
    float globalPeak = 0.0f;
    for (size_t i = 0; i < n; ++i) globalPeak = std::max(globalPeak, env[i]);
    const float thresh = static_cast<float>(relThresh) * globalPeak;
    const auto minSep = static_cast<size_t>(minSepSec * sr);

    std::vector<size_t> peaks;
    for (size_t i = 1; i + 1 < n; ++i) {
        if (env[i] < thresh) continue;
        if (env[i] >= env[i - 1] && env[i] > env[i + 1]) {
            if (!peaks.empty() && i - peaks.back() < minSep) {
                if (env[i] > env[peaks.back()]) peaks.back() = i;  // keep larger
            } else {
                peaks.push_back(i);
            }
        }
    }
    return peaks;
}

// ---- Averaged power spectrum over the whole render (Hann, 50% overlap) ------
struct AvgSpectrum {
    std::vector<double> power;  // size kFft/2+1
    double binHz = 0.0;
};

AvgSpectrum averageSpectrum(const std::vector<float>& mono, double sr) {
    constexpr size_t kFft = 8192;
    Krate::DSP::FFT fft;
    fft.prepare(kFft);
    std::vector<float> win(kFft), frame(kFft);
    for (size_t i = 0; i < kFft; ++i)
        win[i] = 0.5f * (1.0f - std::cos(2.0f * 3.14159265358979f *
                                         static_cast<float>(i) / static_cast<float>(kFft - 1)));
    std::vector<Krate::DSP::Complex> spec(kFft / 2 + 1);

    AvgSpectrum out;
    out.binHz = sr / static_cast<double>(kFft);
    out.power.assign(kFft / 2 + 1, 0.0);

    size_t frames = 0;
    for (size_t start = 0; start + kFft <= mono.size(); start += kFft / 2) {
        for (size_t i = 0; i < kFft; ++i) frame[i] = mono[start + i] * win[i];
        fft.forward(frame.data(), spec.data());
        for (size_t k = 0; k < spec.size(); ++k) {
            const double m = spec[k].magnitude();
            out.power[k] += m * m;
        }
        ++frames;
    }
    // Short render safeguard: single zero-padded frame if render < kFft.
    if (frames == 0) {
        std::fill(frame.begin(), frame.end(), 0.0f);
        for (size_t i = 0; i < std::min(mono.size(), kFft); ++i)
            frame[i] = mono[i] * win[i];
        fft.forward(frame.data(), spec.data());
        for (size_t k = 0; k < spec.size(); ++k) {
            const double m = spec[k].magnitude();
            out.power[k] += m * m;
        }
        frames = 1;
    }
    for (auto& p : out.power) p /= static_cast<double>(frames);
    return out;
}

double spectralCentroid(const AvgSpectrum& s) {
    double num = 0.0, den = 0.0;
    for (size_t k = 1; k < s.power.size(); ++k) {
        const double mag = std::sqrt(s.power[k]);
        num += static_cast<double>(k) * s.binHz * mag;
        den += mag;
    }
    return den > 0.0 ? num / den : 0.0;
}

// Fraction of total power falling in [lo, hi) Hz.
double bandFraction(const AvgSpectrum& s, double lo, double hi) {
    double band = 0.0, total = 0.0;
    for (size_t k = 1; k < s.power.size(); ++k) {
        const double f = static_cast<double>(k) * s.binHz;
        total += s.power[k];
        if (f >= lo && f < hi) band += s.power[k];
    }
    return total > 0.0 ? band / total : 0.0;
}

// Spectral flatness (geometric mean / arithmetic mean of power) over [lo, hi) Hz.
double spectralFlatness(const AvgSpectrum& s, double lo, double hi) {
    double logSum = 0.0, sum = 0.0;
    size_t count = 0;
    for (size_t k = 1; k < s.power.size(); ++k) {
        const double f = static_cast<double>(k) * s.binHz;
        if (f < lo || f >= hi) continue;
        const double p = s.power[k] + 1e-30;
        logSum += std::log(p);
        sum += p;
        ++count;
    }
    if (count == 0 || sum <= 0.0) return 0.0;
    const double geo = std::exp(logSum / static_cast<double>(count));
    const double ari = sum / static_cast<double>(count);
    return geo / ari;
}

// Max excess (dB) of any bin over its +-1/6-octave smoothed local spectral
// envelope, within [lo, hi) Hz. A lone ringing partial pokes far above its
// neighbourhood; broadband noise does not.
double maxPartialExcessDb(const AvgSpectrum& s, double lo, double hi) {
    constexpr double kSixthOct = 1.122462048308935;  // 2^(1/6)
    double worst = -160.0;
    for (size_t k = 1; k < s.power.size(); ++k) {
        const double f = static_cast<double>(k) * s.binHz;
        if (f < lo || f >= hi) continue;
        const auto kLo = static_cast<size_t>(std::max(1.0, std::floor(static_cast<double>(k) / kSixthOct)));
        const auto kHi = std::min(s.power.size() - 1,
                                  static_cast<size_t>(std::ceil(static_cast<double>(k) * kSixthOct)));
        double local = 0.0;
        size_t cnt = 0;
        for (size_t j = kLo; j <= kHi; ++j) { local += s.power[j]; ++cnt; }
        if (cnt == 0) continue;
        local /= static_cast<double>(cnt);
        if (local <= 0.0) continue;
        const double excess = 10.0 * std::log10((s.power[k] + 1e-30) / local);
        worst = std::max(worst, excess);
    }
    return worst;
}

// ---- 25 ms-hop RMS envelope + T60 fit ---------------------------------------
std::vector<double> hopRmsDb(const std::vector<float>& mono, double sr,
                             double hopSec = 0.025) {
    const auto hop = static_cast<size_t>(hopSec * sr);
    std::vector<double> out;
    for (size_t start = 0; start + hop <= mono.size(); start += hop) {
        double sq = 0.0;
        for (size_t i = 0; i < hop; ++i)
            sq += static_cast<double>(mono[start + i]) * mono[start + i];
        const double rms = std::sqrt(sq / static_cast<double>(hop));
        out.push_back(rms > 1e-9 ? 20.0 * std::log10(rms) : -160.0);
    }
    return out;
}

// Least-squares linear fit of dB vs time over hops in [fromSec, +inf) whose
// level stays above `floorDb`; returns T60 (seconds) = 60 / |slope|.
double fitT60(const std::vector<double>& rmsDb, double hopSec, double fromSec,
              double floorDb = -80.0) {
    double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
    int n = 0;
    for (size_t h = 0; h < rmsDb.size(); ++h) {
        const double t = static_cast<double>(h) * hopSec;
        if (t < fromSec || rmsDb[h] <= floorDb) continue;
        sx += t; sy += rmsDb[h]; sxx += t * t; sxy += t * rmsDb[h];
        ++n;
    }
    if (n < 3) return 0.0;
    const double denom = static_cast<double>(n) * sxx - sx * sx;
    if (std::fabs(denom) < 1e-12) return 0.0;
    const double slope = (static_cast<double>(n) * sxy - sx * sy) / denom;  // dB/s
    if (slope >= -1e-6) return 1e9;  // not decaying
    return 60.0 / -slope;
}

double overallPeak(const std::vector<float>& mono) {
    double p = 0.0;
    for (float v : mono) p = std::max(p, std::fabs(static_cast<double>(v)));
    return p;
}

}  // namespace

TEST_CASE("CLAP multi-burst count", "[membrum][clap][acceptance]") {
    auto mono = renderMono(kClapNote, 0.9f, 0.6);
    auto env = smoothedEnvelope(mono, kSampleRate);
    auto peaks = burstPeaks(env, kSampleRate, 0.045, 0.25, 0.005);

    INFO("burst peaks in first 45 ms: " << peaks.size());
    for (size_t p : peaks)
        INFO("  peak @ " << (static_cast<double>(p) / kSampleRate * 1000.0) << " ms");

    // A clap is 3-4 rapid onsets ("several hands not quite in sync"); the
    // pre-fix Perc pad produces exactly ONE.
    REQUIRE(peaks.size() >= 3);
    REQUIRE(peaks.size() <= 4);

    // Mean inter-peak spacing in the 7-14 ms flam band.
    double spacingSum = 0.0;
    for (size_t i = 1; i < peaks.size(); ++i)
        spacingSum += static_cast<double>(peaks[i] - peaks[i - 1]) / kSampleRate;
    const double meanSpacingMs = spacingSum / static_cast<double>(peaks.size() - 1) * 1000.0;
    INFO("mean inter-peak spacing = " << meanSpacingMs << " ms");
    REQUIRE(meanSpacingMs >= 7.0);
    REQUIRE(meanSpacingMs <= 14.0);

    // First-peak 10%-90% rise < 2 ms (clap attack is a crack, not a swell).
    const size_t p0 = peaks[0];
    const float peakVal = env[p0];
    size_t i90 = p0;
    while (i90 > 0 && env[i90 - 1] >= 0.9f * peakVal) --i90;
    size_t i10 = i90;
    while (i10 > 0 && env[i10 - 1] >= 0.1f * peakVal) --i10;
    const double riseMs = static_cast<double>(i90 - i10) / kSampleRate * 1000.0;
    INFO("first-peak 10-90% rise = " << riseMs << " ms");
    REQUIRE(riseMs < 2.0);
}

TEST_CASE("CLAP no discrete partial", "[membrum][clap][acceptance]") {
    auto mono = renderMono(kClapNote, 0.9f, 0.6);
    auto spec = averageSpectrum(mono, kSampleRate);

    const double flatness = spectralFlatness(spec, 500.0, 6000.0);
    INFO("spectral flatness 500 Hz - 6 kHz = " << flatness);
    // A single ringing mode (copper triangle: ~403 Hz partial, autocorr 0.92)
    // collapses flatness toward 0; broadband clap noise stays well above.
    REQUIRE(flatness > 0.15);

    const double excessDb = maxPartialExcessDb(spec, 500.0, 6000.0);
    INFO("max bin excess over +-1/6-oct local envelope = " << excessDb << " dB");
    REQUIRE(excessDb < 8.0);
}

TEST_CASE("CLAP spectral placement", "[membrum][clap][acceptance]") {
    auto mono = renderMono(kClapNote, 0.9f, 0.6);
    auto spec = averageSpectrum(mono, kSampleRate);

    const double centroid = spectralCentroid(spec);
    INFO("spectral centroid = " << centroid << " Hz (pre-fix Perc was ~687 Hz)");
    REQUIRE(centroid >= 1200.0);
    REQUIRE(centroid <= 2500.0);

    const double fracLow = bandFraction(spec, 100.0, 500.0);
    const double fracMid = bandFraction(spec, 500.0, 2000.0);
    INFO("band fractions: 100-500 Hz = " << fracLow << ", 500-2000 Hz = " << fracMid);
    // Pre-fix Perc pad: 0.9942 in 100-500 Hz, 0.0047 in 500-2000 Hz.
    REQUIRE(fracLow < 0.45);
    REQUIRE(fracMid > 0.35);
}

TEST_CASE("CLAP diffuse tail, no ring", "[membrum][clap][acceptance]") {
    auto mono = renderMono(kClapNote, 0.7f, 0.6);
    auto rmsDb = hopRmsDb(mono, kSampleRate);
    constexpr double kHopSec = 0.025;

    const double t60 = fitT60(rmsDb, kHopSec, 0.045);
    INFO("tail T60 = " << t60 * 1000.0 << " ms");
    // The diffuse room tail: long enough to read as a clap's "reverb", short
    // enough not to be a ringing body / undamped plateau.
    REQUIRE(t60 >= 0.120);
    REQUIRE(t60 <= 0.400);

    // Monotone decay after 60 ms: no secondary attack, no undamped plateau.
    // Compare smoothed (3-hop mean) levels in LINEAR amplitude: each hop must
    // stay <= 1.3x the previous smoothed hop.
    std::vector<double> lin(rmsDb.size());
    for (size_t h = 0; h < rmsDb.size(); ++h)
        lin[h] = std::pow(10.0, rmsDb[h] / 20.0);
    const auto firstHop = static_cast<size_t>(0.060 / kHopSec);
    for (size_t h = firstHop + 1; h < lin.size(); ++h) {
        auto smoothAt = [&](size_t idx) {
            const size_t lo = idx > 0 ? idx - 1 : 0;
            const size_t hi = std::min(lin.size() - 1, idx + 1);
            double sum = 0.0;
            for (size_t j = lo; j <= hi; ++j) sum += lin[j];
            return sum / static_cast<double>(hi - lo + 1);
        };
        const double prev = smoothAt(h - 1);
        const double cur  = smoothAt(h);
        if (prev < 1e-7) continue;  // below audibility; ratios meaningless
        INFO("hop " << h << " (t=" << static_cast<double>(h) * kHopSec << " s): "
             << cur << " vs prev " << prev);
        REQUIRE(cur <= prev * 1.3);
    }
}

TEST_CASE("CLAP velocity response", "[membrum][clap][acceptance]") {
    constexpr std::array<float, 4> kVels = {0.25f, 0.5f, 0.75f, 1.0f};
    std::array<double, 4> peakDb{}, centroid{};
    for (size_t v = 0; v < kVels.size(); ++v) {
        auto mono = renderMono(kClapNote, kVels[v], 0.6);
        peakDb[v] = 20.0 * std::log10(std::max(1e-9, overallPeak(mono)));
        centroid[v] = spectralCentroid(averageSpectrum(mono, kSampleRate));
        INFO("vel " << kVels[v] << ": peak " << peakDb[v] << " dBFS, centroid "
             << centroid[v] << " Hz");
    }
    // Louder with velocity (strictly increasing peak).
    for (size_t v = 1; v < kVels.size(); ++v) {
        INFO("peak[" << v - 1 << "]=" << peakDb[v - 1] << " peak[" << v << "]=" << peakDb[v]);
        REQUIRE(peakDb[v] > peakDb[v - 1]);
    }
    // Never darker with velocity (non-decreasing centroid).
    for (size_t v = 1; v < kVels.size(); ++v) {
        INFO("centroid[" << v - 1 << "]=" << centroid[v - 1]
             << " centroid[" << v << "]=" << centroid[v]);
        REQUIRE(centroid[v] >= centroid[v - 1]);
    }

    // Burst TIMING is velocity-independent: same burst count soft and hard.
    auto softEnv = smoothedEnvelope(renderMono(kClapNote, 0.25f, 0.6), kSampleRate);
    auto hardEnv = smoothedEnvelope(renderMono(kClapNote, 1.0f, 0.6), kSampleRate);
    const auto softPeaks = burstPeaks(softEnv, kSampleRate, 0.045, 0.25, 0.005);
    const auto hardPeaks = burstPeaks(hardEnv, kSampleRate, 0.045, 0.25, 0.005);
    INFO("burst count soft=" << softPeaks.size() << " hard=" << hardPeaks.size());
    REQUIRE(softPeaks.size() == hardPeaks.size());
    REQUIRE(softPeaks.size() >= 3);
    REQUIRE(softPeaks.size() <= 4);
}
