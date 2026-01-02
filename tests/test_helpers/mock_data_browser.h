#pragma once

// =============================================================================
// Mock CDataBrowser for UI Testing
// =============================================================================
// Minimal mock that tracks method calls for verification in tests.
// =============================================================================

#include <cstdint>

namespace Iterum {
namespace Testing {

/// @brief Mock CDataBrowser for testing PresetDataSource behavior
///
/// This mock tracks:
/// - Current selected row
/// - Whether unselectAll() was called
/// - Call counts for verification
class MockDataBrowser {
public:
    // State
    int32_t selectedRow_ = -1;

    // Call tracking
    bool unselectAllCalled_ = false;
    int unselectAllCallCount_ = 0;

    // Methods that mirror CDataBrowser interface
    int32_t getSelectedRow() const { return selectedRow_; }

    void setSelectedRow(int32_t row) {
        selectedRow_ = row;
    }

    void unselectAll() {
        unselectAllCalled_ = true;
        unselectAllCallCount_++;
        selectedRow_ = -1;
    }

    // Test helpers
    void resetMockState() {
        unselectAllCalled_ = false;
        unselectAllCallCount_ = 0;
        // Note: selectedRow_ is NOT reset - that's state, not tracking
    }

    void reset() {
        selectedRow_ = -1;
        unselectAllCalled_ = false;
        unselectAllCallCount_ = 0;
    }
};

} // namespace Testing
} // namespace Iterum
