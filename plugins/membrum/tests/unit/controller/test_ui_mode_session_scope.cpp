// ==============================================================================
// Session-scope tests for kUiModeId (Phase 6, T021)
// Spec: specs/141-membrum-phase6-ui/spec.md (FR-030, FR-033, FR-081)
// ==============================================================================
//
// kUiModeId must be:
//   - Registered as a StringListParameter { "Acoustic", "Extended" }, default Acoustic
//   - Automatable (responds to setParamNormalized)
//   - NOT written to IBStream by Processor::getState
//   - Reset to Acoustic on Controller::setComponentState() regardless of blob content
// ==============================================================================

#include "controller/controller.h"
#include "processor/processor.h"
#include "plugin_ids.h"
#include "ui/ui_mode.h"

#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <catch2/catch_test_macros.hpp>

using namespace Membrum;

namespace {

// Read all bytes from a MemoryStream into a vector for inspection.
std::vector<uint8_t> drainStream(Steinberg::MemoryStream& ms)
{
    Steinberg::int64 pos = 0;
    ms.tell(&pos);
    const auto size = static_cast<std::size_t>(pos);
    ms.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    std::vector<uint8_t> out(size);
    if (size > 0) {
        Steinberg::int32 got = 0;
        ms.read(out.data(), static_cast<Steinberg::int32>(size), &got);
    }
    return out;
}

} // namespace

TEST_CASE("kUiModeId registered as StringListParameter (Acoustic/Extended)",
          "[ui_mode_session]")
{
    Controller ctl;
    REQUIRE(ctl.initialize(nullptr) == Steinberg::kResultOk);

    auto* param = ctl.getParameterObject(kUiModeId);
    REQUIRE(param != nullptr);

    const auto& info = param->getInfo();
    REQUIRE(info.stepCount == 1);  // 2 choices -> stepCount 1
    REQUIRE((info.flags & Steinberg::Vst::ParameterInfo::kCanAutomate) != 0);
    REQUIRE((info.flags & Steinberg::Vst::ParameterInfo::kIsList) != 0);

    // Default normalized value = 0 -> Acoustic
    REQUIRE(info.defaultNormalizedValue == 0.0);

    ctl.terminate();
}

TEST_CASE("kUiModeId responds to setParamNormalized (automatable)",
          "[ui_mode_session]")
{
    Controller ctl;
    REQUIRE(ctl.initialize(nullptr) == Steinberg::kResultOk);

    REQUIRE(ctl.setParamNormalized(kUiModeId, 1.0) == Steinberg::kResultOk);
    REQUIRE(ctl.getParamNormalized(kUiModeId) == 1.0);
    REQUIRE(UI::uiModeFromNormalized(1.0f) == UI::UiMode::Advanced);

    REQUIRE(ctl.setParamNormalized(kUiModeId, 0.0) == Steinberg::kResultOk);
    REQUIRE(ctl.getParamNormalized(kUiModeId) == 0.0);

    ctl.terminate();
}

// ----------------------------------------------------------------------------
// T021: setState always resets kUiModeId to Acoustic regardless of blob content.
// ----------------------------------------------------------------------------
TEST_CASE("Controller::setComponentState resets kUiModeId to Acoustic (T021)",
          "[ui_mode_session]")
{
    Controller ctl;
    REQUIRE(ctl.initialize(nullptr) == Steinberg::kResultOk);

    // Prime the controller into Extended so we can prove the reset happens.
    REQUIRE(ctl.setParamNormalized(kUiModeId, 1.0) == Steinberg::kResultOk);
    REQUIRE(ctl.getParamNormalized(kUiModeId) == 1.0);

    // Build a minimal v6 state blob by producing one from a fresh Processor.
    Processor p;
    REQUIRE(p.initialize(nullptr) == Steinberg::kResultOk);
    Steinberg::MemoryStream blob;
    REQUIRE(p.getState(&blob) == Steinberg::kResultOk);
    blob.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);

    // setComponentState must unconditionally reset kUiModeId.
    REQUIRE(ctl.setComponentState(&blob) == Steinberg::kResultOk);
    REQUIRE(ctl.getParamNormalized(kUiModeId) == 0.0);

    p.terminate();
    ctl.terminate();
}

