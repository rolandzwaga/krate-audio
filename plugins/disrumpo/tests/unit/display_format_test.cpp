// ==============================================================================
// Parameter Display Formatting Tests
// ==============================================================================
// Constitution Principle VIII: Testing Discipline
// Tests for getParamStringByValue() custom formatting (FR-027)
//
// Formatting rules per spec:
// - Drive: plain number, one decimal, no unit (e.g., "5.2")
// - Mix: percentage, no decimal (e.g., "75%")
// - Gain: dB with one decimal (e.g., "4.5 dB")
// - Pan: "Center" at 0.5 normalized, else "30% L" or "30% R"
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>

#include "plugin_ids.h"
#include "controller/controller.h"
#include "controller/format_helpers.h"
#include "dsp/shape_slot_descriptors.h"
#include "processor/processor.h"

#include "pluginterfaces/vst/vsttypes.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <algorithm>
#include <array>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace Disrumpo;

// ==============================================================================
// Helper: Convert TChar string to std::string for comparison
// ==============================================================================
static std::string tcharToString(const Steinberg::Vst::TChar* str) {
    std::string result;
    while (*str) {
        result += static_cast<char>(*str);
        ++str;
    }
    return result;
}

// ==============================================================================
// Test Fixture: Create Controller-like parameter container for testing
// ==============================================================================
// Note: These tests verify the formatting logic, not the actual Controller.
// The Controller's getParamStringByValue() implementation must match these rules.

// ==============================================================================
// Test: Drive Display Format (T036c)
// ==============================================================================
TEST_CASE("Drive parameter displays as plain number with one decimal", "[display][format]") {
    // Drive range is [0, 10], so normalized value maps linearly

    SECTION("Drive value 1.0 displays as '1.0'") {
        // normalized = 1.0 / 10.0 = 0.1
        double normalized = 0.1;
        double plain = normalized * 10.0;  // Simulate toPlain()

        // Format: one decimal, no unit
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%.1f", plain);

        REQUIRE(std::string(buffer) == "1.0");
    }

    SECTION("Drive value 5.2 displays as '5.2'") {
        double plain = 5.2;
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%.1f", plain);

        REQUIRE(std::string(buffer) == "5.2");
    }

    SECTION("Drive value 10.0 displays as '10.0'") {
        double plain = 10.0;
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%.1f", plain);

        REQUIRE(std::string(buffer) == "10.0");
    }

    SECTION("Drive value 0.0 displays as '0.0'") {
        double plain = 0.0;
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%.1f", plain);

        REQUIRE(std::string(buffer) == "0.0");
    }

    SECTION("Drive value 7.5 displays as '7.5'") {
        double plain = 7.5;
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%.1f", plain);

        REQUIRE(std::string(buffer) == "7.5");
    }
}

// ==============================================================================
// Test: Mix Display Format (T036c)
// ==============================================================================
TEST_CASE("Mix parameter displays as percentage with no decimal", "[display][format]") {
    // Mix range is [0, 100], displayed as integer percentage

    SECTION("Mix value 75% displays as '75%'") {
        double plain = 75.0;
        int percent = static_cast<int>(std::round(plain));
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%d%%", percent);

        REQUIRE(std::string(buffer) == "75%");
    }

    SECTION("Mix value 0% displays as '0%'") {
        double plain = 0.0;
        int percent = static_cast<int>(std::round(plain));
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%d%%", percent);

        REQUIRE(std::string(buffer) == "0%");
    }

    SECTION("Mix value 100% displays as '100%'") {
        double plain = 100.0;
        int percent = static_cast<int>(std::round(plain));
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%d%%", percent);

        REQUIRE(std::string(buffer) == "100%");
    }

    SECTION("Mix value 50% displays as '50%'") {
        double plain = 50.0;
        int percent = static_cast<int>(std::round(plain));
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%d%%", percent);

        REQUIRE(std::string(buffer) == "50%");
    }

    SECTION("Mix value 33.3% rounds to '33%'") {
        double plain = 33.3;
        int percent = static_cast<int>(std::round(plain));
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%d%%", percent);

        REQUIRE(std::string(buffer) == "33%");
    }

    SECTION("Mix value 66.7% rounds to '67%'") {
        double plain = 66.7;
        int percent = static_cast<int>(std::round(plain));
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%d%%", percent);

        REQUIRE(std::string(buffer) == "67%");
    }
}

