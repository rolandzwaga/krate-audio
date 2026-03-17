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

---

## DataExchange API (Processor → Controller Data Transfer)

### The Problem

Sending display/visualization data from `process()` to the controller via `IMessage` has two issues:

1. **Real-time safety**: `allocateMessage()` + `sendMessage()` allocates on the audio thread
2. **Host compatibility**: Some hosts (notably Reason) don't reliably deliver `IMessage` from `process()`

### The Solution: VST3 DataExchange API

The SDK (v3.7.9+) provides `DataExchangeHandler` / `DataExchangeReceiverHandler` — a purpose-built mechanism with automatic IMessage fallback for older hosts. Located in:

```
public.sdk/source/vst/utility/dataexchange.h   // Header
public.sdk/source/vst/utility/dataexchange.cpp  // Impl (part of sdk_common target)
pluginterfaces/vst/ivstdataexchange.h           // IDataExchangeReceiver interface
```

### Processor Side Pattern

```cpp
// processor.h
#include "public.sdk/source/vst/utility/dataexchange.h"

class Processor : public Steinberg::Vst::AudioEffect {
    // Override connect/disconnect for DataExchange lifecycle
    tresult PLUGIN_API connect(Vst::IConnectionPoint* other) override;
    tresult PLUGIN_API disconnect(Vst::IConnectionPoint* other) override;

private:
    std::unique_ptr<Vst::DataExchangeHandler> dataExchange_;
    DisplayData displayDataBuffer_{};  // Must be trivially copyable!
};
```

```cpp
// processor.cpp — connect/disconnect
tresult PLUGIN_API Processor::connect(Vst::IConnectionPoint* other) {
    auto result = AudioEffect::connect(other);
    if (result == kResultTrue) {
        auto configCallback = [](Vst::DataExchangeHandler::Config& config,
                                 const Vst::ProcessSetup& /*setup*/) {
            config.blockSize = static_cast<uint32>(sizeof(DisplayData));
            config.numBlocks = 2;       // Double-buffer
            config.alignment = 32;      // Cache-line friendly
            config.userContextID = 0;
            return true;
        };
        dataExchange_ = std::make_unique<Vst::DataExchangeHandler>(
            this, configCallback);
        dataExchange_->onConnect(other, getHostContext());
    }
    return result;
}

tresult PLUGIN_API Processor::disconnect(Vst::IConnectionPoint* other) {
    if (dataExchange_) {
        dataExchange_->onDisconnect(other);
        dataExchange_.reset();
    }
    return AudioEffect::disconnect(other);
}

// processor.cpp — setActive
tresult PLUGIN_API Processor::setActive(TBool state) {
    if (state) {
        // ... other activation ...
        if (dataExchange_) dataExchange_->onActivate(processSetup);
    } else {
        if (dataExchange_) dataExchange_->onDeactivate();
        // ... other deactivation ...
    }
    return AudioEffect::setActive(state);
}

// processor.cpp — sendDisplayData (called from process())
void Processor::sendDisplayData() {
    // ... populate displayDataBuffer_ ...

    if (dataExchange_) {
        auto block = dataExchange_->getCurrentOrNewBlock();
        if (block.blockID != Vst::InvalidDataExchangeBlockID && block.data) {
            std::memcpy(block.data, &displayDataBuffer_, sizeof(DisplayData));
            dataExchange_->sendCurrentBlock();
        }
    }
}
```

### Controller Side Pattern

```cpp
// controller.h
#include "public.sdk/source/vst/utility/dataexchange.h"
#include "pluginterfaces/vst/ivstdataexchange.h"

class Controller : public Vst::EditControllerEx1,
                   public VSTGUI::VST3EditorDelegate,
                   public Vst::IDataExchangeReceiver  // Add this
{
public:
    // IDataExchangeReceiver
    void PLUGIN_API queueOpened(Vst::DataExchangeUserContextID userContextID,
                                uint32 blockSize,
                                TBool& dispatchOnBackgroundThread) override;
    void PLUGIN_API queueClosed(
        Vst::DataExchangeUserContextID userContextID) override;
    void PLUGIN_API onDataExchangeBlocksReceived(
        Vst::DataExchangeUserContextID userContextID,
        uint32 numBlocks,
        Vst::DataExchangeBlock* blocks,
        TBool onBackgroundThread) override;

    // REQUIRED: Interface macros to expose IDataExchangeReceiver
    OBJ_METHODS(Controller, EditControllerEx1)
    DEFINE_INTERFACES
        DEF_INTERFACE(Vst::IDataExchangeReceiver)
    END_DEFINE_INTERFACES(EditControllerEx1)
    DELEGATE_REFCOUNT(EditControllerEx1)

private:
    DisplayData cachedDisplayData_{};
    Vst::DataExchangeReceiverHandler dataExchangeReceiver_{this};
};
```

