// ==============================================================================
// Unit Test: Controller Display Formatting
// ==============================================================================
// Verifies that getParamStringByValue() returns correct formatted strings
// with units for each parameter type (Hz, ms, %, st, dB, ct).
//
// Reference: specs/045-plugin-shell/spec.md FR-014, SC-007
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "controller/controller.h"
#include "plugin_ids.h"

#include <string>

using namespace Steinberg;
using namespace Steinberg::Vst;

// =============================================================================
// Helpers
// =============================================================================

static Ruinae::Controller* makeControllerRaw() {
    auto* ctrl = new Ruinae::Controller();
    ctrl->initialize(nullptr);
    return ctrl;
}

static std::string getDisplayString(Ruinae::Controller* ctrl, ParamID id, double value) {
    String128 str{};
    ctrl->getParamStringByValue(id, value, str);
    std::string result;
    for (int i = 0; i < 128 && str[i] != 0; ++i) {
        result += static_cast<char>(str[i]);
    }
    return result;
}

// =============================================================================
// Tests
// =============================================================================

TEST_CASE("Master Gain displays in dB", "[controller][display]") {
    auto* ctrl = makeControllerRaw();

    // 0.5 normalized = gain 1.0 = 0 dB
    std::string display = getDisplayString(ctrl, Ruinae::kMasterGainId, 0.5);
    CHECK(display.find("dB") != std::string::npos);
    CHECK(display.find("0.0") != std::string::npos);

    // 0.0 normalized = gain 0.0 = -80 dB (silence)
    display = getDisplayString(ctrl, Ruinae::kMasterGainId, 0.0);
    CHECK(display.find("dB") != std::string::npos);
    CHECK(display.find("-80") != std::string::npos);

    ctrl->terminate();
}

TEST_CASE("Filter Cutoff displays in Hz or kHz", "[controller][display]") {
    auto* ctrl = makeControllerRaw();

    // Low value -> Hz
    std::string display = getDisplayString(ctrl, Ruinae::kFilterCutoffId, 0.0);
    CHECK(display.find("Hz") != std::string::npos);

    // High value -> kHz
    display = getDisplayString(ctrl, Ruinae::kFilterCutoffId, 1.0);
    CHECK(display.find("kHz") != std::string::npos);

    ctrl->terminate();
}

TEST_CASE("Envelope times display in ms or s", "[controller][display]") {
    auto* ctrl = makeControllerRaw();

    // Small value -> ms
    std::string display = getDisplayString(ctrl, Ruinae::kAmpEnvAttackId, 0.1);
    CHECK(display.find("ms") != std::string::npos);

    // Large value -> s
    display = getDisplayString(ctrl, Ruinae::kAmpEnvAttackId, 1.0);
    CHECK(display.find("s") != std::string::npos);

    ctrl->terminate();
}

TEST_CASE("OSC A Tune displays in semitones", "[controller][display]") {
    auto* ctrl = makeControllerRaw();

    // 0.5 normalized = 0 semitones
    std::string display = getDisplayString(ctrl, Ruinae::kOscATuneId, 0.5);
    CHECK(display.find("st") != std::string::npos);
    CHECK(display.find("+0") != std::string::npos);

    // 1.0 normalized = +24 semitones
    display = getDisplayString(ctrl, Ruinae::kOscATuneId, 1.0);
    CHECK(display.find("+24") != std::string::npos);
    CHECK(display.find("st") != std::string::npos);

    // 0.0 normalized = -24 semitones
    display = getDisplayString(ctrl, Ruinae::kOscATuneId, 0.0);
    CHECK(display.find("-24") != std::string::npos);
    CHECK(display.find("st") != std::string::npos);

    ctrl->terminate();
}

TEST_CASE("OSC A Fine displays in cents", "[controller][display]") {
    auto* ctrl = makeControllerRaw();

    std::string display = getDisplayString(ctrl, Ruinae::kOscAFineId, 0.5);
    CHECK(display.find("ct") != std::string::npos);

    ctrl->terminate();
}

