// ==============================================================================
// Freeze Mode Parameters Unit Tests
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>

using Catch::Approx;

namespace {

// Freeze Enabled: boolean
bool denormFreezeEnabled(double normalized) {
    return normalized >= 0.5;
}

// Delay Time: 10-5000ms
float denormDelayTime(double normalized) {
    return static_cast<float>(10.0 + normalized * 4990.0);
}

double normDelayTime(float ms) {
    return static_cast<double>((ms - 10.0f) / 4990.0f);
}

// Feedback: 0-1.2
float denormFeedback(double normalized) {
    return static_cast<float>(normalized * 1.2);
}

// Pitch Semitones: -24 to +24
float denormPitchSemitones(double normalized) {
    return static_cast<float>(-24.0 + normalized * 48.0);
}

double normPitchSemitones(float semitones) {
    return static_cast<double>((semitones + 24.0f) / 48.0f);
}

// Pitch Cents: -100 to +100
float denormPitchCents(double normalized) {
    return static_cast<float>(-100.0 + normalized * 200.0);
}

// Passthrough params: 0-1
float denormPassthrough(double normalized) {
    return static_cast<float>(normalized);
}

// Filter Type: 0-2 discrete
int denormFilterType(double normalized) {
    return static_cast<int>(normalized * 2.0 + 0.5);
}

// Filter Cutoff: 20-20000Hz (logarithmic)
float denormFilterCutoff(double normalized) {
    return static_cast<float>(20.0 * std::pow(1000.0, normalized));
}

double normFilterCutoff(float hz) {
    return std::log(hz / 20.0f) / std::log(1000.0f);
}

} // anonymous namespace

TEST_CASE("Freeze Enabled normalization", "[params][freeze]") {
    SECTION("normalized 0.0 -> false") {
        REQUIRE(denormFreezeEnabled(0.0) == false);
    }
    SECTION("normalized 0.49 -> false") {
        REQUIRE(denormFreezeEnabled(0.49) == false);
    }
    SECTION("normalized 0.5 -> true") {
        REQUIRE(denormFreezeEnabled(0.5) == true);
    }
    SECTION("normalized 1.0 -> true") {
        REQUIRE(denormFreezeEnabled(1.0) == true);
    }
}

TEST_CASE("Freeze Delay Time normalization", "[params][freeze]") {
    SECTION("normalized 0.0 -> 10ms") {
        REQUIRE(denormDelayTime(0.0) == Approx(10.0f));
    }
    SECTION("normalized 1.0 -> 5000ms") {
        REQUIRE(denormDelayTime(1.0) == Approx(5000.0f));
    }
    SECTION("round-trip: 500ms (default)") {
        float original = 500.0f;
        double normalized = normDelayTime(original);
        float result = denormDelayTime(normalized);
        REQUIRE(result == Approx(original).margin(0.1f));
    }
}

TEST_CASE("Freeze Pitch Semitones normalization", "[params][freeze]") {
    SECTION("normalized 0.0 -> -24 semitones") {
        REQUIRE(denormPitchSemitones(0.0) == Approx(-24.0f));
    }
    SECTION("normalized 0.5 -> 0 semitones (default)") {
        REQUIRE(denormPitchSemitones(0.5) == Approx(0.0f));
    }
    SECTION("normalized 1.0 -> +24 semitones") {
        REQUIRE(denormPitchSemitones(1.0) == Approx(24.0f));
    }
    SECTION("round-trip: 0 semitones") {
        float original = 0.0f;
        double normalized = normPitchSemitones(original);
        float result = denormPitchSemitones(normalized);
        REQUIRE(result == Approx(original).margin(0.01f));
    }
}

TEST_CASE("Freeze Filter Cutoff normalization (logarithmic)", "[params][freeze]") {
    SECTION("normalized 0.0 -> 20Hz") {
        REQUIRE(denormFilterCutoff(0.0) == Approx(20.0f));
    }
    SECTION("normalized 1.0 -> 20000Hz") {
        REQUIRE(denormFilterCutoff(1.0) == Approx(20000.0f));
    }
    SECTION("round-trip: 1000Hz (default)") {
        float original = 1000.0f;
        double normalized = normFilterCutoff(original);
        float result = denormFilterCutoff(normalized);
        REQUIRE(result == Approx(original).margin(1.0f));
    }
}

