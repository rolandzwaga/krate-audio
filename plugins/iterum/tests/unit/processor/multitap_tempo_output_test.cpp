// ==============================================================================
// MultiTap processor: the Tempo output parameter, the Custom Length state tail and the morph into Custom
// ==============================================================================
// specs/iterum-tap-editor/design.md 2.4 (tempo reaches the controller as the hidden read-only output parameter 915),
// 2.5 (the custom values reach the DSP every block, before the pattern-change detection) and 7.1 (the Custom Length
// field appended at the very end of the state stream).
//
// Every case heap-allocates the processor: it holds every mode's DSP, and a stack instance risks a stack overflow
// (Catch2 reports it as "Unknown exception").

#include "processor/processor.h"
#include "delay_mode.h"
#include "plugin_ids.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/processdata.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <vector>

using Catch::Approx;
using Steinberg::Vst::ParamID;
using Steinberg::Vst::ParamValue;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr Steinberg::int32 kBlock = 64;

/// A processor brought up as a host does: initialize, setupProcessing (48 kHz, 64-sample blocks), setActive(true).
class ProcessorRig {
public:
    ProcessorRig() : proc_(Steinberg::owned(new Iterum::Processor())) {
        REQUIRE(proc_->initialize(nullptr) == Steinberg::kResultOk);
        Steinberg::Vst::ProcessSetup setup{Steinberg::Vst::kRealtime, Steinberg::Vst::kSample32, kBlock, kSampleRate};
        REQUIRE(proc_->setupProcessing(setup) == Steinberg::kResultOk);
        REQUIRE(proc_->setActive(true) == Steinberg::kResultOk);
        REQUIRE(data_.prepare(*proc_, kBlock, Steinberg::Vst::kSample32));
        data_.numSamples = kBlock;
        data_.processContext = &context_;
        data_.inputParameterChanges = &input_;
        data_.outputParameterChanges = &output_;
        context_.sampleRate = kSampleRate;
        setTempo(120.0);
    }

    ~ProcessorRig() {
        data_.unprepare();
        proc_->setActive(false);
        proc_->terminate();
    }

    ProcessorRig(const ProcessorRig&) = delete;
    ProcessorRig& operator=(const ProcessorRig&) = delete;

    void setTempo(double bpm) {
        context_.tempo = bpm;
        context_.state = Steinberg::Vst::ProcessContext::kTempoValid | Steinberg::Vst::ProcessContext::kPlaying;
    }

    /// Queues a parameter change for the next block.
    void queue(ParamID id, ParamValue value) {
        Steinberg::int32 index = 0;
        if (auto* q = input_.addParameterData(id, index)) {
            Steinberg::int32 pointIndex = 0;
            q->addPoint(0, value, pointIndex);
        }
    }

    /// One block of silence; the output changes it wrote stay readable until the next block.
    void block() {
        for (Steinberg::int32 bus = 0; bus < data_.numInputs; ++bus) {
            for (Steinberg::int32 ch = 0; ch < data_.inputs[bus].numChannels; ++ch) {
                std::memset(data_.inputs[bus].channelBuffers32[ch], 0, sizeof(float) * kBlock);
            }
        }
        output_.clearQueue();
        REQUIRE(proc_->process(data_) == Steinberg::kResultOk);
        input_.clearQueue();
    }

    /// The points the last block wrote for @p id.
    [[nodiscard]] std::vector<ParamValue> outputPoints(ParamID id) {
        std::vector<ParamValue> points;
        for (Steinberg::int32 i = 0; i < output_.getParameterCount(); ++i) {
            auto* q = output_.getParameterData(i);
            if (q == nullptr || q->getParameterId() != id) continue;
            for (Steinberg::int32 p = 0; p < q->getPointCount(); ++p) {
                Steinberg::int32 offset = 0;
                ParamValue v = 0.0;
                if (q->getPoint(p, offset, v) == Steinberg::kResultOk) points.push_back(v);
            }
        }
        return points;
    }

