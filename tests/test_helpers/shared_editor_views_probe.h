#pragma once

// ==============================================================================
// Shared editor views probe
// ==============================================================================
// Every Krate editor carries three shared views, all built by
// Krate::Plugins::createSharedEditorView (plugins/shared/src/ui/update_banner_view.h):
// the UpdateBanner, the CheckForUpdatesButton and the VersionLabel. This builds
// the editor headlessly and counts them, plus any legacy tag-9999 CTextLabel a
// hand-filled version label used before. Every plugin's editor lifecycle test
// calls it (tools/lint-plugin-editor-baseline.js checks that).

#include "editor_lifecycle_harness.h"

#include "ui/update_banner_view.h"

#include "vstgui/lib/cframe.h"
#include "vstgui/lib/controls/ctextlabel.h"
#include "vstgui/plugin-bindings/vst3editor.h"

#include <string>

namespace Krate::TestSupport {

struct SharedEditorViewCount
{
    int banners = 0;        // Krate::Plugins::UpdateBannerView
    int checkButtons = 0;   // Krate::Plugins::CheckForUpdatesButton
    int versionLabels = 0;  // Krate::Plugins::VersionLabel
    int legacyTagged = 0;   // CTextLabels carrying the old version tag 9999
};

inline void countSharedEditorViews(VSTGUI::CViewContainer* container, SharedEditorViewCount& count)
{
    container->forEachChild([&count](VSTGUI::CView* child) {
        if (dynamic_cast<Krate::Plugins::UpdateBannerView*>(child) != nullptr)
            ++count.banners;
        if (dynamic_cast<Krate::Plugins::CheckForUpdatesButton*>(child) != nullptr)
            ++count.checkButtons;
        if (dynamic_cast<Krate::Plugins::VersionLabel*>(child) != nullptr)
            ++count.versionLabels;
        if (auto* label = dynamic_cast<VSTGUI::CTextLabel*>(child); label != nullptr && label->getTag() == 9999)
            ++count.legacyTagged;
        if (auto* sub = child->asViewContainer())
            countSharedEditorViews(sub, count);
    });
}

// Opens the editor once (as exerciseEditorLifecycle does), counts, and closes it.
inline SharedEditorViewCount openEditorAndCountSharedViews(Steinberg::Vst::EditController& controller,
                                                           const char* templateName,
                                                           const std::string& uidescAbsolutePath)
{
    ensureVstguiInitialized();
    auto* editor = new VSTGUI::VST3Editor(&controller, templateName, uidescAbsolutePath.c_str());
    Steinberg::IPlugView* view = editor;
    SharedEditorViewCount count;
    if (view->attached(nullptr, nativePlatformType()) == Steinberg::kResultTrue && editor->getFrame() != nullptr)
        countSharedEditorViews(editor->getFrame(), count);
    view->removed();
    view->release();
    return count;
}

} // namespace Krate::TestSupport
