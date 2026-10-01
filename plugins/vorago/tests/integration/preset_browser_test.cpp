// ==============================================================================
// Vorago Phase 13 - preset browser save/load round trip (SC-023)
// ==============================================================================
// T007 (specs/vorago-phase13-ui/tasks.md; plan 5.4, 5.5; FR-070 - FR-073).
// Written RED: it does not compile until T014 adds presetBrowserViewForTest(),
// createComponentStateStream() and loadComponentStateWithEdits().
//
// The pair is REAL on both sides: a real Processor (ProcessorFixture) and a real
// Controller whose component handler records begin/perform/endEdit and answers
// IComponent by FORWARDING the query to the processor (the shape of
// plugins/seraphis/tests/integration/preset_load_test.cpp; the Innexus/Ruinae
// precedent: the host's handler is where the controller looks for IComponent).
//
//   (1) headless editor open -> the browser view exists; clicking the built
//       PresetBrowserButton (the full OutlineBrowserButton::onMouseDown
//       begin/value/end triple, outline_button.h:117-129) opens it and reaches
//       the host as 0 edits; closing the editor with the browser still open
//       nulls the view pointer (R-2: there is no save dialog).
//   (2) the state provider's stream is the processor's own getState bytes
//       (kStateV3Bytes, 436).
//   (3) all non-hidden IDs (106 + the Phase 14 roster) are randomized with a
//       seeded Xorshift32 (list IDs snapped to their steps), each to a value that
//       differs from the capture.
//   (4) the load provider restores the capture, telling the host exactly one
//       Begin/Perform/End triple per ID; a second controller fed the same stream
//       through setComponentState agrees.
//   (5) a future-version stream (4 since Phase 14) is rejected before any setter runs.
//
// NAMESPACE HAZARD (vorago_test_fixture.h): plugin types are spelled ::Vorago::.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "test_helpers/editor_lifecycle_harness.h"

#include "controller/controller.h"
#include "plugin_ids.h"
#include "vorago_test_fixture.h"

#include "ui/outline_button.h"
#include "ui/preset_browser_view.h"

#include <krate/dsp/core/random.h>

#include <vst_param_changes.h>

#include "pluginterfaces/base/smartpointer.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "vstgui/lib/cbuttonstate.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cpoint.h"
#include "vstgui/lib/cview.h"
#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/plugin-bindings/vst3editor.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

using Steinberg::Vst::ParamID;
using Steinberg::Vst::ParamValue;

constexpr std::size_t kNumRegistered = 108 + VoragoTest::kNumEcosystemRosterParams;
constexpr std::size_t kNumPersisted =
    106 + VoragoTest::kNumEcosystemRosterParams;  // all but 4 and 5 (hidden, FR-045 of Phase 12)
constexpr double kValueTolerance = 1e-9;  // the Phase 12 state_v2_test.cpp tolerance

// -----------------------------------------------------------------------------
// Component handler stub: records begin/perform/endEdit and forwards an
// IComponent query to the real processor. Stack-owned, so ref counts are inert.
// -----------------------------------------------------------------------------
struct EditRecord {
    enum class Action : std::uint8_t { Begin, Perform, End };
    Action action;
    ParamID id;
    ParamValue value;  // meaningful for Perform only
};

class RecordingComponentHandler final : public Steinberg::Vst::IComponentHandler {
public:
    std::vector<EditRecord> records;
    Steinberg::Vst::IComponent* componentTarget = nullptr;

    Steinberg::tresult PLUGIN_API beginEdit(ParamID id) override {
        records.push_back({.action = EditRecord::Action::Begin, .id = id, .value = 0.0});
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API performEdit(ParamID id, ParamValue value) override {
        records.push_back({.action = EditRecord::Action::Perform, .id = id, .value = value});
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API endEdit(ParamID id) override {
        records.push_back({.action = EditRecord::Action::End, .id = id, .value = 0.0});
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API restartComponent(Steinberg::int32 /*flags*/) override {
        return Steinberg::kResultOk;
    }

    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override {
        if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Vst::IComponentHandler::iid) ||
            Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::FUnknown::iid)) {
            *obj = static_cast<Steinberg::Vst::IComponentHandler*>(this);
            return Steinberg::kResultOk;
        }
        if (componentTarget != nullptr &&
            Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Vst::IComponent::iid)) {
            return componentTarget->queryInterface(iid, obj);
        }
        *obj = nullptr;
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }
};

/// The non-hidden registered parameters with their step counts, read from the
/// controller itself (never a hand-kept list).
struct PersistedParam {
    ParamID id;
    Steinberg::int32 stepCount;
};

