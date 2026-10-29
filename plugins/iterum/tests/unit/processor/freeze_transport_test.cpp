// ==============================================================================
// Freeze processor: the song position reaches the DSP, the Freeze capture state tail, a restored Hold, the
// infinite tail, and the Harmonic Drones CPU budget
// ==============================================================================
// specs/iterum-freeze/design.md 4.1 (the processor fills projectTimeMusic / isPlaying / transportPositionSamples for
// every mode), 5.1 (Hold, Capture and Dry appended at the very end of the state stream), 2.3 (a Hold restored On
// waits for the first music), 2.9 / R3 (kInfiniteTail in every mode) and R2 (row P11, the CPU budget).
//
// Every case heap-allocates the processor: it holds every mode's DSP, and a stack instance risks a stack overflow
// (Catch2 reports it as "Unknown exception").

#include "processor/processor.h"
#include "delay_mode.h"
#include "plugin_ids.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/processdata.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
#include <vector>

using Steinberg::Vst::ParamID;
using Steinberg::Vst::ParamValue;

namespace {

constexpr double kTwoPi = 6.283185307179586476925286766559;
constexpr ParamValue kFreezeModeNorm = static_cast<double>(static_cast<int>(Iterum::DelayMode::Freeze)) / 9.0;

/// A processor brought up as a host does (initialize, setupProcessing, setActive(true)), driven with audio and a
/// running transport at 120 BPM whose song position is each block's first sample.
class FreezeRig {
public:
    FreezeRig(double sampleRate, Steinberg::int32 block)
        : proc_(Steinberg::owned(new Iterum::Processor())), sampleRate_(sampleRate), block_(block) {
        REQUIRE(proc_->initialize(nullptr) == Steinberg::kResultOk);
        Steinberg::Vst::ProcessSetup setup{.processMode = Steinberg::Vst::kRealtime,
                                           .symbolicSampleSize = Steinberg::Vst::kSample32,
                                           .maxSamplesPerBlock = block_,
                                           .sampleRate = sampleRate_};
        REQUIRE(proc_->setupProcessing(setup) == Steinberg::kResultOk);
        REQUIRE(proc_->setActive(true) == Steinberg::kResultOk);
        REQUIRE(data_.prepare(*proc_, block_, Steinberg::Vst::kSample32));
        data_.numSamples = block_;
        data_.processContext = &context_;
        data_.inputParameterChanges = &input_;
        data_.outputParameterChanges = &output_;
        context_.sampleRate = sampleRate_;
        context_.tempo = 120.0;
    }

    ~FreezeRig() {
        data_.unprepare();
        proc_->setActive(static_cast<Steinberg::TBool>(false));
        proc_->terminate();
    }

    FreezeRig(const FreezeRig&) = delete;
    FreezeRig& operator=(const FreezeRig&) = delete;

    /// Queues a parameter change for the next block.
    void queue(ParamID id, ParamValue value) {
        Steinberg::int32 index = 0;
        if (auto* q = input_.addParameterData(id, index)) {
            Steinberg::int32 pointIndex = 0;
            q->addPoint(0, value, pointIndex);
        }
    }

    /// One block whose input is @p in(sample) on both channels, the transport playing with a valid song position;
    /// the left output is appended to out().
    void block(const std::function<float(size_t)>& in) {
        for (Steinberg::int32 bus = 0; bus < data_.numInputs; ++bus) {
            for (Steinberg::int32 ch = 0; ch < data_.inputs[bus].numChannels; ++ch) {
                float* buf = data_.inputs[bus].channelBuffers32[ch];
                for (Steinberg::int32 i = 0; i < block_; ++i) {
                    buf[i] = in(sample_ + static_cast<size_t>(i));
                }
            }
        }
        context_.state = Steinberg::Vst::ProcessContext::kTempoValid | Steinberg::Vst::ProcessContext::kPlaying |
                         Steinberg::Vst::ProcessContext::kProjectTimeMusicValid;
        context_.projectTimeMusic = static_cast<double>(sample_) * 2.0 / sampleRate_;  // 120 BPM
        context_.projectTimeSamples = static_cast<Steinberg::int64>(sample_);
        output_.clearQueue();
        REQUIRE(proc_->process(data_) == Steinberg::kResultOk);
        input_.clearQueue();
        const float* outL = data_.outputs[0].channelBuffers32[0];
        out_.insert(out_.end(), outL, outL + block_);
        sample_ += static_cast<size_t>(block_);
    }

    /// Blocks until at least @p samples have been processed.
    void runTo(size_t samples, const std::function<float(size_t)>& in) {
        while (sample_ < samples) {
            block(in);
        }
    }

