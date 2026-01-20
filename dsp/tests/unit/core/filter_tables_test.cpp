// ==============================================================================
// Layer 0: Core Utilities - Filter Tables Tests
// ==============================================================================
// Constitution Principle VIII: Testing Discipline
// Constitution Principle XII: Test-First Development
//
// Tests for: dsp/include/krate/dsp/core/filter_tables.h
// Contract: specs/070-filter-foundations/contracts/filter_tables.h
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <krate/dsp/core/filter_tables.h>

#include <cstdint>

using namespace Krate::DSP;
using Catch::Approx;

// ==============================================================================
// SC-012: Constexpr Verification (static_assert tests)
// ==============================================================================
// These static_asserts verify that formant data is constexpr

static_assert(kVowelFormants[static_cast<size_t>(Vowel::A)].f1 > 0.0f, "FormantData must be constexpr");
static_assert(getFormant(Vowel::A).f1 > 0.0f, "getFormant must be constexpr");
static_assert(kNumVowels == 5, "kNumVowels must be constexpr and equal to 5");

// ==============================================================================
// Vowel Enum Tests (FR-005)
// ==============================================================================

TEST_CASE("Vowel enum has correct values", "[filter_tables][vowel]") {

    SECTION("Vowel::A equals 0") {
        REQUIRE(static_cast<uint8_t>(Vowel::A) == 0);
    }

    SECTION("Vowel::E equals 1") {
        REQUIRE(static_cast<uint8_t>(Vowel::E) == 1);
    }

    SECTION("Vowel::I equals 2") {
        REQUIRE(static_cast<uint8_t>(Vowel::I) == 2);
    }

    SECTION("Vowel::O equals 3") {
        REQUIRE(static_cast<uint8_t>(Vowel::O) == 3);
    }

    SECTION("Vowel::U equals 4") {
        REQUIRE(static_cast<uint8_t>(Vowel::U) == 4);
    }
}

// ==============================================================================
// kVowelFormants Array Tests (FR-002, FR-003)
// ==============================================================================

TEST_CASE("kVowelFormants array has correct size", "[filter_tables][array]") {

    SECTION("Array has 5 elements") {
        REQUIRE(kVowelFormants.size() == 5);
    }

    SECTION("kNumVowels matches array size") {
        REQUIRE(kNumVowels == kVowelFormants.size());
    }
}

// ==============================================================================
// FormantData Validation Tests (FR-001)
// ==============================================================================

TEST_CASE("All formant frequencies are positive", "[filter_tables][validation]") {

    for (size_t i = 0; i < kVowelFormants.size(); ++i) {
        const auto& formant = kVowelFormants[i];

        SECTION("Vowel " + std::to_string(i) + " has positive F1") {
            REQUIRE(formant.f1 > 0.0f);
        }

        SECTION("Vowel " + std::to_string(i) + " has positive F2") {
            REQUIRE(formant.f2 > 0.0f);
        }

        SECTION("Vowel " + std::to_string(i) + " has positive F3") {
            REQUIRE(formant.f3 > 0.0f);
        }
    }
}

TEST_CASE("All formant bandwidths are positive", "[filter_tables][validation]") {

    for (size_t i = 0; i < kVowelFormants.size(); ++i) {
        const auto& formant = kVowelFormants[i];

        SECTION("Vowel " + std::to_string(i) + " has positive BW1") {
            REQUIRE(formant.bw1 > 0.0f);
        }

        SECTION("Vowel " + std::to_string(i) + " has positive BW2") {
            REQUIRE(formant.bw2 > 0.0f);
        }

        SECTION("Vowel " + std::to_string(i) + " has positive BW3") {
            REQUIRE(formant.bw3 > 0.0f);
        }
    }
}

TEST_CASE("Formants are ordered (F1 < F2 < F3)", "[filter_tables][validation]") {

    for (size_t i = 0; i < kVowelFormants.size(); ++i) {
        const auto& formant = kVowelFormants[i];

        SECTION("Vowel " + std::to_string(i) + " has F1 < F2") {
            REQUIRE(formant.f1 < formant.f2);
        }

        SECTION("Vowel " + std::to_string(i) + " has F2 < F3") {
            REQUIRE(formant.f2 < formant.f3);
        }
    }
}

// ==============================================================================
// SC-008: Research Value Verification (Vowel 'a')
// ==============================================================================

TEST_CASE("Vowel 'a' formants match research values within 10%", "[filter_tables][research]") {

    // Csound bass male voice reference values:
    // F1 = 600 Hz, F2 = 1040 Hz, F3 = 2250 Hz
    // BW1 = 60 Hz, BW2 = 70 Hz, BW3 = 110 Hz

    const auto& a = kVowelFormants[static_cast<size_t>(Vowel::A)];

    SECTION("SC-008: F1 is approximately 600 Hz (within 10%)") {
        REQUIRE(a.f1 == Approx(600.0f).epsilon(0.10f));
    }

    SECTION("SC-008: F2 is approximately 1040 Hz (within 10%)") {
        REQUIRE(a.f2 == Approx(1040.0f).epsilon(0.10f));
    }

    SECTION("SC-008: F3 is approximately 2250 Hz (within 10%)") {
        REQUIRE(a.f3 == Approx(2250.0f).epsilon(0.10f));
    }

    SECTION("BW1 is approximately 60 Hz (within 20%)") {
        REQUIRE(a.bw1 == Approx(60.0f).epsilon(0.20f));
    }

    SECTION("BW2 is approximately 70 Hz (within 20%)") {
        REQUIRE(a.bw2 == Approx(70.0f).epsilon(0.20f));
    }

    SECTION("BW3 is approximately 110 Hz (within 20%)") {
        REQUIRE(a.bw3 == Approx(110.0f).epsilon(0.20f));
    }
}

