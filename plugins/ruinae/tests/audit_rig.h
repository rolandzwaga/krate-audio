#pragma once

// =============================================================================
// Shared rig and measurements promoted from the Ruinae audit probes (T6.1)
// =============================================================================
// Source: specs/ruinae-audit/probes/audit_probes.cpp.txt (left unchanged as the
// audit's evidence). One real Ruinae::Processor driven by a schedule of events
// and parameter changes at absolute sample positions, plus the metrics the
// regression tests of phase 6 share: pitch in cents, alias analysis, click
// ("jump ratio") at an event, burst onsets and the block-size-invariance
// renderer.
//
// Header-only; everything lives in RuinaeAuditRig so no test TU needs an
// anonymous-namespace copy.
// =============================================================================

#include "processor/processor.h"
#include "plugin_ids.h"

#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstnoteexpression.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include "vst_event_list.h"
#include "vst_param_changes.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace RuinaeAuditRig {

inline constexpr double kPi = 3.14159265358979323846;

/// The processor with its engine exposed to the tests.
struct ProbeProc : Ruinae::Processor {
    using Ruinae::Processor::engine;
};

/// An output event list that lets the rig read back what the processor emitted.
class CapturedEvents final : public Krate::Test::EventList {
public:
    const std::vector<Steinberg::Vst::Event>& captured() const { return events_; }
};

struct Sched {
    int64_t pos = 0;
    bool isParam = false;
    Steinberg::Vst::ParamID id = 0;
    double value = 0.0;
    Steinberg::Vst::Event ev{};
};

struct OutNote {
    int64_t pos = 0;
    bool on = false;
    int pitch = 0;
};

/// One real Processor, a schedule of events and parameter changes at absolute
/// samples, and the rendered output at absolute sample indices.
class Rig {
public:
    explicit Rig(double sr, Steinberg::int32 maxBlock = 2048) : sr_(sr) {
        proc = std::make_unique<ProbeProc>();
        proc->initialize(nullptr);
        Steinberg::Vst::ProcessSetup s{};
        s.processMode = Steinberg::Vst::kRealtime;
        s.symbolicSampleSize = Steinberg::Vst::kSample32;
        s.sampleRate = sr;
        s.maxSamplesPerBlock = maxBlock;
        proc->setupProcessing(s);
        proc->setActive(true);
    }
    ~Rig() {
        proc->setActive(false);
        proc->terminate();
    }
    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;

    double sr() const { return sr_; }
    int64_t now() const { return now_; }
    int64_t at(double seconds) const { return static_cast<int64_t>(std::llround(seconds * sr_)); }

    void param(Steinberg::Vst::ParamID id, double v, int64_t pos = -1) {
        Sched s;
        s.pos = pos < 0 ? now_ : pos;
        s.isParam = true;
        s.id = id;
        s.value = v;
        sched_.push_back(s);
    }
    void noteOn(int pitch, float vel, int64_t pos, Steinberg::int32 noteId = -1) {
        Sched s;
        s.pos = pos;
        s.ev.type = Steinberg::Vst::Event::kNoteOnEvent;
        s.ev.noteOn.channel = 0;
        s.ev.noteOn.pitch = static_cast<Steinberg::int16>(pitch);
        s.ev.noteOn.velocity = vel;
        s.ev.noteOn.noteId = noteId;
        s.ev.noteOn.tuning = 0.0f;
        s.ev.noteOn.length = 0;
        sched_.push_back(s);
    }
    void noteOff(int pitch, int64_t pos, Steinberg::int32 noteId = -1) {
        Sched s;
        s.pos = pos;
        s.ev.type = Steinberg::Vst::Event::kNoteOffEvent;
        s.ev.noteOff.channel = 0;
        s.ev.noteOff.pitch = static_cast<Steinberg::int16>(pitch);
        s.ev.noteOff.velocity = 0.0f;
        s.ev.noteOff.noteId = noteId;
        s.ev.noteOff.tuning = 0.0f;
        sched_.push_back(s);
    }
    void bend(int msb, int lsb, int64_t pos) {
        Sched s;
        s.pos = pos;
        s.ev.type = Steinberg::Vst::Event::kLegacyMIDICCOutEvent;
        s.ev.midiCCOut.controlNumber = Steinberg::Vst::ControllerNumbers::kPitchBend;
        s.ev.midiCCOut.channel = 0;
        s.ev.midiCCOut.value = static_cast<Steinberg::int8>(msb);
        s.ev.midiCCOut.value2 = static_cast<Steinberg::int8>(lsb);
        sched_.push_back(s);
    }
    void expr(Steinberg::int32 noteId, Steinberg::Vst::NoteExpressionTypeID type, double v, int64_t pos) {
        Sched s;
        s.pos = pos;
        s.ev.type = Steinberg::Vst::Event::kNoteExpressionValueEvent;
        s.ev.noteExpressionValue.typeId = type;
        s.ev.noteExpressionValue.noteId = noteId;
        s.ev.noteExpressionValue.value = v;
        sched_.push_back(s);
    }