[[nodiscard]] std::vector<PersistedParam> persistedParams(::Vorago::Controller& ctrl) {
    std::vector<PersistedParam> out;
    const Steinberg::int32 count = ctrl.getParameterCount();
    for (Steinberg::int32 i = 0; i < count; ++i) {
        Steinberg::Vst::ParameterInfo info{};
        REQUIRE(ctrl.getParameterInfo(i, info) == Steinberg::kResultOk);
        if ((info.flags & Steinberg::Vst::ParameterInfo::kIsHidden) != 0) {
            continue;
        }
        out.push_back({.id = info.id, .stepCount = info.stepCount});
    }
    return out;
}

[[nodiscard]] std::map<ParamID, ParamValue> snapshot(::Vorago::Controller& ctrl,
                                                     const std::vector<PersistedParam>& params) {
    std::map<ParamID, ParamValue> out;
    for (const PersistedParam& p : params) {
        out[p.id] = ctrl.getParamNormalized(p.id);
    }
    return out;
}

/// Every byte of a stream, read from position 0; leaves the stream at position 0.
[[nodiscard]] std::vector<char> streamBytes(Steinberg::IBStream* stream) {
    std::vector<char> bytes;
    Steinberg::int64 end = 0;
    REQUIRE(stream->seek(0, Steinberg::IBStream::kIBSeekEnd, &end) == Steinberg::kResultOk);
    REQUIRE(end >= 0);
    bytes.resize(static_cast<std::size_t>(end));
    REQUIRE(stream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr) == Steinberg::kResultOk);
    if (!bytes.empty()) {
        Steinberg::int32 read = 0;
        REQUIRE(stream->read(bytes.data(), static_cast<Steinberg::int32>(bytes.size()), &read) ==
                Steinberg::kResultOk);
        REQUIRE(std::cmp_equal(read, bytes.size()));
    }
    REQUIRE(stream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr) == Steinberg::kResultOk);
    return bytes;
}

/// A fresh MemoryStream holding `bytes`, positioned at 0.
[[nodiscard]] Steinberg::IPtr<Steinberg::MemoryStream> streamOf(const std::vector<char>& bytes) {
    auto stream = Steinberg::owned(new Steinberg::MemoryStream());
    std::vector<char> buf(bytes);  // IBStream::write takes a mutable buffer
    Steinberg::int32 written = 0;
    REQUIRE(stream->write(buf.data(),
                          static_cast<Steinberg::int32>(bytes.size()), &written) ==
            Steinberg::kResultOk);
    REQUIRE(std::cmp_equal(written, bytes.size()));
    REQUIRE(stream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr) == Steinberg::kResultOk);
    return stream;
}

/// A seeded draw in [0, 1], snapped to the parameter's steps for list IDs.
[[nodiscard]] ParamValue drawValue(Krate::DSP::Xorshift32& rng, Steinberg::int32 stepCount) {
    const double u = static_cast<double>(rng.nextUnipolar());
    if (stepCount <= 0) {
        return std::clamp(u, 0.0, 1.0);
    }
    const double steps = static_cast<double>(stepCount);
    return std::clamp(std::round(u * steps) / steps, 0.0, 1.0);
}

/// Every PresetBrowserButton instance outside the browser overlay (the overlay
/// builds its own buttons, which are not the header button).
void collectHeaderButtons(VSTGUI::CViewContainer* container,
                          std::vector<Krate::Plugins::OutlineBrowserButton*>& out) {
    if (container == nullptr) {
        return;
    }
    const std::uint32_t count = container->getNbViews();
    for (std::uint32_t i = 0; i < count; ++i) {
        VSTGUI::CView* view = container->getView(i);
        if (view == nullptr || dynamic_cast<Krate::Plugins::PresetBrowserView*>(view) != nullptr) {
            continue;
        }
        if (auto* button = dynamic_cast<Krate::Plugins::OutlineBrowserButton*>(view)) {
            out.push_back(button);
        }
        if (auto* child = dynamic_cast<VSTGUI::CViewContainer*>(view)) {
            collectHeaderButtons(child, out);
        }
    }
}

}  // namespace