// ==============================================================================
// getFormant() Helper Function Tests (FR-003)
// ==============================================================================

TEST_CASE("getFormant returns correct data for each vowel", "[filter_tables][helper]") {

    SECTION("getFormant(Vowel::A) returns correct data") {
        const auto& a = getFormant(Vowel::A);
        REQUIRE(a.f1 == kVowelFormants[0].f1);
        REQUIRE(a.f2 == kVowelFormants[0].f2);
        REQUIRE(a.f3 == kVowelFormants[0].f3);
    }

    SECTION("getFormant(Vowel::E) returns correct data") {
        const auto& e = getFormant(Vowel::E);
        REQUIRE(e.f1 == kVowelFormants[1].f1);
        REQUIRE(e.f2 == kVowelFormants[1].f2);
    }

    SECTION("getFormant(Vowel::I) returns correct data") {
        const auto& i = getFormant(Vowel::I);
        REQUIRE(i.f1 == kVowelFormants[2].f1);
        REQUIRE(i.f2 == kVowelFormants[2].f2);
    }

    SECTION("getFormant(Vowel::O) returns correct data") {
        const auto& o = getFormant(Vowel::O);
        REQUIRE(o.f1 == kVowelFormants[3].f1);
        REQUIRE(o.f2 == kVowelFormants[3].f2);
    }

    SECTION("getFormant(Vowel::U) returns correct data") {
        const auto& u = getFormant(Vowel::U);
        REQUIRE(u.f1 == kVowelFormants[4].f1);
        REQUIRE(u.f2 == kVowelFormants[4].f2);
    }

    SECTION("getFormant returns reference to array element") {
        // Verify that getFormant returns a reference, not a copy
        const FormantData* ptr1 = &getFormant(Vowel::A);
        const FormantData* ptr2 = &kVowelFormants[static_cast<size_t>(Vowel::A)];
        REQUIRE(ptr1 == ptr2);
    }
}

// ==============================================================================
// Additional Vowel Data Verification
// ==============================================================================

TEST_CASE("Other vowels have reasonable formant values", "[filter_tables][data]") {

    SECTION("Vowel E: F1 < F2 < F3, typical ranges") {
        const auto& e = getFormant(Vowel::E);
        REQUIRE(e.f1 >= 300.0f);  // E typically has lower F1 than A
        REQUIRE(e.f1 <= 500.0f);
        REQUIRE(e.f2 >= 1500.0f); // E typically has higher F2 than A
        REQUIRE(e.f2 <= 2000.0f);
    }

    SECTION("Vowel I: F1 < F2 < F3, typical ranges") {
        const auto& i = getFormant(Vowel::I);
        REQUIRE(i.f1 >= 200.0f);  // I has lowest F1 (closed vowel)
        REQUIRE(i.f1 <= 350.0f);
        REQUIRE(i.f2 >= 1600.0f); // I has highest F2 (front vowel)
        REQUIRE(i.f2 <= 2000.0f);
    }

    SECTION("Vowel O: F1 < F2 < F3, typical ranges") {
        const auto& o = getFormant(Vowel::O);
        REQUIRE(o.f1 >= 300.0f);
        REQUIRE(o.f1 <= 500.0f);
        REQUIRE(o.f2 >= 600.0f);  // O has low F2 (back vowel)
        REQUIRE(o.f2 <= 900.0f);
    }

    SECTION("Vowel U: F1 < F2 < F3, typical ranges") {
        const auto& u = getFormant(Vowel::U);
        REQUIRE(u.f1 >= 250.0f);  // U has low F1 (closed vowel)
        REQUIRE(u.f1 <= 450.0f);
        REQUIRE(u.f2 >= 500.0f);  // U has lowest F2 (back vowel)
        REQUIRE(u.f2 <= 750.0f);
    }
}

// ==============================================================================
// F3 Values (Typically 2200-2800 Hz for all vowels)
// ==============================================================================

TEST_CASE("F3 values are in expected range for all vowels", "[filter_tables][f3]") {

    for (size_t i = 0; i < kVowelFormants.size(); ++i) {
        const auto& formant = kVowelFormants[i];

        SECTION("Vowel " + std::to_string(i) + " F3 in range 2200-2800 Hz") {
            REQUIRE(formant.f3 >= 2200.0f);
            REQUIRE(formant.f3 <= 2800.0f);
        }
    }
}

// ==============================================================================
// Bandwidth Reasonable Range Tests
// ==============================================================================

TEST_CASE("Bandwidth values are in reasonable ranges", "[filter_tables][bandwidth]") {

    for (size_t i = 0; i < kVowelFormants.size(); ++i) {
        const auto& formant = kVowelFormants[i];

        SECTION("Vowel " + std::to_string(i) + " BW1 in range 30-100 Hz") {
            REQUIRE(formant.bw1 >= 30.0f);
            REQUIRE(formant.bw1 <= 100.0f);
        }

        SECTION("Vowel " + std::to_string(i) + " BW2 in range 50-150 Hz") {
            REQUIRE(formant.bw2 >= 50.0f);
            REQUIRE(formant.bw2 <= 150.0f);
        }

        SECTION("Vowel " + std::to_string(i) + " BW3 in range 80-200 Hz") {
            REQUIRE(formant.bw3 >= 80.0f);
            REQUIRE(formant.bw3 <= 200.0f);
        }
    }
}