    void render(int64_t n, Steinberg::int32 bs = 512) {
        std::stable_sort(sched_.begin(), sched_.end(),
                         [](const Sched& a, const Sched& b) { return a.pos < b.pos; });
        const int64_t end = now_ + n;
        L.resize(static_cast<size_t>(end), 0.0f);
        R.resize(static_cast<size_t>(end), 0.0f);
        while (now_ < end) {
            const auto len = static_cast<Steinberg::int32>(std::min<int64_t>(bs, end - now_));
            params_.clear();
            inEv_.clear();
            outEv_.clear();
            std::vector<Sched> rest;
            for (const auto& s : sched_) {
                if (s.pos < now_ + len) {
                    if (s.isParam) {
                        params_.addChange(s.id, s.value);
                    } else {
                        Steinberg::Vst::Event e = s.ev;
                        e.sampleOffset = static_cast<Steinberg::int32>(std::max<int64_t>(0, s.pos - now_));
                        inEv_.addEvent(e);
                    }
                } else {
                    rest.push_back(s);
                }
            }
            sched_.swap(rest);

            float* ch[2] = {L.data() + now_, R.data() + now_};
            Steinberg::Vst::AudioBusBuffers out{};
            out.numChannels = 2;
            out.channelBuffers32 = ch;

            Steinberg::Vst::ProcessContext ctx{};
            ctx.sampleRate = sr_;
            ctx.tempo = tempo;
            ctx.timeSigNumerator = 4;
            ctx.timeSigDenominator = 4;
            ctx.projectTimeMusic = ppq;
            ctx.projectTimeSamples = now_;
            ctx.state = Steinberg::Vst::ProcessContext::kTempoValid |
                        Steinberg::Vst::ProcessContext::kProjectTimeMusicValid |
                        Steinberg::Vst::ProcessContext::kTimeSigValid;
            if (playing) ctx.state |= Steinberg::Vst::ProcessContext::kPlaying;

            Steinberg::Vst::ProcessData d{};
            d.processMode = Steinberg::Vst::kRealtime;
            d.symbolicSampleSize = Steinberg::Vst::kSample32;
            d.numSamples = len;
            d.numInputs = 0;
            d.numOutputs = 1;
            d.outputs = &out;
            d.inputParameterChanges = &params_;
            d.inputEvents = &inEv_;
            d.outputEvents = &outEv_;
            d.processContext = useCtx ? &ctx : nullptr;
            proc->process(d);

            for (const auto& e : outEv_.captured()) {
                if (e.type == Steinberg::Vst::Event::kNoteOnEvent)
                    outNotes.push_back({now_ + e.sampleOffset, true, e.noteOn.pitch});
                else if (e.type == Steinberg::Vst::Event::kNoteOffEvent)
                    outNotes.push_back({now_ + e.sampleOffset, false, e.noteOff.pitch});
            }
            if (onBlock) onBlock(now_, len);
            now_ += len;
            if (useCtx && playing) ppq += static_cast<double>(len) / sr_ * (tempo / 60.0);
        }
    }

    std::vector<float> mono(size_t a = 0, size_t b = SIZE_MAX) const {
        b = std::min(b, L.size());
        std::vector<float> m;
        if (a >= b) return m;
        m.resize(b - a);
        for (size_t i = a; i < b; ++i) m[i - a] = 0.5f * (L[i] + R[i]);
        return m;
    }

    std::unique_ptr<ProbeProc> proc;
    std::vector<float> L, R;
    std::vector<OutNote> outNotes;
    std::function<void(int64_t, Steinberg::int32)> onBlock;
    bool useCtx = false;
    bool playing = true;
    double tempo = 120.0;
    double ppq = 0.0;

private:
    double sr_;
    int64_t now_ = 0;
    std::vector<Sched> sched_;
    Krate::Test::ParameterChanges params_;
    Krate::Test::EventList inEv_;
    CapturedEvents outEv_;
};

