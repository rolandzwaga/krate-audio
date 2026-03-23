// ==============================================================================
// Layer 0: Core Tests - Hungarian Algorithm
// ==============================================================================
// Tests for: dsp/include/krate/dsp/core/hungarian_algorithm.h
//
// Verifies optimal assignment for known linear assignment problems.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <krate/dsp/core/hungarian_algorithm.h>

#include <array>
#include <cmath>
#include <vector>

using Catch::Approx;
using namespace Krate::DSP;

TEST_CASE("HungarianAlgorithm: 3x3 known optimal", "[hungarian]") {
    // Classic textbook example:
    //   Cost matrix:
    //     [10, 5, 13]
    //     [ 3, 7,  6]
    //     [ 5, 8,  3]
    // Optimal: row0->col1 (5), row1->col0 (3), row2->col2 (3) = 11
    std::array<float, 9> cost = {
        10.0f, 5.0f, 13.0f,
         3.0f, 7.0f,  6.0f,
         5.0f, 8.0f,  3.0f
    };

    HungarianAlgorithm<16> solver;
    solver.solve(cost.data(), 3, 3);

    float total = solver.getTotalCost(cost.data(), 3);
    CHECK(total == Approx(11.0f));

    // Verify assignments
    CHECK(solver.getRowAssignment(0) == 1);
    CHECK(solver.getRowAssignment(1) == 0);
    CHECK(solver.getRowAssignment(2) == 2);
}

TEST_CASE("HungarianAlgorithm: 2x2 trivial", "[hungarian]") {
    // [1, 2]
    // [2, 1]
    // Optimal: row0->col0 (1), row1->col1 (1) = 2
    std::array<float, 4> cost = {1.0f, 2.0f, 2.0f, 1.0f};

    HungarianAlgorithm<16> solver;
    solver.solve(cost.data(), 2, 2);

    float total = solver.getTotalCost(cost.data(), 2);
    CHECK(total == Approx(2.0f));
}

TEST_CASE("HungarianAlgorithm: 1x1", "[hungarian]") {
    float cost = 42.0f;

    HungarianAlgorithm<16> solver;
    solver.solve(&cost, 1, 1);

    CHECK(solver.getRowAssignment(0) == 0);
    CHECK(solver.getTotalCost(&cost, 1) == Approx(42.0f));
}

TEST_CASE("HungarianAlgorithm: rectangular (more cols than rows)", "[hungarian]") {
    // 2 rows, 3 cols
    // [1, 5, 3]
    // [4, 2, 6]
    // Optimal: row0->col0 (1), row1->col1 (2) = 3
    std::array<float, 6> cost = {
        1.0f, 5.0f, 3.0f,
        4.0f, 2.0f, 6.0f
    };

    HungarianAlgorithm<16> solver;
    solver.solve(cost.data(), 2, 3);

    float total = solver.getTotalCost(cost.data(), 3);
    CHECK(total == Approx(3.0f));

    CHECK(solver.getRowAssignment(0) == 0);
    CHECK(solver.getRowAssignment(1) == 1);
}

TEST_CASE("HungarianAlgorithm: rectangular (more rows than cols)", "[hungarian]") {
    // 3 rows, 2 cols
    // [1, 5]
    // [4, 2]
    // [3, 6]
    // Optimal: row0->col0 (1), row1->col1 (2) = 3 (row2 unassigned)
    std::array<float, 6> cost = {
        1.0f, 5.0f,
        4.0f, 2.0f,
        3.0f, 6.0f
    };

    HungarianAlgorithm<16> solver;
    solver.solve(cost.data(), 3, 2);

    float total = solver.getTotalCost(cost.data(), 2);
    CHECK(total == Approx(3.0f));

    // One row must be unassigned
    int assignedCount = 0;
    for (int i = 0; i < 3; ++i) {
        if (solver.getRowAssignment(i) != HungarianAlgorithm<16>::kUnassigned) {
            ++assignedCount;
        }
    }
    CHECK(assignedCount == 2);
}