TEST_CASE("Vorago_PresetBrowser_SaveLoadRoundTrip", "[vorago][integration][preset]") {
    // Declared before the controllers so they outlive them (inert ref counts).
    RecordingComponentHandler handler;
    Steinberg::Vst::HostApplication hostApp;

    VoragoTest::ProcessorFixture fx;

    auto ctrl = Steinberg::owned(new ::Vorago::Controller());
    REQUIRE(ctrl->initialize(&hostApp) == Steinberg::kResultOk);
    REQUIRE(ctrl->setComponentHandler(&handler) == Steinberg::kResultOk);
    handler.componentTarget = fx.proc.get();

    REQUIRE(std::cmp_equal(ctrl->getParameterCount(), kNumRegistered));
    const std::vector<PersistedParam> params = persistedParams(*ctrl);
    REQUIRE(params.size() == kNumPersisted);
    {
        std::set<ParamID> ids;
        for (const PersistedParam& p : params) {
            ids.insert(p.id);
        }
        REQUIRE(ids.size() == kNumPersisted);
        REQUIRE(!ids.contains(::Vorago::kSustainPedalId));
        REQUIRE(!ids.contains(::Vorago::kChannelPressureId));
    }

    // ------------------------------------------------------------------------
    // (1) The browser lives in the editor; the header button opens it.
    // ------------------------------------------------------------------------
    {
        Krate::TestSupport::ensureVstguiInitialized();
        const std::string uidescPath = std::string(VORAGO_RESOURCES_DIR) + "/editor.uidesc";
        auto* editor = new VSTGUI::VST3Editor(ctrl.get(), "editor", uidescPath.c_str());
        Steinberg::IPlugView* view = editor;
        REQUIRE(view->attached(nullptr, Krate::TestSupport::nativePlatformType()) ==
                Steinberg::kResultTrue);
        REQUIRE(editor->getFrame() != nullptr);

        const Krate::Plugins::PresetBrowserView* browser = ctrl->presetBrowserViewForTest();
        const bool browserBuilt = browser != nullptr;
        const bool closedAtOpen = browserBuilt && !browser->isOpen();

        std::vector<Krate::Plugins::OutlineBrowserButton*> buttons;
        collectHeaderButtons(editor->getFrame(), buttons);
        const std::size_t buttonCount = buttons.size();

        bool openedByClick = false;
        std::size_t editsFromClick = 0;
        if (browserBuilt && buttonCount == 1u) {
            handler.records.clear();
            VSTGUI::CPoint where = buttons.front()->getViewSize().getCenter();
            const VSTGUI::CButtonState left(VSTGUI::kLButton);
            buttons.front()->onMouseDown(where, left);
            openedByClick = ctrl->presetBrowserViewForTest() != nullptr &&
                            ctrl->presetBrowserViewForTest()->isOpen();
            editsFromClick = handler.records.size();
        }

        // Close with the browser still open (willClose must close, then null it).
        view->removed();
        view->release();
        const bool nulledAfterClose = ctrl->presetBrowserViewForTest() == nullptr;

        REQUIRE(browserBuilt);
        REQUIRE(closedAtOpen);
        REQUIRE(buttonCount == 1u);
        REQUIRE(openedByClick);
        REQUIRE(editsFromClick == 0u);  // session tags never reach the host
        REQUIRE(nulledAfterClose);
    }
    handler.records.clear();

    // ------------------------------------------------------------------------
    // Seed the processor with non-default values and mirror them into the
    // controller, so the capture is not a trivial all-defaults state.
    // ------------------------------------------------------------------------
    {
        Krate::DSP::Xorshift32 seedRng(0x5C023A01u);
        Krate::Test::ParameterChanges pc;
        for (const PersistedParam& p : params) {
            pc.addChange(p.id, drawValue(seedRng, p.stepCount));
        }
        REQUIRE(fx.processNoOutputs(&pc) == Steinberg::kResultOk);

        auto procState = Steinberg::owned(new Steinberg::MemoryStream());
        REQUIRE(fx.proc->getState(procState) == Steinberg::kResultOk);
        REQUIRE(procState->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr) ==
                Steinberg::kResultOk);
        REQUIRE(ctrl->setComponentState(procState) == Steinberg::kResultOk);
    }
    REQUIRE(handler.records.empty());  // setComponentState never talks to the host

    // ------------------------------------------------------------------------
    // (2) The state provider's stream IS the processor's own bytes.
    // ------------------------------------------------------------------------
    const std::map<ParamID, ParamValue> captured = snapshot(*ctrl, params);

    Steinberg::IPtr<Steinberg::IBStream> saved = Steinberg::owned(ctrl->createComponentStateStream());
    REQUIRE(saved != nullptr);
    const std::vector<char> savedBytes = streamBytes(saved);

    std::vector<char> directBytes;
    {
        auto direct = Steinberg::owned(new Steinberg::MemoryStream());
        REQUIRE(fx.proc->getState(direct) == Steinberg::kResultOk);
        directBytes = streamBytes(direct);
    }
    REQUIRE(savedBytes.size() == ::Vorago::kStateV3Bytes);
    REQUIRE(directBytes.size() == ::Vorago::kStateV3Bytes);
    REQUIRE(savedBytes == directBytes);

    // ------------------------------------------------------------------------
    // (3) Randomize every persisted ID to a value different from the capture.
    // ------------------------------------------------------------------------
    {
        Krate::DSP::Xorshift32 rng(0x5C023B02u);
        std::size_t changed = 0;
        for (const PersistedParam& p : params) {
            const ParamValue before = captured.at(p.id);
            bool differs = false;
            for (int attempt = 0; attempt < 64 && !differs; ++attempt) {
                REQUIRE(ctrl->setParamNormalized(p.id, drawValue(rng, p.stepCount)) ==
                        Steinberg::kResultOk);
                differs = std::fabs(ctrl->getParamNormalized(p.id) - before) > kValueTolerance;
            }
            INFO("id " << p.id << " could not be moved off its captured value");
            REQUIRE(differs);
            ++changed;
        }
        REQUIRE(changed == kNumPersisted);
    }
    REQUIRE(handler.records.empty());  // setParamNormalized never talks to the host

    // ------------------------------------------------------------------------
    // (4) The load provider restores the capture, one edit triple per ID.
    // ------------------------------------------------------------------------
    REQUIRE(saved->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr) == Steinberg::kResultOk);
    REQUIRE(ctrl->loadComponentStateWithEdits(saved));

    for (const PersistedParam& p : params) {
        INFO("id " << p.id);
        REQUIRE(std::fabs(ctrl->getParamNormalized(p.id) - captured.at(p.id)) <= kValueTolerance);
    }

    REQUIRE(handler.records.size() == 3u * kNumPersisted);
    {
        std::set<ParamID> edited;
        for (std::size_t i = 0; i + 2 < handler.records.size(); i += 3) {
            const EditRecord& b = handler.records[i];
            const EditRecord& perf = handler.records[i + 1];
            const EditRecord& e = handler.records[i + 2];
            INFO("triple " << i / 3 << " id " << b.id);
            REQUIRE(b.action == EditRecord::Action::Begin);
            REQUIRE(perf.action == EditRecord::Action::Perform);
            REQUIRE(e.action == EditRecord::Action::End);
            REQUIRE(perf.id == b.id);
            REQUIRE(e.id == b.id);
            // performEdit carries the value the controller stored for that ID.
            REQUIRE(perf.value == ctrl->getParamNormalized(b.id));
            REQUIRE(edited.insert(b.id).second);  // exactly one triple per ID
        }
        REQUIRE(edited.size() == kNumPersisted);
        for (const PersistedParam& p : params) {
            REQUIRE(edited.count(p.id) == 1u);
        }
        REQUIRE(!edited.contains(::Vorago::kSustainPedalId));
        REQUIRE(!edited.contains(::Vorago::kChannelPressureId));
    }

    // A second controller fed the same stream through setComponentState agrees.
    {
        auto second = Steinberg::owned(new ::Vorago::Controller());
        REQUIRE(second->initialize(&hostApp) == Steinberg::kResultOk);
        REQUIRE(saved->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr) == Steinberg::kResultOk);
        REQUIRE(second->setComponentState(saved) == Steinberg::kResultOk);
        for (const PersistedParam& p : params) {
            INFO("id " << p.id);
            REQUIRE(std::fabs(second->getParamNormalized(p.id) - ctrl->getParamNormalized(p.id)) <=
                    kValueTolerance);
        }
        REQUIRE(second->terminate() == Steinberg::kResultOk);
    }

    // ------------------------------------------------------------------------
    // (5) A future-version stream is rejected before any setter runs.
    // ------------------------------------------------------------------------
    {
        constexpr Steinberg::int32 kFutureVersion = ::Vorago::kCurrentStateVersion + 1;
        static_assert(kFutureVersion == 4, "SC-023 (5): the first future version (v3 is current)");

        // Move the controller off the capture first so "unchanged" is not
        // indistinguishable from "reloaded".
        Krate::DSP::Xorshift32 rng(0x5C023C03u);
        for (const PersistedParam& p : params) {
            REQUIRE(ctrl->setParamNormalized(p.id, drawValue(rng, p.stepCount)) ==
                    Steinberg::kResultOk);
        }
        const std::map<ParamID, ParamValue> before = snapshot(*ctrl, params);

        std::vector<char> futureBytes = savedBytes;
        REQUIRE(futureBytes.size() >= 4u);
        // The stream is little-endian (IBStreamer(state, kLittleEndian)).
        const auto v = static_cast<std::uint32_t>(kFutureVersion);
        futureBytes[0] = static_cast<char>(v & 0xFFu);
        futureBytes[1] = static_cast<char>((v >> 8) & 0xFFu);
        futureBytes[2] = static_cast<char>((v >> 16) & 0xFFu);
        futureBytes[3] = static_cast<char>((v >> 24) & 0xFFu);
        auto future = streamOf(futureBytes);

        handler.records.clear();
        REQUIRE_FALSE(ctrl->loadComponentStateWithEdits(future));
        REQUIRE(handler.records.empty());
        for (const PersistedParam& p : params) {
            INFO("id " << p.id);
            REQUIRE(ctrl->getParamNormalized(p.id) == before.at(p.id));
        }
    }

    handler.componentTarget = nullptr;
    REQUIRE(ctrl->terminate() == Steinberg::kResultOk);
}