/// Normalised envelope time for @p ms (cube-root law, 0..10 s).
inline double envNorm(double ms) { return std::cbrt(std::clamp(ms, 0.0, 10000.0) / 10000.0); }

inline double noteHz(int note, double a4 = 440.0) { return a4 * std::pow(2.0, (note - 69) / 12.0); }

/// Neutral patch: OSC A only (saw by default), filter open, Clean, FX off.
inline void basePatch(Rig& r, double waveformNorm = 0.25) {
    r.param(Ruinae::kMasterGainId, 0.5);
    r.param(Ruinae::kVoiceModeId, 0.0);
    r.param(Ruinae::kPolyphonyId, 7.0 / 15.0);
    r.param(Ruinae::kSoftLimitId, 1.0);
    r.param(Ruinae::kWidthId, 0.5);
    r.param(Ruinae::kSpreadId, 0.0);
    r.param(Ruinae::kOscATypeId, 0.0);
    r.param(Ruinae::kOscAWaveformId, waveformNorm);
    r.param(Ruinae::kOscALevelId, 1.0);
    r.param(Ruinae::kOscATuneId, 0.5);
    r.param(Ruinae::kOscAFineId, 0.5);
    r.param(Ruinae::kOscBLevelId, 0.0);
    r.param(Ruinae::kMixerModeId, 0.0);
    r.param(Ruinae::kMixerPositionId, 0.0);
    r.param(Ruinae::kFilterTypeId, 0.0);
    r.param(Ruinae::kFilterCutoffId, 1.0);
    r.param(Ruinae::kFilterResonanceId, 0.0203);
    r.param(Ruinae::kFilterEnvAmountId, 0.5);
    r.param(Ruinae::kFilterKeyTrackId, 0.0);
    r.param(Ruinae::kDistortionTypeId, 0.0);
    r.param(Ruinae::kDistortionMixId, 1.0);
    r.param(Ruinae::kTranceGateEnabledId, 0.0);
    r.param(Ruinae::kAmpEnvAttackId, envNorm(1.0));
    r.param(Ruinae::kAmpEnvDecayId, envNorm(200.0));
    r.param(Ruinae::kAmpEnvSustainId, 1.0);
    r.param(Ruinae::kAmpEnvReleaseId, envNorm(100.0));
    r.param(Ruinae::kDelayEnabledId, 0.0);
    r.param(Ruinae::kReverbEnabledId, 0.0);
    r.param(Ruinae::kModulationTypeId, 0.0);
    r.param(Ruinae::kHarmonizerEnabledId, 0.0);
    r.param(Ruinae::kGlobalFilterEnabledId, 0.0);
    r.param(Ruinae::kSettingsTuningReferenceId, 0.5);
    r.param(Ruinae::kSettingsPitchBendRangeId, 2.0 / 24.0);
    r.param(Ruinae::kSettingsVelocityCurveId, 0.0);
    r.param(Ruinae::kArpOperatingModeId, 0.0);
}

// ------------------------------------------------------------------------------
// Metrics
// ------------------------------------------------------------------------------

