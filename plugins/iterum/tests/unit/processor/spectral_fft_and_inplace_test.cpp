// ==============================================================================
// Processor: Spectral FFT Size takes effect while running; a mode switch is the same with in-place buffers
// ==============================================================================
// FFT Size: the processor passed the value to SpectralDelay::setFFTSize, which only stores it for the next prepare();
// prepare ran in setupProcessing alone, so a new FFT Size did nothing until the host re-prepared the plugin (2026-11).
// In place: during the 50 ms mode crossfade the processor ran the new mode into the output buffers and then the old
// mode from the input buffers; a host processing in place (input == output) gave the old mode the new mode's output as
// its input (2026-11).
//
// Every case heap-allocates the processor: it holds every mode's DSP, and a stack instance risks a stack overflow.

#include "processor/processor.h"
#include "delay_mode.h"
#include "plugin_ids.h"

#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/processdata.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <vector>

using Steinberg::Vst::ParamID;
using Steinberg::Vst::ParamValue;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr Steinberg::int32 kBlock = 64;

/// A processor brought up as a host does (48 kHz, 64-sample blocks), optionally processing in place.
class Rig {
public:
    explicit Rig(bool inPlace) : proc_(Steinberg::owned(new Iterum::Processor())) {
        REQUIRE(proc_->initialize(nullptr) == Steinberg::kResultOk);
        Steinberg::Vst::ProcessSetup setup{.processMode = Steinberg::Vst::kRealtime,
                                           .symbolicSampleSize = Steinberg::Vst::kSample32,
                                           .maxSamplesPerBlock = kBlock,
                                           .sampleRate = kSampleRate};
        REQUIRE(proc_->setupProcessing(setup) == Steinberg::kResultOk);
        REQUIRE(proc_->setActive(true) == Steinberg::kResultOk);
        REQUIRE(data_.prepare(*proc_, kBlock, Steinberg::Vst::kSample32));
        data_.numSamples = kBlock;
        data_.processContext = &context_;
        data_.inputParameterChanges = &input_;
        context_.sampleRate = kSampleRate;
        context_.tempo = 120.0;
        context_.state = Steinberg::Vst::ProcessContext::kTempoValid | Steinberg::Vst::ProcessContext::kPlaying;
        if (inPlace) {
            for (Steinberg::int32 ch = 0; ch < 2; ++ch) {
                ownOutputs_[ch] = data_.outputs[0].channelBuffers32[ch];
                data_.outputs[0].channelBuffers32[ch] = data_.inputs[0].channelBuffers32[ch];
            }
        }
    }

    ~Rig() {
        if (ownOutputs_[0] != nullptr) {
            for (Steinberg::int32 ch = 0; ch < 2; ++ch) {
                data_.outputs[0].channelBuffers32[ch] = ownOutputs_[ch];
            }
        }
        data_.unprepare();
        proc_->setActive(static_cast<Steinberg::TBool>(false));
        proc_->terminate();
    }

    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;

    void queue(ParamID id, ParamValue value) {
        Steinberg::int32 index = 0;
        if (auto* q = input_.addParameterData(id, index)) {
            Steinberg::int32 pointIndex = 0;
            q->addPoint(0, value, pointIndex);
        }
    }

    /// One block whose input sample n (n counted from the first block) is @p signal(n); appends the left output.
    void block(const std::function<float(size_t)>& signal, std::vector<float>& outL) {
        for (Steinberg::int32 ch = 0; ch < 2; ++ch) {
            float* in = data_.inputs[0].channelBuffers32[ch];
            for (Steinberg::int32 i = 0; i < kBlock; ++i) {
                in[i] = signal(position_ + static_cast<size_t>(i)) * (ch == 0 ? 1.0f : 0.8f);
            }
        }
        REQUIRE(proc_->process(data_) == Steinberg::kResultOk);
        input_.clearQueue();
        const float* out = data_.outputs[0].channelBuffers32[0];
        outL.insert(outL.end(), out, out + kBlock);
        position_ += kBlock;
    }

private:
    Steinberg::IPtr<Iterum::Processor> proc_;
    Steinberg::Vst::HostProcessData data_;
    Steinberg::Vst::ProcessContext context_{};
    Steinberg::Vst::ParameterChanges input_{64};
    float* ownOutputs_[2] = {nullptr, nullptr};
    size_t position_ = 0;
};

