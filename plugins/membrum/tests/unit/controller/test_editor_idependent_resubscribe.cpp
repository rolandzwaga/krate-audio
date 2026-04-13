// ==============================================================================
// T092 -- IDependent resubscribe / balance tests (Phase 6)
// Spec: specs/141-membrum-phase6-ui/spec.md (SC-014 use-after-free safety;
//       FR-030 kUiModeId -> UIViewSwitchContainer; FR-040 kEditorSizeId
//       -> VST3Editor::exchangeView)
// ==============================================================================
//
// The editor's IDependent wiring lives in `MembrumEditorController`:
//   ctor   -> getParameterObject(kUiModeId)     ->  addDependent(this)
//             getParameterObject(kEditorSizeId) ->  addDependent(this)
//   dtor   -> removeDependent(this)                  (both params)
//
// A VST3 template swap (driven by kEditorSizeId) destroys the whole
// sub-controller stack and rebuilds it on the new template. If addDependent
// / removeDependent are not perfectly balanced, the Parameter ends up holding
// a stale IDependent* into a destroyed sub-controller and the next
// Parameter::changed() call is use-after-free (the SC-014 hazard).
//
// This file pins the contract directly with a SpyParameter that counts
// addDependent / removeDependent calls. A tolerant ASan run would also catch
// the bug at runtime, but these unit assertions fail loudly in Release,
// independent of sanitizer instrumentation.
// ==============================================================================

#include "ui/membrum_editor_controller.h"
#include "ui/editor_size_policy.h"
#include "ui/ui_mode.h"
#include "controller/controller.h"
#include "plugin_ids.h"

#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/vst/vsteditcontroller.h"

#include "base/source/fobject.h"
#include "pluginterfaces/base/ustring.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>

using namespace Membrum;
using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

// ------------------------------------------------------------------------------
// SpyParameter: VST3 Parameter subclass that records addDependent and
// removeDependent calls. Used to verify the editor sub-controller leaves the
// count at zero after destruction.
// ------------------------------------------------------------------------------
class SpyParameter : public Parameter
{
public:
    SpyParameter(ParamID id, const char* asciiTitle)
    {
        info.id              = id;
        info.stepCount       = 1;
        info.flags           = ParameterInfo::kCanAutomate | ParameterInfo::kIsList;
        info.defaultNormalizedValue = 0.0;
        info.unitId          = kRootUnitId;
        // Copy ASCII title into the UTF-16 info.title field.
        Steinberg::UString titleWrapper(info.title, str16BufferSize(String128));
        titleWrapper.fromAscii(asciiTitle);
    }

    void addDependent(IDependent* dep) override
    {
        ++addCount_;
        ++liveCount_;
        Parameter::addDependent(dep);
    }
    void removeDependent(IDependent* dep) override
    {
        ++removeCount_;
        --liveCount_;
        Parameter::removeDependent(dep);
    }

    [[nodiscard]] int addCount()    const noexcept { return addCount_; }
    [[nodiscard]] int removeCount() const noexcept { return removeCount_; }
    [[nodiscard]] int liveCount()   const noexcept { return liveCount_; }

private:
    int addCount_    = 0;
    int removeCount_ = 0;
    int liveCount_   = 0;
};

// ------------------------------------------------------------------------------
// Test-only EditController that exposes two SpyParameters (kUiModeId,
// kEditorSizeId). MembrumEditorController queries parameters via
// `EditController::getParameterObject(id)`, which dispatches through the
// `parameters` ParameterContainer -- this harness mirrors the production
// Controller's registration for those two IDs only.
// ------------------------------------------------------------------------------
class SpyEditController : public EditController
{
public:
    tresult PLUGIN_API initialize(FUnknown* context) override
    {
        const tresult r = EditController::initialize(context);
        if (r != kResultOk) return r;

        auto* uiMode     = new SpyParameter(kUiModeId,     "UiMode");
        auto* editorSize = new SpyParameter(kEditorSizeId, "EditorSize");
        parameters.addParameter(uiMode);
        parameters.addParameter(editorSize);
        uiModeSpy_     = uiMode;
        editorSizeSpy_ = editorSize;
        return kResultOk;
    }

    SpyParameter* uiModeSpy()     const noexcept { return uiModeSpy_; }
    SpyParameter* editorSizeSpy() const noexcept { return editorSizeSpy_; }

private:
    SpyParameter* uiModeSpy_     = nullptr;
    SpyParameter* editorSizeSpy_ = nullptr;
};

} // namespace

