#pragma once

// ==============================================================================
// Version label probe
// ==============================================================================
// Every Krate editor shows the build's version through the shared
// Krate::Plugins::VersionLabel custom view (plugins/shared/src/ui/update_banner_view.h),
// built by the controller's createCustomView("VersionLabel") from VERSION_STR.
// This builds the editor headlessly and counts the shared labels, and the legacy
// tag-9999 CTextLabels a hand-filled version label used before.

#include "editor_lifecycle_harness.h"

#include "ui/update_banner_view.h"

#include "vstgui/lib/cframe.h"
#include "vstgui/lib/controls/ctextlabel.h"
#include "vstgui/plugin-bindings/vst3editor.h"

#include <string>

namespace Krate::TestSupport {

struct VersionLabelCount
{
    int shared = 0;        // Krate::Plugins::VersionLabel views
    int legacyTagged = 0;  // CTextLabels carrying the old version tag 9999
};

inline void countVersionLabels(VSTGUI::CViewContainer* container, VersionLabelCount& count)
{
    container->forEachChild([&count](VSTGUI::CView* child) {
        if (dynamic_cast<Krate::Plugins::VersionLabel*>(child) != nullptr)
            ++count.shared;
        if (auto* label = dynamic_cast<VSTGUI::CTextLabel*>(child); label != nullptr && label->getTag() == 9999)
            ++count.legacyTagged;
        if (auto* sub = child->asViewContainer())
            countVersionLabels(sub, count);
    });
}

// Opens the editor once (as exerciseEditorLifecycle does), counts, and closes it.
inline VersionLabelCount openEditorAndCountVersionLabels(Steinberg::Vst::EditController& controller,
                                                         const char* templateName,
                                                         const std::string& uidescAbsolutePath)
{
    ensureVstguiInitialized();
    auto* editor = new VSTGUI::VST3Editor(&controller, templateName, uidescAbsolutePath.c_str());
    Steinberg::IPlugView* view = editor;
    VersionLabelCount count;
    if (view->attached(nullptr, nativePlatformType()) == Steinberg::kResultTrue && editor->getFrame() != nullptr)
        countVersionLabels(editor->getFrame(), count);
    view->removed();
    view->release();
    return count;
}

} // namespace Krate::TestSupport