TEST_CASE("Freeze Filter Type normalization", "[params][freeze]") {
    SECTION("normalized 0.0 -> LowPass (0)") {
        REQUIRE(denormFilterType(0.0) == 0);
    }
    SECTION("normalized 0.5 -> HighPass (1)") {
        REQUIRE(denormFilterType(0.5) == 1);
    }
    SECTION("normalized 1.0 -> BandPass (2)") {
        REQUIRE(denormFilterType(1.0) == 2);
    }
}

TEST_CASE("Freeze passthrough parameters", "[params][freeze]") {
    SECTION("Shimmer Mix is 0-1 passthrough") {
        REQUIRE(denormPassthrough(0.0) == Approx(0.0f));
        REQUIRE(denormPassthrough(0.5) == Approx(0.5f));
        REQUIRE(denormPassthrough(1.0) == Approx(1.0f));
    }
    SECTION("Decay is 0-1 passthrough") {
        REQUIRE(denormPassthrough(0.0) == Approx(0.0f));
        REQUIRE(denormPassthrough(0.5) == Approx(0.5f));
        REQUIRE(denormPassthrough(1.0) == Approx(1.0f));
    }
    SECTION("Diffusion Amount is 0-1 passthrough") {
        REQUIRE(denormPassthrough(0.0) == Approx(0.0f));
        REQUIRE(denormPassthrough(0.3) == Approx(0.3f));
        REQUIRE(denormPassthrough(1.0) == Approx(1.0f));
    }
    SECTION("Dry/Wet is 0-1 passthrough") {
        REQUIRE(denormPassthrough(0.0) == Approx(0.0f));
        REQUIRE(denormPassthrough(0.5) == Approx(0.5f));
        REQUIRE(denormPassthrough(1.0) == Approx(1.0f));
    }
}

// ==============================================================================
// Freeze capture parameters: Hold 1070, Capture 1071, Dry 1072 and their state tail
// (specs/iterum-freeze/design.md 3.1, 5.1; rows P1, P2, P3, P5)
// ==============================================================================

#include "parameters/freeze_params.h"
#include "parameters/multitap_params.h"
#include "parameters/tape_params.h"
#include "base/source/fstreamer.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/vstparameters.h"
#include <map>
#include <string>
#include <utility>

namespace {

std::string freezeAsciiOf(const Steinberg::Vst::String128 s) {
    std::string out;
    for (int i = 0; i < 128 && s[i] != 0; ++i) out += s[i] < 128 ? static_cast<char>(s[i]) : '?';
    return out;
}

std::string formatFreeze(Steinberg::Vst::ParamID id, double value, double stepsNorm) {
    Steinberg::Vst::String128 s = {};
    REQUIRE(Iterum::formatFreezeParam(id, value, s, stepsNorm) == Steinberg::kResultOk);
    return freezeAsciiOf(s);
}

/// A component-state tail as Processor::getState ends it after the redesign: the Freeze block, the Tape sync
/// fields, the MultiTap Length field and, when @p withCapture, the three int32 of the Freeze capture tail.
Steinberg::IPtr<Steinberg::MemoryStream> makeFreezeTail(const Iterum::FreezeParams& fp, bool withCapture) {
    auto stream = Steinberg::owned(new Steinberg::MemoryStream());
    Steinberg::IBStreamer s(stream, kLittleEndian);
    Iterum::saveFreezeParams(fp, s);
    Iterum::TapeParams tape;
    Iterum::saveTapeSyncParams(tape, s);
    Iterum::MultiTapParams mt;
    Iterum::saveMultiTapLengthParams(mt, s);
    if (withCapture) {
        Iterum::saveFreezeCaptureParams(fp, s);
    }
    stream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    return stream;
}

/// Reads a makeFreezeTail stream up to the capture tail as Processor::setState does, then the tail.
void loadFreezeTail(Iterum::FreezeParams& fp, Steinberg::IBStreamer& s) {
    Iterum::loadFreezeParams(fp, s);
    Iterum::TapeParams tape;
    Iterum::loadTapeSyncParams(tape, s);
    Iterum::MultiTapParams mt;
    Iterum::loadMultiTapLengthParams(mt, s);
    Iterum::loadFreezeCaptureParams(fp, s);
}

/// Skips a makeFreezeTail stream up to the capture tail as Controller::setComponentState does.
void skipToFreezeCaptureTail(Steinberg::IBStreamer& s) {
    Iterum::loadFreezeParamsToController(s, [](Steinberg::Vst::ParamID, double) {});
    Iterum::loadTapeSyncParamsToController(s, [](Steinberg::Vst::ParamID, double) {});
    Iterum::loadMultiTapLengthParamsToController(s, [](Steinberg::Vst::ParamID, double) {});
}

/// An EditControllerEx1 holding the Freeze parameters, for the sync wrapper.
class FreezeOnlyController : public Steinberg::Vst::EditControllerEx1 {
public:
    FreezeOnlyController() { Iterum::registerFreezeParams(parameters); }
};

} // namespace