    Iterum::Processor& processor() { return *proc_; }

private:
    Steinberg::IPtr<Iterum::Processor> proc_;
    Steinberg::Vst::HostProcessData data_;
    Steinberg::Vst::ProcessContext context_{};
    Steinberg::Vst::ParameterChanges input_{64};
    Steinberg::Vst::ParameterChanges output_{64};
};

[[nodiscard]] double tempoNorm(double bpm) { return (bpm - 20.0) / 280.0; }

[[nodiscard]] Steinberg::IPtr<Steinberg::MemoryStream> stateOf(Iterum::Processor& proc) {
    auto stream = Steinberg::owned(new Steinberg::MemoryStream());
    REQUIRE(proc.getState(stream) == Steinberg::kResultOk);
    stream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    return stream;
}

[[nodiscard]] Steinberg::int64 sizeOf(Steinberg::MemoryStream& stream) {
    Steinberg::int64 end = 0;
    stream.seek(0, Steinberg::IBStream::kIBSeekEnd, &end);
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    return end;
}

[[nodiscard]] Steinberg::int32 lastInt32(Steinberg::MemoryStream& stream) {
    const Steinberg::int64 end = sizeOf(stream);
    stream.seek(end - 4, Steinberg::IBStream::kIBSeekSet, nullptr);
    Steinberg::IBStreamer s(&stream, kLittleEndian);
    Steinberg::int32 v = -1;
    REQUIRE(s.readInt32(v));
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    return v;
}

constexpr ParamValue kMultiTapModeNorm = static_cast<double>(static_cast<int>(Iterum::DelayMode::MultiTap)) / 9.0;

} // namespace

TEST_CASE("writes MultiTap Tempo on the first block and again only when the tempo moves 0.05 BPM",
          "[iterum][multitap][processor]") {
    ProcessorRig rig;

    rig.setTempo(120.0);
    rig.block();
    const auto first = rig.outputPoints(Iterum::kMultiTapTempoId);
    REQUIRE(first.size() == 1u);
    REQUIRE(first.front() == Approx(tempoNorm(120.0)).margin(1e-9));

    // 0.02 BPM is below the threshold: nothing, however long it lasts.
    rig.setTempo(120.02);
    for (int b = 0; b < 60; ++b) {
        rig.block();
        INFO("block " << b << " at 120.02 BPM");
        REQUIRE(rig.outputPoints(Iterum::kMultiTapTempoId).empty());
    }

    // More than 50 ms have passed: a 1.5 BPM move is sent at once.
    rig.setTempo(121.5);
    rig.block();
    const auto moved = rig.outputPoints(Iterum::kMultiTapTempoId);
    REQUIRE(moved.size() == 1u);
    REQUIRE(moved.front() == Approx(tempoNorm(121.5)).margin(1e-9));

    SECTION("in every mode, not only MultiTap") {
        rig.queue(Iterum::kModeId, 0.0);  // Granular
        rig.setTempo(140.0);
        // The 140 waits out the 50 ms after the 121.5 point (38 blocks), then the 90 waits out the 50 ms after the
        // 140 point: 80 blocks (107 ms) clear both spacings (design 2.4: at least 0.05 s since the last send).
        for (int b = 0; b < 80; ++b) rig.block();
        rig.setTempo(90.0);
        rig.block();
        const auto granular = rig.outputPoints(Iterum::kMultiTapTempoId);
        REQUIRE(granular.size() == 1u);
        REQUIRE(granular.front() == Approx(tempoNorm(90.0)).margin(1e-9));
    }
}

TEST_CASE("sends at most one tempo point per 50 ms", "[iterum][multitap][processor]") {
    // A ramp of 1 BPM per block over 100 blocks of 64 samples at 48 kHz (133 ms): sends at 0, >= 50 and >= 100 ms.
    ProcessorRig rig;
    size_t points = 0;
    for (int b = 0; b < 100; ++b) {
        rig.setTempo(100.0 + static_cast<double>(b));
        rig.block();
        points += rig.outputPoints(Iterum::kMultiTapTempoId).size();
    }
    REQUIRE(points >= 2u);
    REQUIRE(points <= 3u);
}