inline void fft(std::vector<std::complex<double>>& a) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2.0 * kPi / static_cast<double>(len);
        const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k) {
                const auto u = a[i + k];
                const auto v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

/// Blackman-Harris 4-term windowed power spectrum, bins 0..n/2 (n a power of 2).
inline std::vector<double> powerSpectrum(const float* x, size_t n) {
    std::vector<std::complex<double>> a(n);
    for (size_t i = 0; i < n; ++i) {
        const double t = 2.0 * kPi * static_cast<double>(i) / static_cast<double>(n - 1);
        const double w = 0.35875 - 0.48829 * std::cos(t) + 0.14128 * std::cos(2 * t) -
                         0.01168 * std::cos(3 * t);
        a[i] = std::complex<double>(static_cast<double>(x[i]) * w, 0.0);
    }
    fft(a);
    std::vector<double> p(n / 2 + 1);
    for (size_t k = 0; k <= n / 2; ++k) p[k] = std::norm(a[k]);
    return p;
}

inline double goertzelMag(const float* x, size_t n, double f, double sr) {
    const double w = 2.0 * kPi * f / sr;
    const double c = 2.0 * std::cos(w);
    double s1 = 0, s2 = 0;
    for (size_t i = 0; i < n; ++i) {
        const double win =
            0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(i) / static_cast<double>(n - 1));
        const double s0 = static_cast<double>(x[i]) * win + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return std::sqrt(std::max(0.0, s1 * s1 + s2 * s2 - c * s1 * s2));
}

/// Peak search around @p fExp (+-span cents); returns the cents of the peak
/// relative to @p fExp.
inline double pitchCents(const float* x, size_t n, double sr, double fExp, double span = 100.0) {
    double best = -1, bestC = 0;
    for (double c = -span; c <= span; c += 2.0) {
        const double m = goertzelMag(x, n, fExp * std::pow(2.0, c / 1200.0), sr);
        if (m > best) {
            best = m;
            bestC = c;
        }
    }
    const double c0 = bestC;
    for (double c = c0 - 2.0; c <= c0 + 2.0; c += 0.05) {
        const double m = goertzelMag(x, n, fExp * std::pow(2.0, c / 1200.0), sr);
        if (m > best) {
            best = m;
            bestC = c;
        }
    }
    return bestC;
}

struct AliasRes {
    double worstDbc = 0, totalDb = 0, centroidHz = 0;
};

/// Worst and total non-harmonic energy of @p n samples (power of 2) of a tone at @p f0.
inline AliasRes aliasAnalysis(const float* x, size_t n, double sr, double f0) {
    AliasRes r;
    const auto p = powerSpectrum(x, n);
    const double binHz = sr / static_cast<double>(n);
    std::vector<char> harm(p.size(), 0);
    const auto guard = static_cast<long>(6);
    const auto dcBins = static_cast<size_t>(std::ceil(25.0 / binHz));
    double fundPeak = 0;
    for (int h = 1; h < 20000 && static_cast<double>(h) * f0 < sr / 2.0; ++h) {
        const auto c = static_cast<long>(std::llround(static_cast<double>(h) * f0 / binHz));
        for (long k = c - guard; k <= c + guard; ++k)
            if (k >= 0 && k < static_cast<long>(p.size())) {
                harm[static_cast<size_t>(k)] = 1;
                if (h == 1) fundPeak = std::max(fundPeak, p[static_cast<size_t>(k)]);
            }
    }
    double hSum = 0, aSum = 0, worst = 0, num = 0, den = 0;
    for (size_t k = 0; k < p.size(); ++k) {
        num += static_cast<double>(k) * binHz * p[k];
        den += p[k];
        if (k < dcBins) continue;
        if (harm[k]) {
            hSum += p[k];
        } else {
            aSum += p[k];
            worst = std::max(worst, p[k]);
        }
    }
    r.centroidHz = den > 0 ? num / den : 0;
    r.worstDbc = (fundPeak > 0 && worst > 0) ? 10.0 * std::log10(worst / fundPeak) : -240.0;
    r.totalDb = (hSum > 0 && aSum > 0) ? 10.0 * std::log10(aSum / hSum) : -240.0;
    return r;
}

struct ClickRes {
    double ratio = 0;
    int count = 0;
    double maxJump = 0;
};

/// Discontinuity at a discrete event: max |dx| in +-win around @p ev against
/// the steady max |dx| over @p refLen before and after the window (the
/// audit's "jump ratio").
inline ClickRes clickAt(const std::vector<float>& x, size_t ev, size_t win, size_t refLen) {
    ClickRes c;
    auto maxD = [&](size_t a, size_t b) {
        double m = 0;
        b = std::min(b, x.size());
        for (size_t i = std::max<size_t>(a, 1); i < b; ++i)
            m = std::max(m, static_cast<double>(std::fabs(x[i] - x[i - 1])));
        return m;
    };
    const size_t a = ev > win ? ev - win : 0;
    const size_t b = std::min(ev + win, x.size());
    const double refB = a > refLen ? maxD(a - refLen, a) : maxD(0, a);
    const double refA = maxD(b, b + refLen);
    const double ref = std::max({refB, refA, 1e-9});
    double m = 0;
    for (size_t i = std::max<size_t>(a, 1); i < b; ++i) {
        const double d = std::fabs(x[i] - x[i - 1]);
        m = std::max(m, d);
        if (d > 2.0 * ref) ++c.count;
    }
    c.maxJump = m;
    c.ratio = m / ref;
    return c;
}

/// Burst onsets: 2 ms peak envelope crossing thrRel * max after >= 20 ms below half of it.
inline std::vector<size_t> burstOnsets(const std::vector<float>& x, size_t from, double sr, double thrRel) {
    std::vector<size_t> on;
    const auto hop = std::max<size_t>(1, static_cast<size_t>(0.002 * sr));
    double gmax = 0;
    for (size_t i = from; i < x.size(); ++i) gmax = std::max(gmax, static_cast<double>(std::fabs(x[i])));
    if (gmax <= 0) return on;
    const double thr = thrRel * gmax;
    size_t quietHops = 1000;
    for (size_t s = from; s + hop <= x.size(); s += hop) {
        double p = 0;
        for (size_t i = s; i < s + hop; ++i) p = std::max(p, static_cast<double>(std::fabs(x[i])));
        if (p >= thr) {
            if (quietHops >= 10) {
                size_t i = s;
                while (i < s + hop && std::fabs(x[i]) < thr) ++i;
                on.push_back(i);
            }
            quietHops = 0;
        } else if (p < 0.5 * thr) {
            ++quietHops;
        }
    }
    return on;
}

/// The audit's block-size-invariance render: 2 s of mono at 48 kHz in buffers
/// of @p bs samples. Variants: 0 plain notes, 1 + pitch bends, 2 + LFO1 to
/// global and all-voice cutoff, 3 + digital delay and plate reverb, 4 mono
/// legato portamento.
inline std::vector<float> renderInvariance(int variant, Steinberg::int32 bs) {
    Rig r(48000.0, 2048);
    basePatch(r);
    r.param(Ruinae::kFilterCutoffId, 0.6);
    r.param(Ruinae::kFilterResonanceId, 0.3);
    r.param(Ruinae::kFilterEnvAmountId, 0.7);
    r.param(Ruinae::kFilterEnvAttackId, envNorm(30));
    r.param(Ruinae::kFilterEnvDecayId, envNorm(300));
    r.param(Ruinae::kFilterEnvSustainId, 0.3);
    r.param(Ruinae::kAmpEnvAttackId, envNorm(5));
    r.noteOn(60, 0.8f, 1000);
    r.noteOn(64, 0.7f, 7777);
    r.noteOff(60, 20000);
    r.noteOn(67, 0.9f, 30001);
    r.noteOff(64, 40000);
    r.noteOff(67, 60000);
    if (variant == 1) {
        r.bend(127, 127, 10000);
        r.bend(64, 0, 25000);
        r.bend(0, 0, 45001);
    }
    if (variant == 2) {
        r.param(Ruinae::kGlobalFilterEnabledId, 1.0);
        r.param(Ruinae::kGlobalFilterCutoffId, 0.6);
        r.param(Ruinae::kLFO1RateId, 0.622);
        r.param(Ruinae::kLFO1DepthId, 1.0);
        r.param(Ruinae::kLFO1SyncId, 0.0);
        r.param(Ruinae::kModMatrixSlot0SourceId, 1.0 / 14.0);
        r.param(Ruinae::kModMatrixSlot0DestId, 0.0);
        r.param(Ruinae::kModMatrixSlot0AmountId, 0.9);
        r.param(Ruinae::kModMatrixSlot1SourceId, 1.0 / 14.0);
        r.param(Ruinae::kModMatrixSlot1DestId, 4.0 / 14.0);
        r.param(Ruinae::kModMatrixSlot1AmountId, 0.8);
    }
    if (variant == 3) {
        r.param(Ruinae::kDelayEnabledId, 1.0);
        r.param(Ruinae::kDelayTypeId, 0.0);
        r.param(Ruinae::kDelayMixId, 0.4);
        r.param(Ruinae::kReverbEnabledId, 1.0);
        r.param(Ruinae::kReverbTypeId, 0.0);
    }
    if (variant == 4) {
        r.param(Ruinae::kVoiceModeId, 1.0);
        r.param(Ruinae::kMonoLegatoId, 1.0);
        r.param(Ruinae::kMonoPortamentoTimeId, std::cbrt(150.0 / 5000.0));
    }
    r.render(r.at(2.0), bs);
    return r.mono();
}

}  // namespace RuinaeAuditRig