TEST_CASE("Freeze capture parameters are registered as design 3.1 lists them", "[params][freeze]") {
    using namespace Iterum;
    using namespace Steinberg::Vst;
    ParameterContainer container;
    container.init();
    registerFreezeParams(container);

    auto* hold = container.getParameter(kFreezeHoldId);
    REQUIRE(hold != nullptr);
    CHECK(hold->getInfo().stepCount == 1);
    CHECK(hold->getInfo().defaultNormalizedValue == 0.0);
    CHECK((hold->getInfo().flags & ParameterInfo::kCanAutomate) != 0);

    auto* capture = container.getParameter(kFreezeCaptureLengthId);
    REQUIRE(capture != nullptr);
    CHECK(capture->getInfo().stepCount == 3);
    CHECK(capture->getInfo().defaultNormalizedValue == Approx(2.0 / 3.0).margin(1e-9));
    CHECK((capture->getInfo().flags & ParameterInfo::kIsList) != 0);
    String128 s = {};
    capture->toString(2.0 / 3.0, s);
    CHECK(freezeAsciiOf(s) == "1/1");

    auto* dry = container.getParameter(kFreezeDryModeId);
    REQUIRE(dry != nullptr);
    CHECK(dry->getInfo().stepCount == 1);
    CHECK(dry->getInfo().defaultNormalizedValue == 1.0);
    CHECK((dry->getInfo().flags & ParameterInfo::kIsList) != 0);
    dry->toString(0.0, s);
    CHECK(freezeAsciiOf(s) == "Mix");
    dry->toString(1.0, s);
    CHECK(freezeAsciiOf(s) == "Insert");

    auto* mix = container.getParameter(kFreezeMixId);
    REQUIRE(mix != nullptr);
    CHECK(mix->getInfo().defaultNormalizedValue == 1.0);
}

TEST_CASE("Freeze capture parameters denormalise", "[params][freeze]") {
    using namespace Iterum;

    SECTION("Hold: v >= 0.5 is On") {
        const std::pair<double, int> cases[] = {{0.0, 0}, {0.49, 0}, {0.5, 1}, {1.0, 1}};
        for (const auto& [v, expected] : cases) {
            FreezeParams p;
            handleFreezeParamChange(p, kFreezeHoldId, v);
            INFO("normalized " << v);
            CHECK(p.hold.load() == expected);
        }
    }

    SECTION("Dry: v >= 0.5 is Insert") {
        const std::pair<double, int> cases[] = {{0.0, 0}, {0.49, 0}, {0.5, 1}, {1.0, 1}};
        for (const auto& [v, expected] : cases) {
            FreezeParams p;
            handleFreezeParamChange(p, kFreezeDryModeId, v);
            INFO("normalized " << v);
            CHECK(p.dryMode.load() == expected);
        }
    }

    SECTION("Capture: int(v x 3 + 0.5)") {
        const std::pair<double, int> cases[] = {{0.0, 0}, {0.34, 1}, {0.67, 2}, {1.0, 3}};
        for (const auto& [v, expected] : cases) {
            FreezeParams p;
            handleFreezeParamChange(p, kFreezeCaptureLengthId, v);
            INFO("normalized " << v);
            CHECK(p.captureLength.load() == expected);
        }
    }

    SECTION("fresh FreezeParams hold the fresh defaults: Hold off, 1/1, Insert, Mix 100 %") {
        const FreezeParams p;
        CHECK(p.hold.load() == 0);
        CHECK(p.captureLength.load() == 2);
        CHECK(p.dryMode.load() == 1);
        CHECK(p.dryWet.load() == 1.0f);
    }

    SECTION("Hold prints On / Off") {
        CHECK(formatFreeze(kFreezeHoldId, 1.0, 6.0 / 30.0) == "On");
        CHECK(formatFreeze(kFreezeHoldId, 0.0, 6.0 / 30.0) == "Off");
    }
}