// ==============================================================================
// MembrumEditorController registers as IDependent on both session-scoped
// parameters in its ctor and deregisters in its dtor. The balance must be
// exactly 1 add / 1 remove per instance.
// ==============================================================================
TEST_CASE("MembrumEditorController registers on kUiModeId and kEditorSizeId (T092)",
          "[idependent_resubscribe]")
{
    SpyEditController ec;
    REQUIRE(ec.initialize(nullptr) == kResultOk);

    REQUIRE(ec.uiModeSpy()->addCount()    == 0);
    REQUIRE(ec.editorSizeSpy()->addCount() == 0);

    {
        UI::MembrumEditorController sub(/*editor*/ nullptr, &ec);
        // ctor subscribed to both.
        REQUIRE(ec.uiModeSpy()->addCount()     == 1);
        REQUIRE(ec.uiModeSpy()->liveCount()    == 1);
        REQUIRE(ec.editorSizeSpy()->addCount() == 1);
        REQUIRE(ec.editorSizeSpy()->liveCount() == 1);
    }
    // dtor must unsubscribe both -- liveCount must be 0.
    REQUIRE(ec.uiModeSpy()->removeCount()     == 1);
    REQUIRE(ec.uiModeSpy()->liveCount()       == 0);
    REQUIRE(ec.editorSizeSpy()->removeCount() == 1);
    REQUIRE(ec.editorSizeSpy()->liveCount()   == 0);

    ec.terminate();
}

// ==============================================================================
// Template-swap simulation: destroy + re-instantiate the sub-controller N
// times (mimics kEditorSizeId flip driving VST3Editor::exchangeView, which
// rebuilds the sub-controller stack on the new template). Every cycle must
// leave the live-dependent count at zero and the add/remove counters
// perfectly balanced.
// ==============================================================================
TEST_CASE("MembrumEditorController re-instantiation keeps IDependent balanced "
          "across 50 template swaps (T092)",
          "[idependent_resubscribe]")
{
    SpyEditController ec;
    REQUIRE(ec.initialize(nullptr) == kResultOk);

    constexpr int kSwaps = 50;
    for (int i = 0; i < kSwaps; ++i)
    {
        UI::MembrumEditorController sub(/*editor*/ nullptr, &ec);
        REQUIRE(ec.uiModeSpy()->liveCount()     == 1);
        REQUIRE(ec.editorSizeSpy()->liveCount() == 1);
    }

    // After all swaps, the live count must be zero -- no dangling dependents
    // and add / remove counters balanced.
    REQUIRE(ec.uiModeSpy()->addCount()        == kSwaps);
    REQUIRE(ec.uiModeSpy()->removeCount()     == kSwaps);
    REQUIRE(ec.uiModeSpy()->liveCount()       == 0);
    REQUIRE(ec.editorSizeSpy()->addCount()    == kSwaps);
    REQUIRE(ec.editorSizeSpy()->removeCount() == kSwaps);
    REQUIRE(ec.editorSizeSpy()->liveCount()   == 0);

    ec.terminate();
}

// ==============================================================================
// Parameter::changed() after the sub-controller is destroyed must NOT invoke
// a stale IDependent (the classic use-after-free signature). We cannot observe
// "what would have been dispatched" directly, but we CAN verify:
//   * liveCount == 0 after destruction -> Parameter's dependents list empty.
//   * Calling changed() after dtor does not crash and does not resurrect a
//     stale pointer.
// ==============================================================================
TEST_CASE("Parameter::changed() after sub-controller dtor is safe (T092)",
          "[idependent_resubscribe]")
{
    SpyEditController ec;
    REQUIRE(ec.initialize(nullptr) == kResultOk);

    {
        UI::MembrumEditorController sub(/*editor*/ nullptr, &ec);
        // While alive, changed() dispatches to one dependent (update() is a
        // no-op when editor_ / uiModeSwitch_ are null so it's safe).
        ec.uiModeSpy()->changed();
        ec.editorSizeSpy()->changed();
        REQUIRE(ec.uiModeSpy()->liveCount()     == 1);
        REQUIRE(ec.editorSizeSpy()->liveCount() == 1);
    }

    // Post-destruction -- both dependent lists must be empty; changed() is
    // safe to call.
    REQUIRE(ec.uiModeSpy()->liveCount()     == 0);
    REQUIRE(ec.editorSizeSpy()->liveCount() == 0);
    ec.uiModeSpy()->changed();
    ec.editorSizeSpy()->changed();

    ec.terminate();
}
