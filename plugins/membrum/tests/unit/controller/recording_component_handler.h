#pragma once

// ==============================================================================
// RecordingComponentHandler -- a host's IComponentHandler for controller tests
// ==============================================================================
// Records every performEdit the controller sends (so a test can forward them to
// the processor as a host would), the parameter IDs of the beginEdit / endEdit
// gesture brackets, the restartComponent flag words and the setDirty calls.
// Owned by the test: reference counting is a no-op.
// ==============================================================================

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"

#include <utility>
#include <vector>

namespace Membrum::TestSupport {

class RecordingComponentHandler final : public Steinberg::Vst::IComponentHandler,
                                        public Steinberg::Vst::IComponentHandler2
{
public:
    std::vector<std::pair<Steinberg::Vst::ParamID, Steinberg::Vst::ParamValue>> edits;
    std::vector<Steinberg::Vst::ParamID> beginEditIds;
    std::vector<Steinberg::Vst::ParamID> endEditIds;
    int beginEdits = 0;
    int endEdits = 0;
    std::vector<Steinberg::int32> restartFlags;
    std::vector<bool> dirtyCalls;

    Steinberg::tresult PLUGIN_API beginEdit(Steinberg::Vst::ParamID id) override
    {
        ++beginEdits;
        beginEditIds.push_back(id);
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API performEdit(Steinberg::Vst::ParamID id,
                                              Steinberg::Vst::ParamValue value) override
    {
        edits.emplace_back(id, value);
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API endEdit(Steinberg::Vst::ParamID id) override
    {
        ++endEdits;
        endEditIds.push_back(id);
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API restartComponent(Steinberg::int32 flags) override
    {
        restartFlags.push_back(flags);
        return Steinberg::kResultOk;
    }

    // IComponentHandler2
    Steinberg::tresult PLUGIN_API setDirty(Steinberg::TBool state) override
    {
        dirtyCalls.push_back(state != 0);
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API requestOpenEditor(Steinberg::FIDString /*name*/) override
    {
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API startGroupEdit() override { return Steinberg::kResultOk; }
    Steinberg::tresult PLUGIN_API finishGroupEdit() override { return Steinberg::kResultOk; }

    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override
    {
        if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Vst::IComponentHandler::iid) ||
            Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::FUnknown::iid))
        {
            *obj = static_cast<Steinberg::Vst::IComponentHandler*>(this);
            return Steinberg::kResultOk;
        }
        if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Vst::IComponentHandler2::iid))
        {
            *obj = static_cast<Steinberg::Vst::IComponentHandler2*>(this);
            return Steinberg::kResultOk;
        }
        *obj = nullptr;
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }
};

} // namespace Membrum::TestSupport