TEST_CASE("Steps change recomputes Hits and Rotation from their normalized values", "[params][freeze]") {
    using namespace Iterum;
    FreezeParams p;
    handleFreezeParamChange(p, kFreezeEuclideanStepsId, 6.0 / 30.0);  // 8 steps
    handleFreezeParamChange(p, kFreezeEuclideanHitsId, 2.0 / 7.0);    // 3 hits
    handleFreezeParamChange(p, kFreezeEuclideanRotationId, 3.0 / 7.0);  // rotation 3
    REQUIRE(p.euclideanSteps.load() == 8);
    REQUIRE(p.euclideanHits.load() == 3);
    REQUIRE(p.euclideanRotation.load() == 3);

    handleFreezeParamChange(p, kFreezeEuclideanStepsId, 14.0 / 30.0);  // 16 steps
    CHECK(p.euclideanSteps.load() == 16);
    CHECK(p.euclideanHits.load() == 5);
    CHECK(p.euclideanRotation.load() == 6);

    // The readout prints the same values for the same normalized Hits / Rotation at 16 steps.
    CHECK(formatFreeze(kFreezeEuclideanHitsId, 2.0 / 7.0, 14.0 / 30.0) == "5");
    CHECK(formatFreeze(kFreezeEuclideanRotationId, 3.0 / 7.0, 14.0 / 30.0) == "6");
}

TEST_CASE("Freeze capture tail: absent loads Hold off / 1/1 / Mix, present round-trips", "[params][freeze][persistence]") {
    using namespace Iterum;
    using namespace Steinberg;

    SECTION("a stream ending after the MultiTap Length field loads Hold off, 1/1, Dry = Mix") {
        FreezeParams saved;
        auto stream = makeFreezeTail(saved, false);
        IBStreamer s(stream, kLittleEndian);
        FreezeParams loaded;
        loaded.hold.store(1);
        loaded.captureLength.store(0);
        loaded.dryMode.store(1);
        loadFreezeTail(loaded, s);
        CHECK(loaded.hold.load() == 0);
        CHECK(loaded.captureLength.load() == 2);
        CHECK(loaded.dryMode.load() == 0);
    }

    SECTION("a stream with the tail round-trips every combination") {
        for (int hold = 0; hold <= 1; ++hold) {
            for (int capture = 0; capture <= 3; ++capture) {
                for (int dry = 0; dry <= 1; ++dry) {
                    FreezeParams saved;
                    saved.hold.store(hold);
                    saved.captureLength.store(capture);
                    saved.dryMode.store(dry);
                    auto stream = makeFreezeTail(saved, true);
                    IBStreamer s(stream, kLittleEndian);
                    FreezeParams loaded;
                    loaded.hold.store(1 - hold);
                    loaded.captureLength.store(3 - capture);
                    loaded.dryMode.store(1 - dry);
                    loadFreezeTail(loaded, s);
                    INFO("hold " << hold << ", capture " << capture << ", dry " << dry);
                    CHECK(loaded.hold.load() == hold);
                    CHECK(loaded.captureLength.load() == capture);
                    CHECK(loaded.dryMode.load() == dry);
                }
            }
        }
    }

    SECTION("the three fields are the last 12 bytes, after the MultiTap Length field") {
        FreezeParams saved;
        saved.hold.store(1);
        saved.captureLength.store(3);
        saved.dryMode.store(0);
        auto withTail = makeFreezeTail(saved, true);
        auto without = makeFreezeTail(saved, false);
        int64 endWith = 0;
        int64 endWithout = 0;
        withTail->seek(0, IBStream::kIBSeekEnd, &endWith);
        without->seek(0, IBStream::kIBSeekEnd, &endWithout);
        REQUIRE(endWith == endWithout + 12);
        withTail->seek(endWithout, IBStream::kIBSeekSet, nullptr);
        IBStreamer s(withTail, kLittleEndian);
        int32 v[3] = {-1, -1, -1};
        REQUIRE(s.readInt32(v[0]));
        REQUIRE(s.readInt32(v[1]));
        REQUIRE(s.readInt32(v[2]));
        CHECK(v[0] == 1);
        CHECK(v[1] == 3);
        CHECK(v[2] == 0);
    }

    SECTION("out-of-range values clamp") {
        auto stream = Steinberg::owned(new MemoryStream());
        IBStreamer w(stream, kLittleEndian);
        w.writeInt32(5);
        w.writeInt32(9);
        w.writeInt32(-3);
        stream->seek(0, IBStream::kIBSeekSet, nullptr);
        IBStreamer r(stream, kLittleEndian);
        FreezeParams loaded;
        loadFreezeCaptureParams(loaded, r);
        CHECK(loaded.hold.load() == 1);
        CHECK(loaded.captureLength.load() == 3);
        CHECK(loaded.dryMode.load() == 0);
    }
}