TEST_CASE("Percentage parameters display with % symbol", "[controller][display]") {
    auto* ctrl = makeControllerRaw();

    // OSC A Level
    std::string display = getDisplayString(ctrl, Ruinae::kOscALevelId, 0.75);
    CHECK(display.find("%") != std::string::npos);
    CHECK(display.find("75") != std::string::npos);

    // Distortion Drive
    display = getDisplayString(ctrl, Ruinae::kDistortionDriveId, 0.5);
    CHECK(display.find("%") != std::string::npos);
    CHECK(display.find("50") != std::string::npos);

    // Reverb Mix
    display = getDisplayString(ctrl, Ruinae::kReverbMixId, 1.0);
    CHECK(display.find("%") != std::string::npos);
    CHECK(display.find("100") != std::string::npos);

    ctrl->terminate();
}

TEST_CASE("LFO Rate displays in Hz", "[controller][display]") {
    auto* ctrl = makeControllerRaw();

    std::string display = getDisplayString(ctrl, Ruinae::kLFO1RateId, 0.5);
    CHECK(display.find("Hz") != std::string::npos);

    ctrl->terminate();
}

TEST_CASE("Filter Env Amount displays with st", "[controller][display]") {
    auto* ctrl = makeControllerRaw();

    // 0.5 = 0 semitones
    std::string display = getDisplayString(ctrl, Ruinae::kFilterEnvAmountId, 0.5);
    CHECK(display.find("st") != std::string::npos);
    CHECK(display.find("+0") != std::string::npos);

    // 1.0 = +48 semitones
    display = getDisplayString(ctrl, Ruinae::kFilterEnvAmountId, 1.0);
    CHECK(display.find("+48") != std::string::npos);

    ctrl->terminate();
}

TEST_CASE("Mod Matrix Amount displays as bipolar %", "[controller][display]") {
    auto* ctrl = makeControllerRaw();

    // 0.5 normalized = 0%
    std::string display = getDisplayString(ctrl, Ruinae::kModMatrixSlot0AmountId, 0.5);
    CHECK(display.find("%") != std::string::npos);
    CHECK(display.find("+0") != std::string::npos);

    // 1.0 normalized = +100%
    display = getDisplayString(ctrl, Ruinae::kModMatrixSlot0AmountId, 1.0);
    CHECK(display.find("+100") != std::string::npos);

    // 0.0 normalized = -100%
    display = getDisplayString(ctrl, Ruinae::kModMatrixSlot0AmountId, 0.0);
    CHECK(display.find("-100") != std::string::npos);

    ctrl->terminate();
}

TEST_CASE("Delay Time displays in ms or s", "[controller][display]") {
    auto* ctrl = makeControllerRaw();

    // Small value -> ms
    std::string display = getDisplayString(ctrl, Ruinae::kDelayTimeId, 0.0);
    CHECK(display.find("ms") != std::string::npos);

    // Large value -> s
    display = getDisplayString(ctrl, Ruinae::kDelayTimeId, 1.0);
    CHECK(display.find("s") != std::string::npos);

    ctrl->terminate();
}

TEST_CASE("Reverb Pre-Delay displays in ms", "[controller][display]") {
    auto* ctrl = makeControllerRaw();

    std::string display = getDisplayString(ctrl, Ruinae::kReverbPreDelayId, 0.5);
    CHECK(display.find("ms") != std::string::npos);

    ctrl->terminate();
}

TEST_CASE("Portamento Time displays in ms or s", "[controller][display]") {
    auto* ctrl = makeControllerRaw();

    // Small value -> ms
    std::string display = getDisplayString(ctrl, Ruinae::kMonoPortamentoTimeId, 0.1);
    CHECK(display.find("ms") != std::string::npos);

    ctrl->terminate();
}