// ----------------------------------------------------------------------------
// T021: kit preset load with "uiMode":"Extended" drives kUiModeId to Extended.
// Here we exercise the Controller-visible preset-load code path by simulating
// the callback: the preset code calls setParamNormalized(kUiModeId, 1.0) when
// the JSON contains "uiMode":"Extended". Full JSON wiring lives in Phase 5 /
// T055; this test pins the contract the callback must satisfy.
// ----------------------------------------------------------------------------
TEST_CASE("Kit preset uiMode=Extended triggers kUiModeId change via callback (T021)",
          "[ui_mode_session]")
{
    Controller ctl;
    REQUIRE(ctl.initialize(nullptr) == Steinberg::kResultOk);

    // Simulated preset-load callback behaviour (Phase 5 T055 will call
    // setParamNormalized on the UI thread when the JSON has "uiMode":"Extended").
    auto presetLoadCallback = [&ctl](const std::string& uiModeValue) {
        if (uiModeValue == "Extended")
            ctl.setParamNormalized(kUiModeId, 1.0);
        else if (uiModeValue == "Acoustic")
            ctl.setParamNormalized(kUiModeId, 0.0);
    };

    presetLoadCallback("Extended");
    REQUIRE(ctl.getParamNormalized(kUiModeId) == 1.0);
    presetLoadCallback("Acoustic");
    REQUIRE(ctl.getParamNormalized(kUiModeId) == 0.0);

    ctl.terminate();
}

TEST_CASE("Processor::getState does NOT write kUiModeId bytes", "[ui_mode_session]")
{
    Processor p;
    REQUIRE(p.initialize(nullptr) == Steinberg::kResultOk);

    Steinberg::MemoryStream ms;
    REQUIRE(p.getState(&ms) == Steinberg::kResultOk);
    auto bytes = drainStream(ms);

    // kUiModeId is session-scoped and must NOT appear in the processor state
    // blob. Verify total size matches the expected v8 layout with zero
    // session tail (processor getState always emits hasSession=false).
    //
    // Phase 6 v6 layout = 10610 bytes (9330 base + 1280 macros).
    // Phase 7 v7 extends the per-pad sound array from 34 to 42 float64 slots,
    // adding 32 * 8 * 8 = 2048 bytes.
    // Phase 8A v8 adds 2 more sound slots per pad,
    // adding 32 * 2 * 8 = 512 bytes.
    // Phase 8C v9 adds 2 more sound slots per pad,
    // adding 32 * 2 * 8 = 512 bytes.
    // Phase 8D v10 adds 4 more sound slots per pad,
    // adding 32 * 4 * 8 = 1024 bytes.
    // Phase 8E v11 adds 1 more sound slot per pad,
    // adding 32 * 1 * 8 = 256 bytes.
    // Phase 8F v12 adds 1 more sound slot per pad,
    // adding 32 * 1 * 8 = 256 bytes.
    // Phase 9 v13 appends 1 master-gain float64 (8 bytes) at the end of the blob.
    // v14 drops the 2-byte override count (matrix removal).
    // If kUiModeId had been appended as an int32 it would be +4.
    REQUIRE(bytes.size() == std::size_t{10610 + 2048 + 512 + 512 + 1024 + 256 + 256 + 8 - 2});

    p.terminate();
}

// ==============================================================================
// T096: kUiModeId host automation from a non-UI thread. The controller API
// must be safe to call off-thread, and the param read back must reflect the
// write. In production, VSTGUI's IDependent mechanism defers the repaint
// onto the UI thread; we cannot observe repaints headless, but we CAN:
//   * Write kUiModeId from a worker std::thread.
//   * Read kUiModeId back from the main thread and verify it matches.
//   * Confirm no crash and no other parameter drift.
// ==============================================================================
#include "base/source/fobject.h"

#include <atomic>
#include <thread>
#include <vector>

namespace {

// SpyDependent: records update() calls with the thread id each was made on.
// Installed on a Parameter via addDependent(); Parameter::setNormalized calls
// Parameter::changed() which synchronously dispatches to update().
class SpyDependent : public Steinberg::FObject
{
public:
    void PLUGIN_API update(FUnknown* /*changed*/, Steinberg::int32 message) override
    {
        if (message != Steinberg::FObject::kChanged)
            return;
        updateCount_.fetch_add(1, std::memory_order_acq_rel);
        lastThreadId_ = std::this_thread::get_id();
    }

    int updateCount() const noexcept
    {
        return updateCount_.load(std::memory_order_acquire);
    }
    std::thread::id lastThreadId() const noexcept { return lastThreadId_; }

private:
    std::atomic<int> updateCount_{0};
    std::thread::id  lastThreadId_{};
};

} // namespace

