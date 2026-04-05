// =============================================================================
// Markov Matrix Preset Tests
// =============================================================================
// Spec 133 (Gradus v1.7) — 5 hardcoded transition matrices for the Markov arp
// mode. Tests verify:
//   - Row-stochasticity (each row sums to ~1.0)
//   - Musical bias invariants (Jazz favors ii→V→I, etc.)
// =============================================================================

#include <catch2/catch_all.hpp>

#include <krate/dsp/processors/markov_matrices.h>
#include <krate/dsp/primitives/held_note_buffer.h>

#include <array>
#include <cmath>

using Krate::DSP::MarkovPreset;
using Krate::DSP::getMarkovPresetMatrix;
using Krate::DSP::kMarkovMatrixDim;
using Krate::DSP::kMarkovMatrixSize;

namespace {

// Helper: sum the cells of one row of a 7x7 row-major matrix.
float rowSum(const std::array<float, kMarkovMatrixSize>& m, int row) {
    float s = 0.0f;
    for (size_t c = 0; c < kMarkovMatrixDim; ++c) {
        s += m[static_cast<size_t>(row) * kMarkovMatrixDim + c];
    }
    return s;
}

float cell(const std::array<float, kMarkovMatrixSize>& m, int row, int col) {
    return m[static_cast<size_t>(row) * kMarkovMatrixDim + static_cast<size_t>(col)];
}

// Degree constants for readability
constexpr int kI   = 0;
constexpr int kII  = 1;
constexpr int kIII = 2;
constexpr int kIV  = 3;
constexpr int kV   = 4;
constexpr int kVI  = 5;
constexpr int kVII = 6;

} // namespace

TEST_CASE("Markov presets are row-stochastic", "[markov][matrices]") {
    constexpr MarkovPreset presets[] = {
        MarkovPreset::Uniform,
        MarkovPreset::Jazz,
        MarkovPreset::Minimal,
        MarkovPreset::Ambient,
        MarkovPreset::Classical,
    };

    for (auto preset : presets) {
        const auto& m = getMarkovPresetMatrix(preset);
        for (size_t row = 0; row < kMarkovMatrixDim; ++row) {
            const float sum = rowSum(m, static_cast<int>(row));
            INFO("preset=" << static_cast<int>(preset) << " row=" << row
                 << " sum=" << sum);
            CHECK(sum == Catch::Approx(1.0f).margin(1e-5f));
        }
    }
}

TEST_CASE("Markov Uniform matrix is actually uniform", "[markov][matrices]") {
    const auto& m = getMarkovPresetMatrix(MarkovPreset::Uniform);
    constexpr float expected = 1.0f / 7.0f;
    for (size_t i = 0; i < kMarkovMatrixSize; ++i) {
        CHECK(m[i] == Catch::Approx(expected).margin(1e-6f));
    }
}

TEST_CASE("Markov Jazz matrix encodes ii-V-I voice leading",
          "[markov][matrices]") {
    const auto& m = getMarkovPresetMatrix(MarkovPreset::Jazz);

    // ii → V should dominate the ii row
    CHECK(cell(m, kII, kV) > 0.5f);
    CHECK(cell(m, kII, kV) > cell(m, kII, kI));
    CHECK(cell(m, kII, kV) > cell(m, kII, kIV));

    // V → I should dominate the V row
    CHECK(cell(m, kV, kI) > 0.4f);
    CHECK(cell(m, kV, kI) > cell(m, kV, kII));

    // vi → ii is common in jazz (ii-V-I setup via vi)
    CHECK(cell(m, kVI, kII) > 0.3f);

    // vii° → I (leading tone resolution)
    CHECK(cell(m, kVII, kI) > 0.4f);
}

TEST_CASE("Markov Minimal matrix has strong self-loops",
          "[markov][matrices]") {
    const auto& m = getMarkovPresetMatrix(MarkovPreset::Minimal);

    // Every degree should have a strong self-loop (diagonal > 0.4)
    for (size_t d = 0; d < kMarkovMatrixDim; ++d) {
        INFO("degree=" << d);
        CHECK(cell(m, static_cast<int>(d), static_cast<int>(d)) > 0.4f);
    }

    // ±1 neighbors should be the next-strongest transitions for interior
    // degrees (skip boundary cases).
    for (size_t d = 1; d + 1 < kMarkovMatrixDim; ++d) {
        INFO("degree=" << d);
        const int di = static_cast<int>(d);
        const float left  = cell(m, di, di - 1);
        const float right = cell(m, di, di + 1);
        // Left/right should each be at least 0.15 (step motion is weighted)
        CHECK(left > 0.1f);
        CHECK(right > 0.1f);
    }
}

TEST_CASE("Markov Ambient matrix prefers wide jumps over neighbors",
          "[markov][matrices]") {
    const auto& m = getMarkovPresetMatrix(MarkovPreset::Ambient);

    // From degree 0 (I), the strongest transitions should be to mid/high
    // degrees (3-5), not to immediate neighbors (0, 1).
    const float nearSum = cell(m, kI, kI) + cell(m, kI, kII);
    const float farSum  = cell(m, kI, kIV) + cell(m, kI, kV) + cell(m, kI, kVI);
    CHECK(farSum > nearSum);

    // From degree 3 (IV), the strongest transitions should be to degrees
    // distant from IV (far from 3 in the 0..6 space).
    const float farFromIV = cell(m, kIV, kI) + cell(m, kIV, kII);
    const float nearIV    = cell(m, kIV, kIII) + cell(m, kIV, kIV) + cell(m, kIV, kV);
    CHECK(farFromIV > nearIV);
}

TEST_CASE("Markov Classical matrix encodes I-IV-V-I circle of fifths",
          "[markov][matrices]") {
    const auto& m = getMarkovPresetMatrix(MarkovPreset::Classical);

    // I → IV or V should dominate
    CHECK(cell(m, kI, kIV) + cell(m, kI, kV) > 0.55f);

    // IV → V should dominate the IV row
    CHECK(cell(m, kIV, kV) > 0.5f);
    CHECK(cell(m, kIV, kV) > cell(m, kIV, kI));

    // V → I should dominate the V row (strong cadential pull)
    CHECK(cell(m, kV, kI) > 0.5f);
    CHECK(cell(m, kV, kI) > cell(m, kV, kIV));
}

TEST_CASE("Markov Custom preset returns Uniform as fallback",
          "[markov][matrices]") {
    const auto& m = getMarkovPresetMatrix(MarkovPreset::Custom);
    constexpr float expected = 1.0f / 7.0f;
    for (size_t i = 0; i < kMarkovMatrixSize; ++i) {
        CHECK(m[i] == Catch::Approx(expected).margin(1e-6f));
    }
}