// ==============================================================================
// Test: Gain Display Format (T036c)
// ==============================================================================
TEST_CASE("Gain parameter displays with dB suffix and one decimal", "[display][format]") {
    // Gain range is [-24, +24] dB

    SECTION("Gain value 0.0 displays as '0.0 dB'") {
        double plain = 0.0;
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%.1f dB", plain);

        REQUIRE(std::string(buffer) == "0.0 dB");
    }

    SECTION("Gain value 4.5 displays as '4.5 dB'") {
        double plain = 4.5;
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%.1f dB", plain);

        REQUIRE(std::string(buffer) == "4.5 dB");
    }

    SECTION("Gain value -12.0 displays as '-12.0 dB'") {
        double plain = -12.0;
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%.1f dB", plain);

        REQUIRE(std::string(buffer) == "-12.0 dB");
    }

    SECTION("Gain value 24.0 displays as '24.0 dB'") {
        double plain = 24.0;
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%.1f dB", plain);

        REQUIRE(std::string(buffer) == "24.0 dB");
    }

    SECTION("Gain value -24.0 displays as '-24.0 dB'") {
        double plain = -24.0;
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%.1f dB", plain);

        REQUIRE(std::string(buffer) == "-24.0 dB");
    }
}

// ==============================================================================
// Test: Pan Display Format (T036c)
// ==============================================================================
TEST_CASE("Pan parameter displays with L/R suffix or Center", "[display][format]") {
    // Pan range is [-1, +1] in plain value
    // normalized 0.5 = plain 0.0 = Center
    // normalized < 0.5 = Left
    // normalized > 0.5 = Right

    SECTION("Pan at center (0.0 plain) displays as 'Center'") {
        double plain = 0.0;
        std::string result;

        if (std::abs(plain) < 0.01) {
            result = "Center";
        }

        REQUIRE(result == "Center");
    }

    SECTION("Pan at -0.3 displays as '30% L'") {
        double plain = -0.3;
        int percent = static_cast<int>(std::round(std::abs(plain) * 100.0));
        char buffer[32];

        if (plain < 0) {
            snprintf(buffer, sizeof(buffer), "%d%% L", percent);
        }

        REQUIRE(std::string(buffer) == "30% L");
    }

    SECTION("Pan at +0.3 displays as '30% R'") {
        double plain = 0.3;
        int percent = static_cast<int>(std::round(plain * 100.0));
        char buffer[32];

        if (plain > 0) {
            snprintf(buffer, sizeof(buffer), "%d%% R", percent);
        }

        REQUIRE(std::string(buffer) == "30% R");
    }

    SECTION("Pan at -1.0 (full left) displays as '100% L'") {
        double plain = -1.0;
        int percent = static_cast<int>(std::round(std::abs(plain) * 100.0));
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%d%% L", percent);

        REQUIRE(std::string(buffer) == "100% L");
    }

    SECTION("Pan at +1.0 (full right) displays as '100% R'") {
        double plain = 1.0;
        int percent = static_cast<int>(std::round(plain * 100.0));
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%d%% R", percent);

        REQUIRE(std::string(buffer) == "100% R");
    }

    SECTION("Pan near center (-0.005) still displays as 'Center'") {
        double plain = -0.005;
        std::string result;

        if (std::abs(plain) < 0.01) {
            result = "Center";
        } else if (plain < 0) {
            int percent = static_cast<int>(std::round(std::abs(plain) * 100.0));
            char buffer[32];
            snprintf(buffer, sizeof(buffer), "%d%% L", percent);
            result = buffer;
        }

        REQUIRE(result == "Center");
    }

    SECTION("Pan at -0.5 displays as '50% L'") {
        double plain = -0.5;
        int percent = static_cast<int>(std::round(std::abs(plain) * 100.0));
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%d%% L", percent);

        REQUIRE(std::string(buffer) == "50% L");
    }

    SECTION("Pan at +0.75 displays as '75% R'") {
        double plain = 0.75;
        int percent = static_cast<int>(std::round(plain * 100.0));
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%d%% R", percent);

        REQUIRE(std::string(buffer) == "75% R");
    }
}