TEST_CASE("HungarianAlgorithm: zero rows/cols returns unassigned", "[hungarian]") {
    HungarianAlgorithm<16> solver;
    solver.solve(nullptr, 0, 0);

    CHECK(solver.getRowAssignment(0) == HungarianAlgorithm<16>::kUnassigned);
}

TEST_CASE("HungarianAlgorithm: 4x4 with equal costs", "[hungarian]") {
    // All costs = 1.0. Any assignment is optimal with total = 4.
    std::array<float, 16> cost;
    cost.fill(1.0f);

    HungarianAlgorithm<16> solver;
    solver.solve(cost.data(), 4, 4);

    float total = solver.getTotalCost(cost.data(), 4);
    CHECK(total == Approx(4.0f));

    // Verify it's a valid permutation (each column used exactly once)
    std::array<bool, 4> colUsed = {false, false, false, false};
    for (int i = 0; i < 4; ++i) {
        int j = solver.getRowAssignment(i);
        REQUIRE(j >= 0);
        REQUIRE(j < 4);
        CHECK_FALSE(colUsed[static_cast<size_t>(j)]);
        colUsed[static_cast<size_t>(j)] = true;
    }
}

TEST_CASE("HungarianAlgorithm: frequency distance cost matrix for partial tracking",
           "[hungarian]") {
    // Simulate partial tracking scenario:
    // 3 previous partials at 440, 880, 1320 Hz
    // 4 current peaks at 442, 875, 1325, 2000 Hz
    // Cost = |freq_prev - freq_peak|, with max distance threshold = 50 Hz
    constexpr float kMaxDist = 50.0f;

    float prevFreqs[] = {440.0f, 880.0f, 1320.0f};
    float peakFreqs[] = {442.0f, 875.0f, 1325.0f, 2000.0f};

    std::array<float, 12> cost; // 3x4
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 4; ++j) {
            float dist = std::abs(prevFreqs[i] - peakFreqs[j]);
            cost[static_cast<size_t>(i) * 4 + static_cast<size_t>(j)] = (dist < kMaxDist) ? dist : kMaxDist * 100.0f;
        }
    }

    HungarianAlgorithm<16> solver;
    solver.solve(cost.data(), 3, 4);

    // Expected: prev[0]->peak[0] (2Hz), prev[1]->peak[1] (5Hz), prev[2]->peak[2] (5Hz)
    CHECK(solver.getRowAssignment(0) == 0);
    CHECK(solver.getRowAssignment(1) == 1);
    CHECK(solver.getRowAssignment(2) == 2);
}

TEST_CASE("HungarianAlgorithm: crossing partials resolved correctly",
           "[hungarian]") {
    // Two partials crossing: prev at 500, 510; peaks at 508, 502
    // Greedy would match prev[0]->peak[1] (2Hz) then prev[1]->peak[0] (2Hz) = 4
    // But also: prev[0]->peak[0] (8Hz) + prev[1]->peak[1] (2Hz) = 10
    // The optimal is total=4: prev[0]->peak[1], prev[1]->peak[0]
    float prevFreqs[] = {500.0f, 510.0f};
    float peakFreqs[] = {508.0f, 502.0f};

    std::array<float, 4> cost;
    for (int i = 0; i < 2; ++i) {
        for (int j = 0; j < 2; ++j) {
            cost[static_cast<size_t>(i) * 2 + static_cast<size_t>(j)] = std::abs(prevFreqs[i] - peakFreqs[j]);
        }
    }
    // cost = [8, 2, 2, 8]

    HungarianAlgorithm<16> solver;
    solver.solve(cost.data(), 2, 2);

    float total = solver.getTotalCost(cost.data(), 2);
    CHECK(total == Approx(4.0f));

    // prev[0] -> peak[1] (502Hz, dist=2), prev[1] -> peak[0] (508Hz, dist=2)
    CHECK(solver.getRowAssignment(0) == 1);
    CHECK(solver.getRowAssignment(1) == 0);
}
