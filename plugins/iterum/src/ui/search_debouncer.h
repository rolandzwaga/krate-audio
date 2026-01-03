#pragma once

// =============================================================================
// SearchDebouncer - Debounce Logic for Search Input
// =============================================================================
// Pure function implementation with no VSTGUI dependencies.
// Delays filter application by 200ms to avoid excessive updates during typing.
//
// Usage:
//   1. Call onTextChanged() when search text changes
//   2. Periodically call shouldApplyFilter() with current time
//   3. When shouldApplyFilter() returns true, call consumePendingFilter() to get query
// =============================================================================

#include <string>
#include <cstdint>
#include <algorithm>

namespace Iterum {

class SearchDebouncer {
public:
    static constexpr uint64_t kDebounceMs = 200;

    SearchDebouncer() = default;

    /// @brief Called when search text changes
    /// @param newText The new search text
    /// @param currentTimeMs Current time in milliseconds
    /// @return true if filter should be applied immediately (empty/whitespace-only text)
    bool onTextChanged(const std::string& newText, uint64_t currentTimeMs) {
        // Check if text is empty or whitespace-only
        bool isEmpty = isEmptyOrWhitespace(newText);

        if (isEmpty) {
            // Empty search clears immediately without debounce
            pendingQuery_.clear();
            hasPending_ = false;
            lastChangeTime_ = 0;
            return true;  // Apply immediately
        }

        // If text is the same as pending, don't reset timer
        if (newText == pendingQuery_) {
            return false;
        }

        // New non-empty text: set pending and reset timer
        pendingQuery_ = newText;
        lastChangeTime_ = currentTimeMs;
        hasPending_ = true;
        return false;  // Don't apply yet, wait for debounce
    }

    /// @brief Check if debounce period has elapsed and filter should be applied
    /// @param currentTimeMs Current time in milliseconds
    /// @return true if filter should be applied
    bool shouldApplyFilter(uint64_t currentTimeMs) const {
        if (!hasPending_) {
            return false;
        }

        // Check if debounce period has elapsed
        return (currentTimeMs - lastChangeTime_) >= kDebounceMs;
    }

    /// @brief Check if there's a pending filter waiting for debounce
    /// @return true if there's a pending filter
    bool hasPendingFilter() const {
        return hasPending_;
    }

    /// @brief Get the pending query (without consuming it)
    /// @return The pending query string
    const std::string& getPendingQuery() const {
        return pendingQuery_;
    }

    /// @brief Consume the pending filter and return the query
    /// @return The query to apply (or empty if none pending)
    std::string consumePendingFilter() {
        if (!hasPending_) {
            return "";
        }

        std::string result = std::move(pendingQuery_);
        pendingQuery_.clear();
        hasPending_ = false;
        lastChangeTime_ = 0;
        return result;
    }

    /// @brief Reset all state
    void reset() {
        pendingQuery_.clear();
        hasPending_ = false;
        lastChangeTime_ = 0;
    }

private:
    std::string pendingQuery_;
    uint64_t lastChangeTime_ = 0;
    bool hasPending_ = false;

    /// @brief Check if string is empty or contains only whitespace
    static bool isEmptyOrWhitespace(const std::string& s) {
        return s.empty() ||
               std::all_of(s.begin(), s.end(), [](unsigned char c) {
                   return std::isspace(c);
               });
    }
};

} // namespace Iterum