TEST_CASE("state round-trip keeps Custom Length and a pre-tail stream loads Free", "[iterum][multitap][processor]") {
    SECTION("a fresh processor saves 1/1 as the last field") {
        ProcessorRig rig;
        auto state = stateOf(rig.processor());
        REQUIRE(lastInt32(*state) == 4);
    }

    SECTION("Length 2/1 survives getState -> setState -> getState") {
        ProcessorRig source;
        source.queue(Iterum::kMultiTapCustomLengthId, 1.0);
        source.block();
        auto saved = stateOf(source.processor());
        REQUIRE(lastInt32(*saved) == 5);

        ProcessorRig target;
        REQUIRE(target.processor().setState(saved) == Steinberg::kResultOk);
        auto resaved = stateOf(target.processor());
        REQUIRE(lastInt32(*resaved) == 5);
        REQUIRE(sizeOf(*resaved) == sizeOf(*saved));
    }

    SECTION("a stream truncated after the Tape sync fields loads Free") {
        ProcessorRig source;
        source.queue(Iterum::kMultiTapCustomLengthId, 1.0);
        source.block();
        auto saved = stateOf(source.processor());
        const Steinberg::int64 size = sizeOf(*saved);

        // The same bytes without the last int32: what every build before this change wrote.
        std::vector<char> bytes(static_cast<size_t>(size - 4));
        Steinberg::int32 read = 0;
        REQUIRE(saved->read(bytes.data(), static_cast<Steinberg::int32>(bytes.size()), &read) == Steinberg::kResultOk);
        auto truncated = Steinberg::owned(new Steinberg::MemoryStream());
        Steinberg::int32 written = 0;
        truncated->write(bytes.data(), static_cast<Steinberg::int32>(bytes.size()), &written);
        truncated->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);

        ProcessorRig target;  // starts at 1/1
        REQUIRE(target.processor().setState(truncated) == Steinberg::kResultOk);
        auto resaved = stateOf(target.processor());
        REQUIRE(lastInt32(*resaved) == 0);
    }
}

TEST_CASE("switching Timing Pattern to Custom at the same tap count glides toward ratio x span",
          "[iterum][multitap][processor]") {
    ProcessorRig rig;
    rig.setTempo(120.0);

    // MultiTap, Quarter, 4 taps, Morph Time at its 50 ms minimum; custom ratios 0.1 .. 0.4 at Length 1/1.
    rig.queue(Iterum::kModeId, kMultiTapModeNorm);
    rig.queue(Iterum::kMultiTapTimingPatternId, 2.0 / 19.0);
    rig.queue(Iterum::kMultiTapTapCountId, 2.0 / 14.0);
    rig.queue(Iterum::kMultiTapMorphTimeId, 0.0);
    rig.queue(Iterum::kMultiTapCustomLengthId, 0.8);
    for (int i = 0; i < 4; ++i) {
        rig.queue(Iterum::kMultiTapCustomTime0Id + static_cast<ParamID>(i), 0.1 * static_cast<double>(i + 1));
    }
    for (int b = 0; b < 10; ++b) rig.block();

    const auto& delay = rig.processor().multiTapDelayForTest();
    float start[4] = {};
    for (size_t i = 0; i < 4; ++i) {
        start[i] = delay.getTapTimeMs(i);
        INFO("tap " << i);
        REQUIRE(start[i] == Approx(500.0f * static_cast<float>(i + 1)).margin(0.5));  // Quarter at 120 BPM
    }
    const float target[4] = {200.0f, 400.0f, 600.0f, 800.0f};  // ratio x 2000 ms

    rig.queue(Iterum::kMultiTapTimingPatternId, 1.0);  // Custom
    for (int b = 0; b < 200; ++b) {
        rig.block();
        for (size_t i = 0; i < 4; ++i) {
            const float t = delay.getTapTimeMs(i);
            INFO("block " << b << " tap " << i << " at " << t << " ms");
            REQUIRE(t >= 1.0f);
            REQUIRE(t >= target[i] - 0.5f);
            REQUIRE(t <= start[i] + 0.5f);
        }
    }
    for (size_t i = 0; i < 4; ++i) {
        INFO("tap " << i);
        REQUIRE(delay.getTapTimeMs(i) == Approx(target[i]).margin(0.5));
    }
}
