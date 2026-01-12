# Thread Safety and Editor Lifecycle

## The Problem

**CRITICAL**: `setParamNormalized()` can be called from **ANY thread**:
- User interaction → UI thread
- Automation → host thread (could be audio thread!)
- State loading → background thread
- Host sync → any thread

**VSTGUI controls MUST only be manipulated on the UI thread.** Calling `setVisible()` or any other control method from a non-UI thread causes:
- Race conditions
- Host hangs and crashes
- Undefined behavior

---

## The WRONG Pattern (Thread Safety Violation)

```cpp
// BROKEN - DO NOT DO THIS
Steinberg::tresult PLUGIN_API Controller::setParamNormalized(
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value) {

    auto result = EditControllerEx1::setParamNormalized(id, value);

    // Thread safety violation - setParamNormalized can be called from any thread!
    if (id == kTimeModeId) {
        bool shouldBeVisible = (value < 0.5f);
        delayTimeControl_->setVisible(shouldBeVisible);  // CRASH!
    }

    return result;
}
```

**Why This Crashes:**
1. Automation writes parameter changes → calls `setParamNormalized()` on audio thread
2. Audio thread calls `setVisible()` on VSTGUI control
3. UI thread is simultaneously rendering the control
4. Race condition → crash or hang

---

## The CORRECT Pattern (IDependent + Deferred Updates)

Use the `IDependent` mechanism with `UpdateHandler` for automatic thread-safe UI updates:

```cpp
class VisibilityController : public Steinberg::FObject {
public:
    VisibilityController(
        Steinberg::Vst::EditController* editController,
        Steinberg::Vst::Parameter* timeModeParam,
        VSTGUI::CControl* delayTimeControl)
    : editController_(editController)
    , timeModeParam_(timeModeParam)
    , delayTimeControl_(delayTimeControl)
    {
        if (timeModeParam_) {
            timeModeParam_->addRef();
            timeModeParam_->addDependent(this);  // Register for notifications
            timeModeParam_->deferUpdate();       // Trigger initial update on UI thread
        }
        if (delayTimeControl_) {
            delayTimeControl_->remember();
        }
    }

    ~VisibilityController() override {
        deactivate();
        if (watchedParam_) {
            watchedParam_->release();
            watchedParam_ = nullptr;
        }
    }

    // CRITICAL: Must be called BEFORE destruction
    void deactivate() {
        if (isActive_.exchange(false, std::memory_order_acq_rel)) {
            if (watchedParam_) {
                watchedParam_->removeDependent(this);
            }
        }
    }

    // IDependent::update - AUTOMATICALLY called on UI thread!
    void PLUGIN_API update(Steinberg::FUnknown* changedUnknown, Steinberg::int32 message) override {
        if (!isActive_.load(std::memory_order_acquire)) return;

        if (message == IDependent::kChanged && timeModeParam_ && delayTimeControl_) {
            float normalizedValue = timeModeParam_->getNormalized();
            bool shouldBeVisible = (normalizedValue < 0.5f);

            // SAFE: This is called on UI thread via UpdateHandler
            delayTimeControl_->setVisible(shouldBeVisible);

            if (delayTimeControl_->getFrame()) {
                delayTimeControl_->invalid();
            }
        }
    }

    OBJ_METHODS(VisibilityController, FObject)

private:
    std::atomic<bool> isActive_{true};
    Steinberg::Vst::EditController* editController_;
    Steinberg::Vst::Parameter* timeModeParam_;
    VSTGUI::CControl* delayTimeControl_;
};
```

---

## How It Works

1. **Parameter Registration**: `timeModeParam_->addDependent(this)` registers for change notifications

2. **Deferred Updates**: When the parameter changes (from ANY thread), it calls `deferUpdate()` internally, scheduling notification on UI thread

3. **UI Thread Callback**: `update()` is called on UI thread at 30Hz by `CVSTGUITimer`

4. **Safe UI Manipulation**: Since `update()` runs on UI thread, `setVisible()` is safe

---

## Integration in Controller