    [[nodiscard]] size_t samplesDone() const { return sample_; }
    [[nodiscard]] const std::vector<float>& out() const { return out_; }
    Iterum::Processor& processor() { return *proc_; }

private:
    Steinberg::IPtr<Iterum::Processor> proc_;
    double sampleRate_;
    Steinberg::int32 block_;
    Steinberg::Vst::HostProcessData data_;
    Steinberg::Vst::ProcessContext context_{};
    Steinberg::Vst::ParameterChanges input_{64};
    Steinberg::Vst::ParameterChanges output_{64};
    size_t sample_ = 0;
    std::vector<float> out_;
};

[[nodiscard]] std::function<float(size_t)> tone(double hz, double sampleRate) {
    return [hz, sampleRate](size_t i) {
        return static_cast<float>(0.5 * std::sin(kTwoPi * hz * static_cast<double>(i) / sampleRate));
    };
}

[[nodiscard]] Steinberg::IPtr<Steinberg::MemoryStream> stateOf(Iterum::Processor& proc) {
    auto stream = Steinberg::owned(new Steinberg::MemoryStream());
    REQUIRE(proc.getState(stream) == Steinberg::kResultOk);
    stream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    return stream;
}

[[nodiscard]] std::vector<char> bytesOf(Steinberg::MemoryStream& stream) {
    Steinberg::int64 end = 0;
    stream.seek(0, Steinberg::IBStream::kIBSeekEnd, &end);
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    std::vector<char> bytes(static_cast<size_t>(end));
    Steinberg::int32 read = 0;
    REQUIRE(stream.read(bytes.data(), static_cast<Steinberg::int32>(bytes.size()), &read) == Steinberg::kResultOk);
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    return bytes;
}

[[nodiscard]] Steinberg::int32 int32At(const std::vector<char>& bytes, size_t offset) {
    Steinberg::int32 v = 0;
    std::memcpy(&v, bytes.data() + offset, sizeof(v));  // the state is little-endian, as is every target
    return v;
}

/// The first sample in [from, to) with |x| above @p thr, or @p to.
[[nodiscard]] size_t firstAbove(const std::vector<float>& x, size_t from, size_t to, float thr) {
    for (size_t i = from; i < std::min(to, x.size()); ++i) {
        if (std::abs(x[i]) > thr) {
            return i;
        }
    }
    return to;
}

/// Goertzel: the squared amplitude of the component at @p hz over [a, b).
[[nodiscard]] double tonePower(const std::vector<float>& x, size_t a, size_t b, double hz, double sampleRate) {
    const double w = kTwoPi * hz / sampleRate;
    const double c = 2.0 * std::cos(w);
    double s1 = 0.0;
    double s2 = 0.0;
    for (size_t i = a; i < b; ++i) {
        const double s0 = static_cast<double>(x[i]) + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const double re = s1 - s2 * std::cos(w);
    const double im = s2 * std::sin(w);
    const double n = static_cast<double>(b - a);
    return (re * re + im * im) * 4.0 / (n * n);
}

} // namespace

TEST_CASE("Freeze passes the song position: a hit lands at the step's sample", "[iterum][freeze][processor]") {
    // 48 kHz, block 64, 120 BPM: a beat is 24000 samples. Mode Freeze, Mix 1.0, Dry = Mix (the output is the wet
    // alone), Attack 0, a 440 Hz input for 3 s. Step 0 of bar 1 finds no span; the next hit, step 3 at beat 1.5
    // (sample 36000), plays the span; the first hit of bar 2 is at beat 4 (sample 96000).
    constexpr double kSr = 48000.0;
    FreezeRig rig(kSr, 64);
    rig.queue(Iterum::kModeId, kFreezeModeNorm);
    rig.queue(Iterum::kFreezeMixId, 1.0);
    rig.queue(Iterum::kFreezeDryModeId, 0.0);
    rig.queue(Iterum::kFreezeEnvelopeAttackId, 0.0);
    rig.runTo(144000, tone(440.0, kSr));
    const auto& out = rig.out();

    const size_t second = firstAbove(out, 24000, 60000, 1e-3f);
    INFO("first hit after 0.5 s at sample " << second);
    CHECK(second + 64 >= 36000);
    CHECK(second <= 36000 + 64);

    const size_t bar2 = firstAbove(out, 84000, 120000, 1e-3f);  // after the beat-3 slice (72000 + 9600) has ended
    INFO("first hit of bar 2 at sample " << bar2);
    CHECK(bar2 + 64 >= 96000);
    CHECK(bar2 <= 96000 + 64);
}

TEST_CASE("Freeze state tail round-trips through getState / setState", "[iterum][freeze][processor]") {
    FreezeRig source(48000.0, 64);
    source.queue(Iterum::kFreezeHoldId, 1.0);
    source.queue(Iterum::kFreezeCaptureLengthId, 1.0 / 3.0);
    source.queue(Iterum::kFreezeDryModeId, 0.0);
    source.block([](size_t) { return 0.0f; });
    auto saved = stateOf(source.processor());
    const auto savedBytes = bytesOf(*saved);

    FreezeRig target(48000.0, 64);
    REQUIRE(target.processor().setState(saved) == Steinberg::kResultOk);
    auto resaved = stateOf(target.processor());
    const auto resavedBytes = bytesOf(*resaved);

    REQUIRE(resavedBytes.size() == savedBytes.size());
    CHECK(resavedBytes == savedBytes);
    REQUIRE(savedBytes.size() >= 12);
    const size_t tail = savedBytes.size() - 12;
    CHECK(int32At(savedBytes, tail) == 1);      // Hold On
    CHECK(int32At(savedBytes, tail + 4) == 1);  // Capture 1/2
    CHECK(int32At(savedBytes, tail + 8) == 0);  // Dry = Mix
}

TEST_CASE("a project restored with Hold On holds the first music", "[iterum][freeze][processor]") {
    // A Freeze state saved with Hold On; after setState: 2 s of silence, then 330 Hz, playing from beat 0; from
    // beat 8 (4 s) a 440 Hz input. Hold waits for the first capture of music (beat 8) and then holds the 330 Hz bar.
    constexpr double kSr = 48000.0;
    FreezeRig source(kSr, 64);
    source.queue(Iterum::kModeId, kFreezeModeNorm);
    source.queue(Iterum::kFreezeHoldId, 1.0);
    source.block([](size_t) { return 0.0f; });
    auto saved = stateOf(source.processor());

    FreezeRig rig(kSr, 64);
    REQUIRE(rig.processor().setState(saved) == Steinberg::kResultOk);
    auto in330 = tone(330.0, kSr);
    auto in440 = tone(440.0, kSr);
    rig.runTo(288000, [&](size_t i) {
        if (i < 96000) {
            return 0.0f;
        }
        return i < 192000 ? in330(i) : in440(i);
    });
    const auto& out = rig.out();

    double silent = 0.0;
    for (size_t i = 0; i < 96000; ++i) {
        silent = std::max(silent, static_cast<double>(std::abs(out[i])));
    }
    INFO("peak output over the silent 2 s: " << silent);
    CHECK(silent < 1e-5);

    const double hits330 = tonePower(out, 96000, 192000, 330.0, kSr);
    INFO("330 Hz power over [2, 4 s): " << hits330);
    CHECK(hits330 > 1e-3);

    const double p330 = tonePower(out, 216000, 288000, 330.0, kSr);
    const double p440 = tonePower(out, 216000, 288000, 440.0, kSr);
    INFO("over [4.5, 6 s): 330 Hz " << p330 << ", 440 Hz " << p440);
    CHECK(p330 > 10.0 * p440);
}

TEST_CASE("Iterum reports an infinite tail in every mode", "[iterum][freeze][processor]") {
    FreezeRig rig(48000.0, 64);
    for (int mode = 0; mode < 10; ++mode) {
        rig.queue(Iterum::kModeId, static_cast<double>(mode) / 9.0);
        rig.block([](size_t) { return 0.0f; });
        INFO("mode " << mode);
        CHECK(rig.processor().getTailSamples() == Steinberg::Vst::kInfiniteTail);
    }
}

TEST_CASE("Freeze Harmonic Drones at 4 voices x Octave stays inside the plugin budget",
          "[iterum][freeze][processor][performance]") {
    // Row P11 (R2): 44.1 kHz, block 512, Harmonic Drones, Voices 4, Octave, Drift 30 %, a 233 Hz input held as the
    // window; 30 s of audio after a 5 s warm-up must take under 5 % of the audio time (the full-plugin budget).
    // Runs only through `node tools/run-cpu-tests.js plugin_tests`, alone.
    constexpr double kSr = 44100.0;
    FreezeRig rig(kSr, 512);
    rig.queue(Iterum::kModeId, kFreezeModeNorm);
    rig.queue(Iterum::kFreezePatternTypeId, 2.0 / 3.0);  // Harmonic Drones
    rig.queue(Iterum::kFreezeDroneVoiceCountId, 1.0);    // 4 voices
    rig.queue(Iterum::kFreezeDroneIntervalId, 1.0);      // Octave
    rig.queue(Iterum::kFreezeDroneDriftId, 0.3);
    rig.queue(Iterum::kFreezeHoldId, 1.0);
    const auto in = tone(233.0, kSr);
    rig.runTo(static_cast<size_t>(5.0 * kSr), in);
    const auto start = std::chrono::steady_clock::now();
    rig.runTo(static_cast<size_t>(35.0 * kSr), in);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    INFO("30 s of audio took " << seconds << " s (" << 100.0 * seconds / 30.0 << " % of real time)");
    CHECK(seconds < 0.05 * 30.0);
}