TEST_CASE("kUiModeId host automation from non-UI thread is thread-safe (T096)",
          "[ui_mode_session]")
{
    Controller ctl;
    REQUIRE(ctl.initialize(nullptr) == Steinberg::kResultOk);

    // Snapshot the pre-automation state of all other params so we can verify
    // no drift (hidden-params-must-still-be-reachable guarantee).
    const int paramCount = ctl.getParameterCount();
    struct Snap { Steinberg::Vst::ParamID id; Steinberg::Vst::ParamValue value; };
    std::vector<Snap> before;
    before.reserve(static_cast<std::size_t>(paramCount));
    for (int i = 0; i < paramCount; ++i) {
        Steinberg::Vst::ParameterInfo info{};
        REQUIRE(ctl.getParameterInfo(i, info) == Steinberg::kResultOk);
        if (info.id == kUiModeId) continue;
        before.push_back({ info.id, ctl.getParamNormalized(info.id) });
    }

    std::atomic<bool> done{false};
    std::thread automationThread([&] {
        for (int i = 0; i < 200; ++i) {
            const double v = (i & 1) ? 1.0 : 0.0;
            ctl.setParamNormalized(kUiModeId, v);
        }
        done.store(true, std::memory_order_release);
    });
    automationThread.join();
    REQUIRE(done.load(std::memory_order_acquire));

    // Final value is well-defined (last write wins; automationThread ended on
    // i=199 -> v=1.0).
    REQUIRE(ctl.getParamNormalized(kUiModeId) == 1.0);

    // All other params unchanged -- mode toggling from any thread must not
    // mutate neighbouring state (SC-003 guarantee).
    for (const auto& s : before) {
        REQUIRE(ctl.getParamNormalized(s.id) == s.value);
    }

    ctl.terminate();
}

// ==============================================================================
// T096 (SpyDependent): explicit IDependent observation of kUiModeId changes
// driven from a worker thread.
//
// Parameter::setNormalized() calls changed() which synchronously dispatches
// to every registered IDependent. In production the MembrumEditorController
// dependent forwards the change onto VSTGUI's UI thread via a
// UIViewSwitchContainer::setCurrentViewIndex() call (plus a CVSTGUITimer
// for exchangeView). The test harness cannot observe the VSTGUI deferral
// because there is no UI thread, but it CAN pin the invariants that the
// deferral path relies on:
//
//   1. Every setParamNormalized from a non-UI thread MUST fire update() on
//      a registered IDependent exactly once (toggled values only -- no
//      update() when value is unchanged).
//   2. update() runs synchronously on the thread that called
//      setParamNormalized (the switch-to-UI-thread is VSTGUI's
//      responsibility, not the Parameter's).
//   3. The sequence is well-defined: N toggles yield exactly N update()
//      calls, none dropped, none extra.
//
// This covers the "simulated non-UI thread" automation path that the spec
// T096 calls out as the UI-thread deferral precondition.
// ==============================================================================
TEST_CASE("kUiModeId change from non-UI thread dispatches IDependent::update "
          "on the calling thread (T096)",
          "[ui_mode_session]")
{
    Controller ctl;
    REQUIRE(ctl.initialize(nullptr) == Steinberg::kResultOk);

    auto* param = ctl.getParameterObject(kUiModeId);
    REQUIRE(param != nullptr);

    SpyDependent spy;
    param->addDependent(&spy);

    const std::thread::id mainId = std::this_thread::get_id();

    // Drive 50 toggles (1.0 <-> 0.0) from a worker thread. The parameter
    // default is 0.0 so we start by writing 1.0; every subsequent toggle
    // flips the stored value and therefore fires changed() -> update() once.
    std::atomic<std::thread::id> workerId{};
    std::thread worker([&] {
        workerId.store(std::this_thread::get_id(), std::memory_order_release);
        for (int i = 0; i < 50; ++i) {
            const double v = (i & 1) ? 0.0 : 1.0;
            ctl.setParamNormalized(kUiModeId, v);
        }
    });
    worker.join();

    // Exactly 50 update() calls recorded (each toggle is a real value change;
    // first write is 1.0 against default 0.0, alternates thereafter).
    REQUIRE(spy.updateCount() == 50);

    // update() ran on the worker thread (the thread that invoked
    // setParamNormalized) -- NOT on main. This pins the contract that
    // VSTGUI's UI-thread deferral is the sub-controller's responsibility.
    const auto wId = workerId.load(std::memory_order_acquire);
    REQUIRE(wId != mainId);
    REQUIRE(spy.lastThreadId() == wId);

    // Setting the same value again does NOT fire update() (short-circuit
    // in Parameter::setNormalized when the new value equals the old).
    const int beforeNoop = spy.updateCount();
    const double currentValue = param->getNormalized();
    ctl.setParamNormalized(kUiModeId, currentValue);
    REQUIRE(spy.updateCount() == beforeNoop);

    param->removeDependent(&spy);
    ctl.terminate();
}