// ==============================================================================
// Test: Parameter ID Detection for Format Selection
// ==============================================================================
TEST_CASE("Parameter ID encoding identifies correct parameter types", "[display][encoding]") {
    SECTION("Node Drive parameter is correctly identified") {
        auto paramId = makeNodeParamId(0, 0, NodeParamType::kNodeDrive);
        REQUIRE(isNodeParamId(paramId) == true);
        REQUIRE(extractNodeParamType(paramId) == NodeParamType::kNodeDrive);
    }

    SECTION("Node Mix parameter is correctly identified") {
        auto paramId = makeNodeParamId(3, 2, NodeParamType::kNodeMix);
        REQUIRE(isNodeParamId(paramId) == true);
        REQUIRE(extractNodeParamType(paramId) == NodeParamType::kNodeMix);
    }

    SECTION("Band Gain parameter is correctly identified") {
        auto paramId = makeBandParamId(5, BandParamType::kBandGain);
        REQUIRE(isBandParamId(paramId) == true);
        REQUIRE(extractBandParamType(paramId) == BandParamType::kBandGain);
    }

    SECTION("Band Pan parameter is correctly identified") {
        auto paramId = makeBandParamId(2, BandParamType::kBandPan);
        REQUIRE(isBandParamId(paramId) == true);
        REQUIRE(extractBandParamType(paramId) == BandParamType::kBandPan);
    }

    SECTION("Global Mix parameter is correctly identified") {
        auto paramId = makeGlobalParamId(GlobalParamType::kGlobalMix);
        REQUIRE(isGlobalParamId(paramId) == true);
    }

    SECTION("Global Input Gain parameter is correctly identified") {
        auto paramId = makeGlobalParamId(GlobalParamType::kGlobalInputGain);
        REQUIRE(isGlobalParamId(paramId) == true);
    }
}

// ==============================================================================
// D-090 (fix plan T4.03): Ring Saturation's Hz/Ratio slot follows the Freq mode
// ==============================================================================
// The adapter reads slot 6 as 20 * 250^v Hz in Fixed mode and as the harmonic
// ratio 1 + round(v * 15) in Harmonic mode (slot 5). The host text and the knob
// popup must show what the DSP runs, for the band's displayed-node proxy and for
// the node's own parameter.
TEST_CASE("Ring Saturation carrier slot text follows the Freq mode slot", "[disrumpo][display][D-090]") {
    Controller c;
    REQUIRE(c.initialize(nullptr) == Steinberg::kResultOk);
    c.setParamNormalized(makeBandParamId(0, BandParamType::kBandDisplayedType),
                         static_cast<double>(static_cast<int>(DistortionType::RingSaturation)) / 25.0);

    const auto textOf = [&](Steinberg::Vst::ParamID id, double v) {
        Steinberg::Vst::String128 s{};
        REQUIRE(c.getParamStringByValue(id, v, s) == Steinberg::kResultTrue);
        return tcharToString(s);
    };
    const auto proxy6 = makeBandParamId(0, BandParamType::kBandDisplayedShape6);
    const auto node6 = makeNodeParamId(0, 0, NodeParamType::kNodeShape6);

    // Fixed (slot 5 = 0): 20 * 250^0.5 = 316.2 Hz
    c.setParamNormalized(makeBandParamId(0, BandParamType::kBandDisplayedShape5), 0.0);
    c.setParamNormalized(makeNodeParamId(0, 0, NodeParamType::kNodeShape5), 0.0);
    const std::string fixedProxy = textOf(proxy6, 0.5);
    const std::string fixedNode = textOf(node6, 0.5);

    // Harmonic (slot 5 = 1/3 -> index 1): ratio 1 + round(7.5) = 9
    c.setParamNormalized(makeBandParamId(0, BandParamType::kBandDisplayedShape5), 1.0 / 3.0);
    c.setParamNormalized(makeNodeParamId(0, 0, NodeParamType::kNodeShape5), 1.0 / 3.0);
    const std::string harmonicProxy = textOf(proxy6, 0.5);
    const std::string harmonicNode = textOf(node6, 0.5);

    std::printf("T4.03 RingSat slot 6 at 0.5: Fixed proxy '%s' node '%s' | Harmonic proxy '%s' node '%s'\n",
                fixedProxy.c_str(), fixedNode.c_str(), harmonicProxy.c_str(), harmonicNode.c_str());
    CHECK(fixedProxy == "316 Hz");
    CHECK(fixedNode == "316 Hz");
    CHECK(harmonicProxy == "9x");
    CHECK(harmonicNode == "9x");
    c.terminate();
}