```cpp
// In controller.h
class Controller : public Steinberg::Vst::EditControllerEx1,
                   public VSTGUI::VST3EditorDelegate {
private:
    Steinberg::IPtr<Steinberg::FObject> digitalVisibilityController_;
    Steinberg::IPtr<Steinberg::FObject> pingPongVisibilityController_;
};

// In controller.cpp - didOpen()
void Controller::didOpen(VSTGUI::VST3Editor* editor) {
    activeEditor_ = editor;

    if (editor) {
        if (auto* frame = editor->getFrame()) {
            auto findControl = [frame](int32_t tag) -> VSTGUI::CControl* {
                // ... traversal code ...
            };

            // Create visibility controllers
            if (auto* digitalDelayTime = findControl(kDigitalDelayTimeId)) {
                if (auto* digitalTimeMode = getParameterObject(kDigitalTimeModeId)) {
                    digitalVisibilityController_ = new VisibilityController(
                        this, digitalTimeMode, digitalDelayTime);
                }
            }
        }
    }
}

// In controller.cpp - willClose()
void Controller::willClose(VSTGUI::VST3Editor* editor) {
    (void)editor;

    // PHASE 1: Deactivate ALL controllers FIRST
    deactivateController(digitalVisibilityController_);
    deactivateController(pingPongVisibilityController_);

    // PHASE 2: Clear the editor pointer
    activeEditor_ = nullptr;

    // PHASE 3: Destroy controllers
    digitalVisibilityController_ = nullptr;
    pingPongVisibilityController_ = nullptr;
}

static void deactivateController(Steinberg::IPtr<Steinberg::FObject>& controller) {
    if (controller) {
        if (auto* vc = dynamic_cast<VisibilityController*>(controller.get())) {
            vc->deactivate();
        }
    }
}
```

---

## Editor Lifecycle and deferUpdate Race Condition

### The Problem

The `deferUpdate()` mechanism queues updates to be delivered later. If the editor closes before the update fires, the callback invokes on a **destroyed object**.

**The Dangerous Sequence:**
1. `VisibilityController` constructor calls `deferUpdate()` → update is queued
2. User closes editor quickly (before update fires)
3. `willClose()` destroys the VisibilityController
4. Destructor calls `removeDependent(this)` → **TOO LATE**
5. The queued update fires → calls `update()` on destroyed object → **CRASH**

### The Solution

Call `removeDependent()` in `deactivate()`, not the destructor:

```cpp
void deactivate() {
    // Use exchange to ensure we only do this once (idempotent)
    if (isActive_.exchange(false, std::memory_order_acq_rel)) {
        if (watchedParam_) {
            watchedParam_->removeDependent(this);  // Stop updates BEFORE destruction
        }
    }
}
```

### willClose() Order of Operations

| Phase | Action | Effect |
|-------|--------|--------|
| 1 | `deactivate()` | Removes dependent, stops receiving updates |
| 2 | `activeEditor_ = nullptr` | Any in-flight update safely returns early |
| 3 | Destroy controllers | Object destroyed, no race condition |

---

## CViewContainer Child Destruction Order

**Critical Rule:** Child views are destroyed BEFORE the parent's destructor runs.

VSTGUI's `CViewContainer` destroys children via `beforeDelete()` → `removeAll()` before the destructor:

```cpp
// WRONG - Crashes on editor close
class MyContainer : public CViewContainer {
    CTextEdit* textField_ = nullptr;  // Child added via addView()

    ~MyContainer() {
        // BUG: textField_ was already destroyed!
        if (textField_) {
            textField_->unregisterTextEditListener(this);  // USE-AFTER-FREE!
        }
    }
};

// CORRECT - Safe for editor close
class MyContainer : public CViewContainer {
    CTextEdit* textField_ = nullptr;

    ~MyContainer() {
        // Do NOT access child views here - they're already gone
        // Only cleanup non-child-view resources
    }
};
```

### What Can You Do in Destructor?

| Action | Safe? | Why |
|--------|-------|-----|
| Unregister from child views | **NO** | Already destroyed by `removeAll()` |
| Unregister keyboard hook from frame | Yes | Frame outlives us |
| Delete objects we own directly | Yes | We created them, we destroy them |
| Stop timers we own | Yes | Timer is not a child view |
| Set `activeEditor_ = nullptr` | Yes | Raw pointer, no method calls |

---

## Testing Thread Safety

To verify your implementation is safe:

1. **Rapid Open/Close Test**: Open editor, immediately close (< 100ms), repeat 50+ times
2. **Close During Mode Switch**: Change a visibility-controlling parameter, immediately close editor
3. **Automation During Close**: Play automation while rapidly opening/closing editor
4. **AddressSanitizer Test**: Build with ASan enabled:
   ```bash
   cmake -B build-asan -DENABLE_ASAN=ON
   cmake --build build-asan --config Debug
   ```

---

## Summary: IDependent Lifecycle Rules

1. **Constructor**: Call `addDependent()` and optionally `deferUpdate()` for initial sync
2. **deactivate()**: Call `removeDependent()` here, not in destructor
3. **willClose()**: Call `deactivate()` on ALL controllers BEFORE destroying them
4. **Destructor**: Only release references, deactivation already done
5. **update()**: Check `isActive_` first, then check for valid editor
