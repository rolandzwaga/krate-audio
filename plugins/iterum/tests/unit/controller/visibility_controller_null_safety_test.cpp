// ==============================================================================
// Visibility Controller Null Safety Regression Test
// ==============================================================================
// Regression test for crash when closing plugin window.
//
// BUG: When willClose() destroyed VisibilityControllers before setting
// activeEditor_ to nullptr, any pending deferred update could access
// a partially-destroyed editor, causing a crash.
//
// FIX: Set activeEditor_ = nullptr FIRST, before destroying visibility
// controllers. The update() method checks *editorPtr_ and returns early
// if nullptr, making destruction safe regardless of pending callbacks.
//
// This test verifies the null-safety pattern used by VisibilityController.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <functional>
#include <vector>

namespace {

// =============================================================================
// Minimal mock types to simulate VisibilityController pattern
// =============================================================================

// Simulates VSTGUI::VST3Editor
class MockEditor {
public:
    bool isValid() const { return valid_; }
    void invalidate() { valid_ = false; }
private:
    bool valid_ = true;
};

// Simulates the VisibilityController pattern from controller.cpp
// Key feature: stores pointer-to-pointer for safe null checking
class MockVisibilityController {
public:
    // Constructor takes pointer to the controller's editor member
    explicit MockVisibilityController(MockEditor** editorPtr)
        : editorPtr_(editorPtr)
    {
        // Simulate deferUpdate() scheduling a callback
        updateCallCount_ = 0;
    }

    // Simulates IDependent::update() - called on UI thread
    // Returns true if update was processed, false if skipped due to null editor
    bool update() {
        updateCallCount_++;

        // CRITICAL: Check if editor is null before accessing
        // This is the pattern that prevents the crash
        MockEditor* editor = editorPtr_ ? *editorPtr_ : nullptr;
        if (!editor) {
            return false;  // Early return - safe during destruction
        }

        // Would access editor here in real code
        // editor->getFrame()->...
        return editor->isValid();
    }

    int getUpdateCallCount() const { return updateCallCount_; }

private:
    MockEditor** editorPtr_;  // Pointer to controller's activeEditor_ member
    int updateCallCount_ = 0;
};

} // namespace

// =============================================================================
// Tests
// =============================================================================

TEST_CASE("VisibilityController pattern handles null editor safely",
          "[regression][controller][visibility]") {

    SECTION("Update returns early when editorPtr_ target is null") {
        // Simulate controller's activeEditor_ member
        MockEditor* activeEditor = nullptr;

        // Create visibility controller with pointer to member
        MockVisibilityController controller(&activeEditor);

        // Simulate deferred update firing when editor is null
        bool processed = controller.update();

        // Should return false (skipped) without crashing
        REQUIRE_FALSE(processed);
        REQUIRE(controller.getUpdateCallCount() == 1);
    }

    SECTION("Update returns early when editorPtr_ itself is null") {
        // Edge case: editorPtr_ could theoretically be null
        MockVisibilityController controller(nullptr);

        bool processed = controller.update();

        REQUIRE_FALSE(processed);
    }

    SECTION("Update processes when editor is valid") {
        MockEditor editor;
        MockEditor* activeEditor = &editor;

        MockVisibilityController controller(&activeEditor);

        bool processed = controller.update();

        REQUIRE(processed);
    }

    SECTION("Correct destruction order prevents crash - editor nulled first") {
        // This simulates the CORRECT order in willClose():
        // 1. activeEditor_ = nullptr  (FIRST)
        // 2. visibilityController_ = nullptr  (destroys controller)

        MockEditor editor;
        MockEditor* activeEditor = &editor;

        // Create controller
        auto* controller = new MockVisibilityController(&activeEditor);

        // CORRECT ORDER: Null editor first
        activeEditor = nullptr;

        // Now destroy controller - any pending update() would see null and return early
        bool processed = controller->update();  // Simulate pending callback
        REQUIRE_FALSE(processed);  // Should skip, not crash

        delete controller;

        SUCCEED();  // No crash = success
    }

    SECTION("Wrong destruction order would access invalid editor") {
        // This documents the BUG scenario (but we don't actually crash in test)
        // In the real bug:
        // 1. visibilityController_ = nullptr  (destroys controller)
        //    - During destruction, deferred update might fire
        //    - activeEditor_ is still non-null but editor is being destroyed
        //    - CRASH when accessing partially-destroyed editor
        // 2. activeEditor_ = nullptr  (TOO LATE)

        MockEditor editor;
        MockEditor* activeEditor = &editor;

        auto* controller = new MockVisibilityController(&activeEditor);

        // WRONG ORDER: Editor still appears valid during controller destruction
        // In real code, the editor might be partially destroyed at this point
        editor.invalidate();  // Simulate editor being in bad state

        bool processed = controller->update();

        // With wrong order, update() would try to use the invalid editor
        // This documents that we'd get a false positive (appears to work)
        // but in real code with partially-destroyed VSTGUI objects, this crashes
        REQUIRE_FALSE(processed);  // isValid() returns false

        delete controller;
        activeEditor = nullptr;  // Too late!
    }
}

TEST_CASE("Pointer-to-pointer pattern allows safe member updates",
          "[regression][controller][visibility]") {

    SECTION("Controller sees updated editor after reassignment") {
        MockEditor editor1;
        MockEditor editor2;
        MockEditor* activeEditor = &editor1;

        MockVisibilityController controller(&activeEditor);

        // First update sees editor1
        REQUIRE(controller.update());

        // Controller's activeEditor_ is reassigned (simulates didOpen with new editor)
        activeEditor = &editor2;

        // Controller sees the new editor through the pointer-to-pointer
        REQUIRE(controller.update());

        // Null the editor (simulates willClose)
        activeEditor = nullptr;

        // Controller sees null and returns early
        REQUIRE_FALSE(controller.update());
    }
}