[[nodiscard]] ParamValue modeNorm(Iterum::DelayMode mode) {
    return static_cast<double>(static_cast<int>(mode)) / (static_cast<int>(Iterum::DelayMode::NumModes) - 1);
}

/// The sample, counted from the impulse, where Spectral's wet output first exceeds 1e-3, at FFT Size @p sizeNorm
/// (0 = 512 ... 1 = 4096) set while running: Base Delay 0, Spread 0, Feedback 0, Mix 100 %.
[[nodiscard]] size_t spectralOnset(ParamValue sizeNorm) {
    Rig rig(false);
    std::vector<float> out;
    const auto silence = [](size_t) { return 0.0f; };
    rig.queue(Iterum::kModeId, modeNorm(Iterum::DelayMode::Spectral));
    rig.queue(Iterum::kSpectralTimeModeId, 0.0);
    rig.queue(Iterum::kSpectralBaseDelayId, 0.0);
    rig.queue(Iterum::kSpectralSpreadId, 0.0);
    rig.queue(Iterum::kSpectralFeedbackId, 0.0);
    rig.queue(Iterum::kSpectralMixId, 1.0);
    rig.block(silence, out);
    rig.queue(Iterum::kSpectralFFTSizeId, sizeNorm);
    for (int b = 0; b < 300; ++b) {  // 400 ms: the mode and FFT-size crossfades are long over
        rig.block(silence, out);
    }
    const size_t impulseAt = out.size();
    for (int b = 0; b < 300; ++b) {
        rig.block([impulseAt](size_t n) { return n == impulseAt ? 1.0f : 0.0f; }, out);
    }
    for (size_t i = impulseAt; i < out.size(); ++i) {
        if (std::abs(out[i]) > 1e-3f) {
            return i - impulseAt;
        }
    }
    return SIZE_MAX;
}

}  // namespace

TEST_CASE("Spectral FFT Size changes the analysis while the plugin runs", "[iterum][spectral][processor]") {
    const size_t small = spectralOnset(0.0);  // 512
    const size_t large = spectralOnset(1.0);  // 4096
    INFO("onset at 512: " << small << " samples, at 4096: " << large << " samples");
    REQUIRE(small != SIZE_MAX);
    REQUIRE(large != SIZE_MAX);
    // The STFT latency grows with the frame: 3584 samples more at 4096 than at 512 (half that with 50 % overlap)
    CHECK(large > small + 1500);
}

TEST_CASE("A mode switch sounds the same when the host processes in place", "[iterum][processor][crossfade]") {
    const auto render = [](bool inPlace) {
        Rig rig(inPlace);
        std::vector<float> out;
        const auto sine = [](size_t n) { return 0.4f * std::sin(0.031f * static_cast<float>(n)); };
        rig.queue(Iterum::kModeId, modeNorm(Iterum::DelayMode::Digital));
        for (int b = 0; b < 100; ++b) {
            rig.block(sine, out);
        }
        rig.queue(Iterum::kModeId, modeNorm(Iterum::DelayMode::PingPong));  // 50 ms = 37.5 blocks of crossfade
        for (int b = 0; b < 100; ++b) {
            rig.block(sine, out);
        }
        return out;
    };
    const std::vector<float> separate = render(false);
    const std::vector<float> inPlace = render(true);
    REQUIRE(separate.size() == inPlace.size());
    float worst = 0.0f;
    for (size_t i = 0; i < separate.size(); ++i) {
        worst = std::max(worst, std::abs(separate[i] - inPlace[i]));
    }
    INFO("largest in-place difference " << worst);
    CHECK(worst < 1e-6f);
}