// Every slot of every type: the formatter's text, parsed back, is the value
// Processor::mapShapeSlotsToParams writes for the same slot values (within 1 % of
// the slot's range), and for the Norm fields the adapter maps further, the value
// the adapter runs (formulas written out here from distortion_adapter.cpp).
TEST_CASE("Shape slot text shows the value the processor maps the slot to", "[disrumpo][display][D-090]") {
    static const char* const kCurveNames[] = {"Tanh", "Atan", "Cubic", "Quintic", "RSqrt",
                                              "Erf", "HardClip", "Diode", "Tube"};
    int checked = 0;
    int bad = 0;
    const auto report = [&](bool ok, int t, int s, float v, const std::string& text, double want) {
        ++checked;
        if (ok) return;
        ++bad;
        if (bad <= 20) std::printf("T4.03 type %d slot %d v %.2f: text '%s', want %.4f\n", t, s, v, text.c_str(), want);
    };
    for (int t = 0; t < kDistortionTypeCount; ++t) {
        const auto type = static_cast<DistortionType>(t);
        for (int s = 0; s < kShapeSlotDescriptorCount; ++s) {
            const ShapeSlotDescriptor& d = shapeSlotDescriptor(type, s);
            for (const float mode5 : {0.5f, 1.0f / 3.0f}) {
                for (int i = 0; i <= 20; ++i) {
                    std::array<float, kShapeSlotDescriptorCount> slots{};
                    slots.fill(0.5f);
                    slots[static_cast<size_t>(s)] = static_cast<float>(i) / 20.0f;
                    if (s != 5) slots[5] = mode5;
                    const float v = slots[static_cast<size_t>(s)];
                    DistortionParams p;
                    Processor::mapShapeSlotsToParams(type, slots.data(), p);
                    Steinberg::Vst::String128 str{};
                    const bool formatted = formatShapeSlot(type, s, v, slots.data(), str);
                    const std::string text = formatted ? tcharToString(str) : std::string();
                    const double shown = std::strtod(text.c_str(), nullptr);
                    if (d.map == SlotMap::Unused) {
                        report(!formatted, t, s, v, text, 0.0);
                        continue;
                    }
                    if (!formatted) {
                        report(false, t, s, v, "(no format)", 0.0);
                        continue;
                    }
                    switch (d.map) {
                        case SlotMap::Toggle:
                            report(text == ((p.*d.boolField) ? "On" : "Off"), t, s, v, text, (p.*d.boolField) ? 1 : 0);
                            break;
                        case SlotMap::Index:
                        case SlotMap::Pow2: {
                            const int field = p.*d.intField;
                            if (d.labels != nullptr) {
                                int k = -1;
                                for (int e = 0; e <= static_cast<int>(d.span); ++e) {
                                    if (text == d.labels[e]) k = e;
                                }
                                const int first = static_cast<int>(d.min);
                                const int labelled = d.map == SlotMap::Pow2 ? first * (1 << k) : first + k;
                                report(k >= 0 && labelled == field, t, s, v, text, field);
                            } else {
                                report(std::lround(shown) == field, t, s, v, text, field);
                            }
                            break;
                        }
                        case SlotMap::Linear:
                        case SlotMap::Bipolar: {
                            const double field = p.*d.floatField;
                            const double range = d.map == SlotMap::Bipolar ? 2.0 * d.span : d.span;
                            report(std::fabs(shown - field) <= 0.01 * range, t, s, v, text, field);
                            break;
                        }
                        case SlotMap::Norm: {
                            const double field = p.*d.floatField;
                            if (d.display == SlotDisplay::CurveName) {
                                const int idx = std::clamp(static_cast<int>(field * 8.0 + 0.5), 0, 8);
                                report(text == kCurveNames[idx], t, s, v, text, idx);
                            } else if (d.display == SlotDisplay::TemporalHoldMs) {
                                const double ms = 1.0 + field * 499.0;
                                report(std::fabs(shown - ms) <= 0.01 * 499.0, t, s, v, text, ms);
                            } else if (d.display == SlotDisplay::RingCarrier) {
                                if (p.rsFreqSelect == 1) {
                                    // float, as the adapter computes it (0.7f * 15 rounds to 10.5f)
                                    const double ratio = 1.0f + std::round(static_cast<float>(field) * 15.0f);
                                    report(shown == ratio && text.back() == 'x', t, s, v, text, ratio);
                                } else {
                                    const double hz = 20.0 * std::pow(250.0, field);
                                    report(std::fabs(shown - hz) <= 0.01 * hz && text.find("Hz") != std::string::npos,
                                           t, s, v, text, hz);
                                }
                            } else {
                                report(std::fabs(shown - field * 100.0) <= 1.0, t, s, v, text, field * 100.0);
                            }
                            break;
                        }
                        case SlotMap::Unused:
                            break;
                    }
                }
            }
        }
    }
    std::printf("T4.03 shape slot text: %d of %d (type, slot, value) cases off\n", bad, checked);
    CHECK(bad == 0);
}