TEST_CASE("Freeze capture controller sync", "[params][freeze][persistence]") {
    using namespace Iterum;
    using namespace Steinberg;

    SECTION("absent: (1070, 0.0), (1071, 2/3), (1072, 0.0)") {
        FreezeParams saved;
        auto stream = makeFreezeTail(saved, false);
        IBStreamer s(stream, kLittleEndian);
        skipToFreezeCaptureTail(s);
        std::map<Vst::ParamID, double> captured;
        loadFreezeCaptureParamsToController(s, [&](Vst::ParamID id, double v) { captured[id] = v; });
        REQUIRE(captured.count(kFreezeHoldId) == 1);
        REQUIRE(captured.count(kFreezeCaptureLengthId) == 1);
        REQUIRE(captured.count(kFreezeDryModeId) == 1);
        CHECK(captured[kFreezeHoldId] == 0.0);
        CHECK(captured[kFreezeCaptureLengthId] == Approx(2.0 / 3.0).margin(1e-9));
        CHECK(captured[kFreezeDryModeId] == 0.0);
    }

    SECTION("present: Hold 0 / 1, Capture index / 3, Dry 0 / 1") {
        for (int hold = 0; hold <= 1; ++hold) {
            for (int capture = 0; capture <= 3; ++capture) {
                for (int dry = 0; dry <= 1; ++dry) {
                    FreezeParams saved;
                    saved.hold.store(hold);
                    saved.captureLength.store(capture);
                    saved.dryMode.store(dry);
                    auto stream = makeFreezeTail(saved, true);
                    IBStreamer s(stream, kLittleEndian);
                    skipToFreezeCaptureTail(s);
                    std::map<Vst::ParamID, double> captured;
                    loadFreezeCaptureParamsToController(s, [&](Vst::ParamID id, double v) { captured[id] = v; });
                    INFO("hold " << hold << ", capture " << capture << ", dry " << dry);
                    CHECK(captured[kFreezeHoldId] == static_cast<double>(hold));
                    CHECK(captured[kFreezeCaptureLengthId] == Approx(static_cast<double>(capture) / 3.0).margin(1e-9));
                    CHECK(captured[kFreezeDryModeId] == static_cast<double>(dry));
                }
            }
        }
    }
}

TEST_CASE("Freeze capture tail reaches the controller", "[params][freeze][persistence]") {
    using namespace Iterum;
    using namespace Steinberg;
    auto controller = Steinberg::owned(new FreezeOnlyController());
    {
        FreezeParams saved;
        saved.dryMode.store(1);
        auto stream = makeFreezeTail(saved, true);
        IBStreamer s(stream, kLittleEndian);
        skipToFreezeCaptureTail(s);
        syncFreezeCaptureParamsToController(s, *controller);
        CHECK(controller->getParamNormalized(kFreezeDryModeId) == 1.0);
    }
    {
        FreezeParams saved;
        auto stream = makeFreezeTail(saved, false);
        IBStreamer s(stream, kLittleEndian);
        skipToFreezeCaptureTail(s);
        syncFreezeCaptureParamsToController(s, *controller);
        CHECK(controller->getParamNormalized(kFreezeDryModeId) == 0.0);
    }
}