```cpp
// controller.cpp
void PLUGIN_API Controller::queueOpened(
    Vst::DataExchangeUserContextID, uint32, TBool& dispatchOnBackgroundThread) {
    dispatchOnBackgroundThread = false;  // UI thread — safe with timer pattern
}

void PLUGIN_API Controller::queueClosed(Vst::DataExchangeUserContextID) {}

void PLUGIN_API Controller::onDataExchangeBlocksReceived(
    Vst::DataExchangeUserContextID, uint32 numBlocks,
    Vst::DataExchangeBlock* blocks, TBool) {
    // Use latest block (most recent data)
    for (uint32 i = 0; i < numBlocks; ++i) {
        if (blocks[i].data && blocks[i].size >= sizeof(DisplayData))
            std::memcpy(&cachedDisplayData_, blocks[i].data, sizeof(DisplayData));
    }
}

tresult PLUGIN_API Controller::notify(Vst::IMessage* message) {
    if (!message) return kInvalidArgument;

    // DataExchange fallback: handles IMessage-encoded blocks transparently
    if (dataExchangeReceiver_.onMessage(message))
        return kResultOk;

    // ... other message handling (SampleFileLoaded, etc.) ...

    return EditControllerEx1::notify(message);
}
```

### Critical Implementation Notes

| Rule | Detail |
|------|--------|
| **DisplayData must be trivially copyable** | `static_assert(std::is_trivially_copyable_v<DisplayData>)` — required for `memcpy` block transport |
| **Null-guard `dataExchange_`** | Tests and hosts that don't call `connect()` must not crash — `if (dataExchange_)` before every call |
| **DEFINE_INTERFACES chaining** | `END_DEFINE_INTERFACES(EditControllerEx1)` chains through the parent's `queryInterface` — this is required for `IUnitInfo` to remain discoverable |
| **`dispatchOnBackgroundThread = false`** | Keeps `onDataExchangeBlocksReceived` on UI thread, safe with the existing timer + cached data pattern |
| **Existing timer pattern preserved** | The 30ms `CVSTGUITimer` continues to drive view updates from `cachedDisplayData_` — no change to the view refresh path |
| **IMessage fallback is automatic** | When host doesn't support `IDataExchangeHandler`, the SDK transparently falls back to IMessage via a 1ms timer. No plugin code changes needed. |
| **`onActivate` before `setActive` returns** | Call `dataExchange_->onActivate(processSetup)` at the END of the `setActive(true)` block, after all other setup |
| **`onDeactivate` before `setActive` base call** | Call `dataExchange_->onDeactivate()` at the START of the `setActive(false)` block, before resetting other state |

### Testing DataExchange

The IMessage fallback uses a platform timer (1ms interval) to deliver queued blocks. In tests:

- **Unit tests (no connect)**: `sendDisplayData()` is a no-op when `dataExchange_` is null — existing tests work unchanged
- **Unit tests (direct receiver)**: Call `onDataExchangeBlocksReceived()` directly with a mock `DataExchangeBlock` pointing to test data
- **Integration tests**: Wire Processor ↔ Controller via `IConnectionPoint`, then pump platform messages to allow the fallback timer to fire:

```cpp
// Windows: SetTimer requires message dispatching
static void pumpMessages(int durationMs) {
#ifdef _WIN32
    auto start = std::chrono::steady_clock::now();
    while (/* elapsed < durationMs */) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
#else
    std::this_thread::sleep_for(std::chrono::milliseconds(durationMs));
#endif
}
```

### SDK Source Reference

| File | Key Content |
|------|-------------|
| `dataexchange.cpp:44-49` | Message IDs: `"DataExchange"`, `"DataExchangeQueueOpened"`, `"DataExchangeQueueClosed"` |
| `dataexchange.cpp:52-192` | `MessageHandler` — fallback IMessage implementation with ring buffers and 1ms timer |
| `dataexchange.cpp:395-463` | `DataExchangeReceiverHandler::onMessage()` — decodes fallback messages, calls `onDataExchangeBlocksReceived()` |
| `dataexchange.h:40-123` | `DataExchangeHandler` public API |
| `dataexchange.h:135-150` | `DataExchangeReceiverHandler` public API |
