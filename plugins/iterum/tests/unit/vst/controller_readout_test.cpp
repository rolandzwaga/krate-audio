// ==============================================================================
// Controller readouts: Gain in dB, Freeze Hits / Rotation against the current Steps (design-system.md 6.4, 9.3)
// ==============================================================================

#include "controller/controller.h"
#include "plugin_ids.h"

#include "base/source/fobject.h"
#include "base/source/updatehandler.h"
#include "pluginterfaces/base/ustring.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace Iterum;

namespace {

[[nodiscard]] std::string paramString(Controller& c, Steinberg::Vst::ParamID id, double normalized) {
    Steinberg::Vst::String128 s = {};
    REQUIRE(c.getParamStringByValue(id, normalized, s) == Steinberg::kResultOk);
    char buf[128] = {};
    Steinberg::UString(s, 128).toAscii(buf, 128);
    return buf;
}

/// Counts the kChanged notifications a parameter sends its dependents.
class ChangeCounter : public Steinberg::FObject {
public:
    void PLUGIN_API update(Steinberg::FUnknown* /*changedUnknown*/, Steinberg::int32 message) override {
        if (message == IDependent::kChanged) {
            ++changes;
        }
    }
    int changes = 0;
};

}  // namespace

TEST_CASE("Gain readout prints dB and parses it back", "[vst][gain][format]") {
    auto controller = Steinberg::owned(new Controller());
    REQUIRE(controller->initialize(nullptr) == Steinberg::kResultOk);

    CHECK(paramString(*controller, kGainId, 0.0) == "-80.0 dB");
    CHECK(paramString(*controller, kGainId, 0.5) == "0.0 dB");
    CHECK(paramString(*controller, kGainId, 1.0) == "6.0 dB");

    Steinberg::Vst::String128 text = {};
    Steinberg::UString(text, 128).fromAscii("-6.0 dB");
    Steinberg::Vst::ParamValue parsed = -1.0;
    REQUIRE(controller->getParamValueByString(kGainId, text, parsed) == Steinberg::kResultTrue);
    CHECK(paramString(*controller, kGainId, parsed) == "-6.0 dB");

    REQUIRE(controller->terminate() == Steinberg::kResultOk);
}

TEST_CASE("Freeze Hits and Rotation readouts use the current Steps", "[freeze][format]") {
    auto controller = Steinberg::owned(new Controller());
    REQUIRE(controller->initialize(nullptr) == Steinberg::kResultOk);

    controller->setParamNormalized(kFreezeEuclideanStepsId, 14.0 / 30.0);  // 16 steps
    CHECK(paramString(*controller, kFreezeEuclideanHitsId, 1.0) == "16");
    CHECK(paramString(*controller, kFreezeEuclideanRotationId, 1.0) == "15");

    controller->setParamNormalized(kFreezeEuclideanStepsId, 6.0 / 30.0);  // 8 steps
    CHECK(paramString(*controller, kFreezeEuclideanHitsId, 1.0) == "8");
    CHECK(paramString(*controller, kFreezeEuclideanRotationId, 1.0) == "7");

    REQUIRE(controller->terminate() == Steinberg::kResultOk);
}

TEST_CASE("Changing Freeze Steps notifies the Hits and Rotation readouts", "[freeze]") {
    Steinberg::UpdateHandler::instance();  // deferUpdate needs the update handler
    auto controller = Steinberg::owned(new Controller());
    REQUIRE(controller->initialize(nullptr) == Steinberg::kResultOk);

    auto* hits = controller->getParameterObject(kFreezeEuclideanHitsId);
    auto* rotation = controller->getParameterObject(kFreezeEuclideanRotationId);
    REQUIRE(hits != nullptr);
    REQUIRE(rotation != nullptr);
    ChangeCounter hitsSeen;
    ChangeCounter rotationSeen;
    hits->addDependent(&hitsSeen);
    rotation->addDependent(&rotationSeen);
    Steinberg::UpdateHandler::instance()->triggerDeferedUpdates();
    hitsSeen.changes = 0;
    rotationSeen.changes = 0;

    controller->setParamNormalized(kFreezeEuclideanStepsId, 0.5);
    Steinberg::UpdateHandler::instance()->triggerDeferedUpdates();
    CHECK(hitsSeen.changes >= 1);
    CHECK(rotationSeen.changes >= 1);

    hits->removeDependent(&hitsSeen);
    rotation->removeDependent(&rotationSeen);
    REQUIRE(controller->terminate() == Steinberg::kResultOk);
}
