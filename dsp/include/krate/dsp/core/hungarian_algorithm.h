// ==============================================================================
// Layer 0: Core Utility - Hungarian Algorithm (Kuhn-Munkres)
// ==============================================================================
// Solves the linear assignment problem: given an N×M cost matrix, find the
// assignment of rows to columns that minimizes total cost.
//
// Used by PartialTracker for optimal peak-to-track matching.
//
// Time complexity: O(n³) where n = max(rows, cols)
// Space: Fixed-size arrays, no heap allocation after construction.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in solve())
// - Principle III: Modern C++ (C++20, constexpr)
// - Principle IX: Layer 0 (no DSP dependencies)
// ==============================================================================

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

namespace Krate::DSP {

/// @brief Solves the linear assignment problem using the Hungarian algorithm.
///
/// Given a cost matrix of size rows × cols (rows ≤ kMaxSize, cols ≤ kMaxSize),
/// finds the minimum-cost assignment of rows to columns.
///
/// @tparam kMaxSize Maximum matrix dimension (rows or cols)
template <size_t kMaxSize = 128>
class HungarianAlgorithm {
public:
    /// Value representing "no assignment"
    static constexpr int kUnassigned = -1;

    /// Large cost to represent "forbidden" assignments
    static constexpr float kInfCost = 1e18f;

    HungarianAlgorithm() noexcept = default;

    /// @brief Solve the assignment problem.
    ///
    /// After calling, use getRowAssignment(i) to get the column assigned to row i,
    /// or getColAssignment(j) to get the row assigned to column j.
    ///
    /// @param cost Flat row-major cost matrix of size rows × cols.
    ///             Access: cost[row * cols + col]
    /// @param rows Number of rows (workers/tracks)
    /// @param cols Number of columns (jobs/peaks)
    /// @note Real-time safe (no allocations)
    void solve(const float* cost, int rows, int cols) noexcept {
        rows_ = rows;
        cols_ = cols;

        if (rows <= 0 || cols <= 0) {
            rowAssignment_.fill(kUnassigned);
            colAssignment_.fill(kUnassigned);
            return;
        }

        // Work on a square matrix of dimension n = max(rows, cols)
        const int n = std::max(rows, cols);
        if (n > static_cast<int>(kMaxSize)) {
            // Fallback: no assignment if matrix too large
            rowAssignment_.fill(kUnassigned);
            colAssignment_.fill(kUnassigned);
            return;
        }

        // Initialize potentials and assignments
        for (int i = 0; i <= n; ++i) {
            u_[i] = 0.0f;
            v_[i] = 0.0f;
            colMatch_[i] = 0;
            way_[i] = 0;
        }

        // Copy cost matrix into padded square matrix (1-indexed)
        // Pad with zeros for the dummy rows/cols
        for (int i = 1; i <= n; ++i) {
            for (int j = 1; j <= n; ++j) {
                if (i <= rows && j <= cols) {
                    a_[i][j] = cost[(i - 1) * cols + (j - 1)];
                } else {
                    a_[i][j] = 0.0f; // Dummy entries
                }
            }
        }

        // Hungarian algorithm (1-indexed, Jonker-Volgenant style)
        for (int i = 1; i <= n; ++i) {
            // p[0] = i means we're augmenting from row i
            colMatch_[0] = i;
            int j0 = 0; // Virtual column 0

            for (int j = 0; j <= n; ++j) {
                minVal_[j] = kInfCost;
                used_[j] = false;
            }

            do {
                used_[j0] = true;
                int i0 = colMatch_[j0];
                float delta = kInfCost;
                int j1 = -1;

                for (int j = 1; j <= n; ++j) {
                    if (used_[j]) continue;

                    float cur = a_[i0][j] - u_[i0] - v_[j];
                    if (cur < minVal_[j]) {
                        minVal_[j] = cur;
                        way_[j] = j0;
                    }
                    if (minVal_[j] < delta) {
                        delta = minVal_[j];
                        j1 = j;
                    }
                }

                // Update potentials
                for (int j = 0; j <= n; ++j) {
                    if (used_[j]) {
                        u_[colMatch_[j]] += delta;
                        v_[j] -= delta;
                    } else {
                        minVal_[j] -= delta;
                    }
                }

                j0 = j1;
            } while (colMatch_[j0] != 0);

            // Augment along the path
            do {
                int j1 = way_[j0];
                colMatch_[j0] = colMatch_[j1];
                j0 = j1;
            } while (j0 != 0);
        }

        // Extract assignments (convert from 1-indexed to 0-indexed)
        rowAssignment_.fill(kUnassigned);
        colAssignment_.fill(kUnassigned);

        for (int j = 1; j <= n; ++j) {
            int i = colMatch_[j];
            if (i >= 1 && i <= rows && j >= 1 && j <= cols) {
                rowAssignment_[static_cast<size_t>(i - 1)] = j - 1;
                colAssignment_[static_cast<size_t>(j - 1)] = i - 1;
            }
        }
    }

    /// @brief Get the column assigned to the given row.
    /// @param row Row index (0-based)
    /// @return Column index (0-based) or kUnassigned
    [[nodiscard]] int getRowAssignment(int row) const noexcept {
        if (row < 0 || row >= static_cast<int>(kMaxSize)) return kUnassigned;
        return rowAssignment_[static_cast<size_t>(row)];
    }

    /// @brief Get the row assigned to the given column.
    /// @param col Column index (0-based)
    /// @return Row index (0-based) or kUnassigned
    [[nodiscard]] int getColAssignment(int col) const noexcept {
        if (col < 0 || col >= static_cast<int>(kMaxSize)) return kUnassigned;
        return colAssignment_[static_cast<size_t>(col)];
    }

    /// @brief Get the total cost of the optimal assignment.
    /// @param cost Original cost matrix (same as passed to solve())
    /// @param cols Number of columns
    /// @return Sum of costs for assigned pairs
    [[nodiscard]] float getTotalCost(const float* cost, int cols) const noexcept {
        float total = 0.0f;
        for (int i = 0; i < rows_; ++i) {
            int j = rowAssignment_[static_cast<size_t>(i)];
            if (j != kUnassigned && j < cols) {
                total += cost[i * cols + j];
            }
        }
        return total;
    }

private:
    // Working arrays (1-indexed, hence +1 or +2 sizing)
    std::array<std::array<float, kMaxSize + 1>, kMaxSize + 1> a_{};
    std::array<float, kMaxSize + 1> u_{};      // Row potentials
    std::array<float, kMaxSize + 1> v_{};      // Column potentials
    std::array<int, kMaxSize + 1> colMatch_{}; // colMatch_[j] = row assigned to col j
    std::array<int, kMaxSize + 1> way_{};      // Augmenting path
    std::array<float, kMaxSize + 1> minVal_{}; // Minimum reduced costs
    std::array<bool, kMaxSize + 1> used_{};    // Visited columns

    // Output assignments (0-indexed)
    std::array<int, kMaxSize> rowAssignment_{};
    std::array<int, kMaxSize> colAssignment_{};

    int rows_ = 0;
    int cols_ = 0;
};

} // namespace Krate::DSP
