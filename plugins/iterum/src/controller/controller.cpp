// ==============================================================================
// Edit Controller Implementation
// ==============================================================================

#include "controller.h"
#include "plugin_ids.h"
#include "version.h"

#include "preset/preset_manager.h"
#include "ui/preset_browser_view.h"
#include "ui/save_preset_dialog_view.h"
#include "ui/tap_pattern_editor.h"

#include "public.sdk/source/vst/vstparameters.h"

#include "vstgui/lib/cframe.h"
#include "vstgui/lib/platform/iplatformframe.h"
#include "vstgui/lib/controls/ccontrol.h"
#include "vstgui/lib/controls/coptionmenu.h"
#include "vstgui/lib/controls/ctextlabel.h"
#include "vstgui/uidescription/uiviewswitchcontainer.h"
#include "vstgui/uidescription/uiattributes.h"

#include "base/source/fobject.h"

#include <vector>
#include <atomic>

#if defined(_DEBUG) && defined(_WIN32)
#include "vstgui/lib/platform/win32/win32factory.h"
#include <windows.h>
#include <fstream>
#include <sstream>

// Debug helper to log view hierarchy
static void logViewHierarchy(VSTGUI::CView* view, std::ofstream& log, int depth = 0) {
    if (!view) return;

    std::string indent(depth * 2, ' ');

    // Get class name
    const char* className = "Unknown";
    if (dynamic_cast<VSTGUI::UIViewSwitchContainer*>(view)) {
        className = "UIViewSwitchContainer";
    } else if (dynamic_cast<VSTGUI::COptionMenu*>(view)) {
        className = "COptionMenu";
    } else if (dynamic_cast<VSTGUI::CControl*>(view)) {
        className = "CControl";
    } else if (view->asViewContainer()) {
        className = "CViewContainer";
    } else {
        className = "CView";
    }

    // Get control tag if it's a control
    int32_t tag = -1;
    if (auto* control = dynamic_cast<VSTGUI::CControl*>(view)) {
        tag = control->getTag();
    }

    auto size = view->getViewSize();
    log << indent << className;
    if (tag >= 0) {
        log << " [tag=" << tag << "]";
    }
    log << " size=" << size.getWidth() << "x" << size.getHeight();
    log << "\n";

    // Recurse into containers
    if (auto* container = view->asViewContainer()) {
        VSTGUI::ViewIterator it(container);
        while (*it) {
            logViewHierarchy(*it, log, depth + 1);
            ++it;
        }
    }
}

// Debug helper to find first control with a given tag
static VSTGUI::CControl* findControlByTag(VSTGUI::CViewContainer* container, int32_t tag) {
    if (!container) return nullptr;

    VSTGUI::ViewIterator it(container);
    while (*it) {
        if (auto* control = dynamic_cast<VSTGUI::CControl*>(*it)) {
            if (control->getTag() == tag) {
                return control;
            }
        }
        if (auto* childContainer = (*it)->asViewContainer()) {
            if (auto* found = findControlByTag(childContainer, tag)) {
                return found;
            }
        }
        ++it;
    }
    return nullptr;
}

// Debug helper to find ALL controls with a given tag
// Returns all controls (e.g., slider + value display) that share the same tag
static std::vector<VSTGUI::CControl*> findAllControlsByTag(VSTGUI::CViewContainer* container, int32_t tag) {
    std::vector<VSTGUI::CControl*> results;
    if (!container) return results;

    std::function<void(VSTGUI::CViewContainer*)> search;
    search = [tag, &results, &search](VSTGUI::CViewContainer* cont) {
        if (!cont) return;
        VSTGUI::ViewIterator it(cont);
        while (*it) {
            if (auto* control = dynamic_cast<VSTGUI::CControl*>(*it)) {
                if (control->getTag() == tag) {
                    results.push_back(control);
                }
            }
            if (auto* childContainer = (*it)->asViewContainer()) {
                search(childContainer);
            }
            ++it;
        }
    };
    search(container);
    return results;
}

#endif

// ==============================================================================
// VisibilityController: Thread-safe control visibility manager
// ==============================================================================
// Uses IDependent mechanism to receive parameter change notifications on UI thread.
// This is the CORRECT pattern for updating VSTGUI controls based on parameter values.
//
// CRITICAL Threading Rules:
// - setParamNormalized() can be called from ANY thread (automation, state load, etc.)
// - VSTGUI controls MUST only be manipulated on the UI thread
// - Solution: Use Parameter::addDependent() + deferred updates via UpdateHandler
//
// CRITICAL View Switching:
// - UIViewSwitchContainer DESTROYS and RECREATES controls when switching templates
// - DO NOT cache control pointers - they become invalid (dangling) after view switch
// - MUST look up control DYNAMICALLY on each update using control tag
// - Control tag remains constant, pointer changes on every view switch
// ==============================================================================
class VisibilityController : public Steinberg::FObject {
public:
    // editorPtr: Pointer to the controller's activeEditor_ member (NOT the editor itself!)
    // This allows us to always get the CURRENT editor, or nullptr if closed.
    VisibilityController(
        VSTGUI::VST3Editor** editorPtr,
        Steinberg::Vst::Parameter* watchedParam,
        std::initializer_list<Steinberg::int32> controlTags,
        float visibilityThreshold = 0.5f,
        bool showWhenBelow = true)
    : editorPtr_(editorPtr)
    , watchedParam_(watchedParam)
    , controlTags_(controlTags)
    , visibilityThreshold_(visibilityThreshold)
    , showWhenBelow_(showWhenBelow)
    {
        if (watchedParam_) {
            watchedParam_->addRef();
            watchedParam_->addDependent(this);  // Register for parameter change notifications
            // Trigger initial update on UI thread
            watchedParam_->deferUpdate();
        }
    }

    ~VisibilityController() override {
        // Ensure we're deactivated (handles case of direct destruction without deactivate())
        deactivate();

        // Release our reference to the parameter
        if (watchedParam_) {
            watchedParam_->release();
            watchedParam_ = nullptr;
        }
    }

    // Deactivate this controller to safely handle editor close.
    // CRITICAL: This must be called BEFORE destruction to prevent use-after-free.
    // It removes us as a dependent BEFORE the object is destroyed, ensuring that
    // any queued deferred updates won't be delivered to a destroyed object.
    void deactivate() {
        // Use exchange to ensure we only do this once (idempotent)
        if (isActive_.exchange(false, std::memory_order_acq_rel)) {
            // Was active, now deactivated - remove dependent to stop receiving updates
            // This must happen BEFORE destruction to prevent the race condition where
            // a deferred update fires during/after the destructor runs.
            if (watchedParam_) {
                watchedParam_->removeDependent(this);
            }
        }
    }

    // IDependent::update - called on UI thread via deferred update mechanism
    void PLUGIN_API update(Steinberg::FUnknown* changedUnknown, Steinberg::int32 message) override {
        // CRITICAL: Check isActive FIRST before accessing ANY member.
        // This prevents use-after-free when deferred updates fire during/after destruction.
        if (!isActive_.load(std::memory_order_acquire)) {
            return;
        }

        // Get current editor from controller's member - may be nullptr if editor closed
        VSTGUI::VST3Editor* editor = editorPtr_ ? *editorPtr_ : nullptr;
        if (message == IDependent::kChanged && watchedParam_ && editor) {
            // Get current parameter value (normalized: 0.0 to 1.0)
            float normalizedValue = watchedParam_->getNormalized();

            // Determine visibility based on threshold and direction
            bool shouldBeVisible = showWhenBelow_ ?
                (normalizedValue < visibilityThreshold_) :
                (normalizedValue >= visibilityThreshold_);

            // Update visibility for all associated controls (label + slider + value display)
            for (Steinberg::int32 tag : controlTags_) {
                // CRITICAL: Look up ALL controls DYNAMICALLY on each update
                // UIViewSwitchContainer destroys/recreates controls on view switch,
                // so cached pointers become dangling references.
                // IMPORTANT: Multiple controls can have the same tag (e.g., slider + value display)
                auto controls = findAllControlsByTag(tag);

                for (auto* control : controls) {
                    // SAFE: This is called on UI thread via UpdateHandler::deferedUpdate()
                    control->setVisible(shouldBeVisible);

                    // Trigger redraw if needed
                    if (control->getFrame()) {
                        control->invalid();
                    }
                }
            }
        }
    }

    OBJ_METHODS(VisibilityController, FObject)

private:
    // Find ALL controls with given tag in current view hierarchy
    // Returns vector because multiple controls can share a tag (slider + value display)
    std::vector<VSTGUI::CControl*> findAllControlsByTag(Steinberg::int32 tag) {
        std::vector<VSTGUI::CControl*> results;
        // Get current editor - may be nullptr if closed
        VSTGUI::VST3Editor* editor = editorPtr_ ? *editorPtr_ : nullptr;
        if (!editor) return results;
        auto* frame = editor->getFrame();
        if (!frame) return results;

#if defined(_DEBUG) && defined(_WIN32)
        // Use debug helper in debug builds
        return ::findAllControlsByTag(frame, tag);
#else
        // Manual traversal in release builds
        std::function<void(VSTGUI::CViewContainer*)> search;
        search = [tag, &results, &search](VSTGUI::CViewContainer* container) {
            if (!container) return;
            VSTGUI::ViewIterator it(container);
            while (*it) {
                if (auto* control = dynamic_cast<VSTGUI::CControl*>(*it)) {
                    if (control->getTag() == tag) {
                        results.push_back(control);
                    }
                }
                if (auto* childContainer = (*it)->asViewContainer()) {
                    search(childContainer);
                }
                ++it;
            }
        };
        search(frame);
        return results;
#endif
    }

    VSTGUI::VST3Editor** editorPtr_;  // Pointer to controller's activeEditor_ member
    Steinberg::Vst::Parameter* watchedParam_;
    std::vector<Steinberg::int32> controlTags_;
    float visibilityThreshold_;
    bool showWhenBelow_;
    std::atomic<bool> isActive_{true};  // Guards against use-after-free in deferred updates
};

// ==============================================================================
// CompoundVisibilityController: Visibility based on TWO parameters (AND logic)
// ==============================================================================
// Shows controls when BOTH conditions are met:
// - param1 condition is true (based on threshold1 and showWhenBelow1)
// - param2 condition is true (based on threshold2 and showWhenBelow2)
//
// Use case: MultiTap Note Value visibility
// - Show when TimeMode is Synced (>= 0.5) AND Pattern is Mathematical (>= 14/19)
// ==============================================================================
class CompoundVisibilityController : public Steinberg::FObject {
public:
    CompoundVisibilityController(
        VSTGUI::VST3Editor** editorPtr,
        Steinberg::Vst::Parameter* param1,
        float threshold1,
        bool showWhenBelow1,
        Steinberg::Vst::Parameter* param2,
        float threshold2,
        bool showWhenBelow2,
        std::initializer_list<Steinberg::int32> controlTags)
    : editorPtr_(editorPtr)
    , param1_(param1)
    , param2_(param2)
    , controlTags_(controlTags)
    , threshold1_(threshold1)
    , threshold2_(threshold2)
    , showWhenBelow1_(showWhenBelow1)
    , showWhenBelow2_(showWhenBelow2)
    {
        if (param1_) {
            param1_->addRef();
            param1_->addDependent(this);
        }
        if (param2_) {
            param2_->addRef();
            param2_->addDependent(this);
        }
        // Trigger initial update
        if (param1_) param1_->deferUpdate();
    }

    ~CompoundVisibilityController() override {
        deactivate();
        if (param1_) {
            param1_->release();
            param1_ = nullptr;
        }
        if (param2_) {
            param2_->release();
            param2_ = nullptr;
        }
    }

    void deactivate() {
        if (isActive_.exchange(false, std::memory_order_acq_rel)) {
            if (param1_) param1_->removeDependent(this);
            if (param2_) param2_->removeDependent(this);
        }
    }

    void PLUGIN_API update(Steinberg::FUnknown* changedUnknown, Steinberg::int32 message) override {
        if (!isActive_.load(std::memory_order_acquire)) {
            return;
        }

        VSTGUI::VST3Editor* editor = editorPtr_ ? *editorPtr_ : nullptr;
        if (message == IDependent::kChanged && editor && param1_ && param2_) {
            float val1 = param1_->getNormalized();
            float val2 = param2_->getNormalized();

            // Check both conditions
            bool cond1 = showWhenBelow1_ ? (val1 < threshold1_) : (val1 >= threshold1_);
            bool cond2 = showWhenBelow2_ ? (val2 < threshold2_) : (val2 >= threshold2_);
            bool shouldBeVisible = cond1 && cond2;

            for (Steinberg::int32 tag : controlTags_) {
                auto controls = findAllControlsByTag(tag);
                for (auto* control : controls) {
                    control->setVisible(shouldBeVisible);
                    if (control->getFrame()) {
                        control->invalid();
                    }
                }
            }
        }
    }

    OBJ_METHODS(CompoundVisibilityController, FObject)

private:
    std::vector<VSTGUI::CControl*> findAllControlsByTag(Steinberg::int32 tag) {
        std::vector<VSTGUI::CControl*> results;
        VSTGUI::VST3Editor* editor = editorPtr_ ? *editorPtr_ : nullptr;
        if (!editor) return results;
        auto* frame = editor->getFrame();
        if (!frame) return results;

        std::function<void(VSTGUI::CViewContainer*)> search;
        search = [tag, &results, &search](VSTGUI::CViewContainer* container) {
            if (!container) return;
            VSTGUI::ViewIterator it(container);
            while (*it) {
                if (auto* control = dynamic_cast<VSTGUI::CControl*>(*it)) {
                    if (control->getTag() == tag) {
                        results.push_back(control);
                    }
                }
                if (auto* childContainer = (*it)->asViewContainer()) {
                    search(childContainer);
                }
                ++it;
            }
        };
        search(frame);
        return results;
    }

    VSTGUI::VST3Editor** editorPtr_;
    Steinberg::Vst::Parameter* param1_;
    Steinberg::Vst::Parameter* param2_;
    std::vector<Steinberg::int32> controlTags_;
    float threshold1_;
    float threshold2_;
    bool showWhenBelow1_;
    bool showWhenBelow2_;
    std::atomic<bool> isActive_{true};
};

#include "parameters/bbd_params.h"
#include "parameters/digital_params.h"
#include "parameters/ducking_params.h"
#include "parameters/freeze_params.h"
#include "parameters/granular_params.h"
#include "parameters/multitap_params.h"
#include "parameters/pingpong_params.h"
#include "parameters/reverse_params.h"
#include "parameters/shimmer_params.h"
#include "parameters/spectral_params.h"
#include "parameters/tape_params.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ustring.h"

#include <cstdio>
#include <string>

namespace Iterum {

// ==============================================================================
// Destructor
// ==============================================================================
// Defined here so unique_ptr<PresetManager> can use the complete type

Controller::~Controller() = default;

// ==============================================================================
// IPluginBase
// ==============================================================================

Steinberg::tresult PLUGIN_API Controller::initialize(FUnknown* context) {
#if defined(_DEBUG) && defined(_WIN32)
    // Debug: Write to log file during initialization
    {
        char tempPath[MAX_PATH];
        GetTempPathA(MAX_PATH, tempPath);
        std::string logPath = std::string(tempPath) + "iterum_debug.log";
        std::ofstream log(logPath, std::ios::app);
        log << "=== Iterum Controller::initialize called ===\n";
        log.flush();
    }
#endif

    // Always call parent first
    Steinberg::tresult result = EditControllerEx1::initialize(context);
    if (result != Steinberg::kResultTrue) {
        return result;
    }

    // ==========================================================================
    // Register Parameters
    // Constitution Principle V: All values normalized 0.0 to 1.0
    // ==========================================================================

    // Note: Bypass parameter removed - DAWs provide their own bypass functionality

    // Gain parameter
    parameters.addParameter(
        STR16("Gain"),             // title
        STR16("dB"),               // units
        0,                          // stepCount (0 = continuous)
        0.5,                        // defaultValue (normalized: 0.5 = unity)
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        kGainId,                    // parameter ID
        0,                          // unitId
        STR16("Gain")              // shortTitle
    );

    // Mode parameter (selects active delay mode)
    // MUST use StringListParameter for proper toPlain() scaling!
    // Basic Parameter::toPlain() just returns normalized value unchanged.
    auto* modeParam = new Steinberg::Vst::StringListParameter(
        STR16("Mode"),             // title
        kModeId,                    // parameter ID
        nullptr,                    // units
        Steinberg::Vst::ParameterInfo::kCanAutomate |
        Steinberg::Vst::ParameterInfo::kIsList
    );
    modeParam->appendString(STR16("Granular"));
    modeParam->appendString(STR16("Spectral"));
    modeParam->appendString(STR16("Shimmer"));
    modeParam->appendString(STR16("Tape"));
    modeParam->appendString(STR16("BBD"));
    modeParam->appendString(STR16("Digital"));
    modeParam->appendString(STR16("PingPong"));
    modeParam->appendString(STR16("Reverse"));
    modeParam->appendString(STR16("MultiTap"));
    modeParam->appendString(STR16("Freeze"));
    modeParam->appendString(STR16("Ducking"));
    // Set default to Digital (index 5) - normalized value = 5/10 = 0.5
    modeParam->setNormalized(0.5);
    parameters.addParameter(modeParam);

    // ==========================================================================
    // Mode-Specific Parameter Registration
    // ==========================================================================

    registerGranularParams(parameters);  // Granular Delay (spec 034)
    registerSpectralParams(parameters);  // Spectral Delay (spec 033)
    registerDuckingParams(parameters);   // Ducking Delay (spec 032)
    registerFreezeParams(parameters);    // Freeze Mode (spec 031)
    registerReverseParams(parameters);   // Reverse Delay (spec 030)
    registerShimmerParams(parameters);   // Shimmer Delay (spec 029)
    registerTapeParams(parameters);      // Tape Delay (spec 024)
    registerBBDParams(parameters);       // BBD Delay (spec 025)
    registerDigitalParams(parameters);   // Digital Delay (spec 026)
    registerPingPongParams(parameters);  // PingPong Delay (spec 027)
    registerMultiTapParams(parameters);  // MultiTap Delay (spec 028)

    // ==========================================================================
    // Preset Manager (Spec 042)
    // ==========================================================================
    // Create PresetManager for preset browsing/scanning.
    // Note: We pass nullptr for processor since the controller doesn't have
    // direct access to it. We provide a state provider callback for saving.
    presetManager_ = std::make_unique<PresetManager>(nullptr, this);

    // Set state provider callback for preset saving
    presetManager_->setStateProvider([this]() -> Steinberg::IBStream* {
        return this->createComponentStateStream();
    });

    // Set load provider callback for preset loading
    presetManager_->setLoadProvider([this](Steinberg::IBStream* state) -> bool {
        return this->loadComponentStateWithNotify(state);
    });

    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API Controller::terminate() {
    return EditControllerEx1::terminate();
}

// ==============================================================================
// IEditController - State Management
// ==============================================================================

Steinberg::tresult PLUGIN_API Controller::setComponentState(
    Steinberg::IBStream* state) {
    // ==========================================================================
    // Constitution Principle I: Controller syncs TO processor state
    // This is called by host after processor state is loaded
    // We must read the SAME format that Processor::getState() writes
    // ==========================================================================

    if (!state) {
        return Steinberg::kResultFalse;
    }

    Steinberg::IBStreamer streamer(state, kLittleEndian);

    // Read global parameters (must match Processor::getState order)
    float gain = 0.5f;
    if (streamer.readFloat(gain)) {
        // Convert from linear gain to normalized parameter value
        // gain range: 0.0 to 2.0, normalized = gain / 2.0
        setParamNormalized(kGainId, static_cast<double>(gain / 2.0f));
    }

    // Note: bypass removed - DAWs provide their own bypass functionality

    Steinberg::int32 mode = 0;
    if (streamer.readInt32(mode)) {
        // Convert mode index (0-10) to normalized (0.0-1.0)
        setParamNormalized(kModeId, static_cast<double>(mode) / 10.0);
    }

    // ==========================================================================
    // Sync mode-specific parameters (order MUST match Processor::getState)
    // ==========================================================================

    syncGranularParamsToController(streamer, *this);  // Granular Delay (spec 034)
    syncSpectralParamsToController(streamer, *this);  // Spectral Delay (spec 033)
    syncDuckingParamsToController(streamer, *this);   // Ducking Delay (spec 032)
    syncFreezeParamsToController(streamer, *this);    // Freeze Mode (spec 031)
    syncReverseParamsToController(streamer, *this);   // Reverse Delay (spec 030)
    syncShimmerParamsToController(streamer, *this);   // Shimmer Delay (spec 029)
    syncTapeParamsToController(streamer, *this);      // Tape Delay (spec 024)
    syncBBDParamsToController(streamer, *this);       // BBD Delay (spec 025)
    syncDigitalParamsToController(streamer, *this);   // Digital Delay (spec 026)
    syncPingPongParamsToController(streamer, *this);  // PingPong Delay (spec 027)
    syncMultiTapParamsToController(streamer, *this);  // MultiTap Delay (spec 028)

    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API Controller::getState(Steinberg::IBStream* state) {
    // Save controller-specific state (UI preferences, not audio parameters)
    // Constitution Principle V: UI-only state goes here

    // Example: Save which tab is selected, zoom level, etc.
    // For now, we have no controller-specific state
    (void)state;  // Unused for now

    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API Controller::setState(Steinberg::IBStream* state) {
    // Restore controller-specific state
    (void)state;  // Unused for now

    return Steinberg::kResultTrue;
}

// ==============================================================================
// IEditController - Editor Creation
// ==============================================================================

Steinberg::IPlugView* PLUGIN_API Controller::createView(
    Steinberg::FIDString name) {
    // ==========================================================================
    // Constitution Principle V: Use UIDescription for UI layout
    // ==========================================================================

    if (Steinberg::FIDStringsEqual(name, Steinberg::Vst::ViewType::kEditor)) {
#if defined(_DEBUG) && defined(_WIN32)
        // Debug: Write info to log file for easy diagnosis
        {
            char tempPath[MAX_PATH];
            GetTempPathA(MAX_PATH, tempPath);
            std::string logPath = std::string(tempPath) + "iterum_debug.log";
            std::ofstream log(logPath, std::ios::app);

            log << "=== Iterum createView called ===\n";

            if (auto* factory = VSTGUI::getPlatformFactory().asWin32Factory()) {
                log << "Got Win32Factory: OK\n";

                if (auto path = factory->getResourceBasePath()) {
                    std::string basePath(*path);
                    std::string fullPath = basePath + "\\editor.uidesc";
                    log << "Resource base path: " << basePath << "\n";

                    // Check if file actually exists
                    DWORD attribs = GetFileAttributesA(fullPath.c_str());
                    if (attribs != INVALID_FILE_ATTRIBUTES) {
                        log << "editor.uidesc EXISTS at path: OK\n";
                    } else {
                        log << "ERROR: editor.uidesc NOT FOUND at: " << fullPath << "\n";
                        log << "GetLastError: " << GetLastError() << "\n";
                    }
                } else {
                    log << "ERROR: Resource base path is NOT SET!\n";
                    log << "This means setupVSTGUIBundleSupport was not called.\n";
                }
            } else {
                log << "ERROR: Cannot get Win32Factory!\n";
            }

            log << "Creating VST3Editor with editor.uidesc...\n";
            log.flush();
        }
#endif

        // Create VSTGUI editor from UIDescription file
        auto* editor = new VSTGUI::VST3Editor(
            this,                           // controller
            "Editor",                       // viewName (matches uidesc)
            "editor.uidesc"                 // UIDescription file
        );

        return editor;
    }

    return nullptr;
}

// ==============================================================================
// IEditController - Parameter Display
// ==============================================================================

Steinberg::tresult PLUGIN_API Controller::getParamStringByValue(
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue valueNormalized,
    Steinberg::Vst::String128 string) {

    // =======================================================================
    // Route parameter formatting by ID range
    // =======================================================================

    if (id < kGranularBaseId) {
        // Global parameters (0-99)
        switch (id) {
            case kGainId: {
                // Convert normalized (0-1) to dB display
                // normalized 0.5 = 0 dB (unity gain)
                double linearGain = valueNormalized * 2.0;
                double dB = (linearGain > 0.0001)
                    ? 20.0 * std::log10(linearGain)
                    : -80.0;

                char text[32];
                std::snprintf(text, sizeof(text), "%.1f", dB);

                Steinberg::UString(string, 128).fromAscii(text);
                return Steinberg::kResultTrue;
            }

            // Note: kBypassId removed - DAWs provide their own bypass functionality

            // kModeId is handled by StringListParameter automatically

            default:
                return EditControllerEx1::getParamStringByValue(
                    id, valueNormalized, string);
        }
    }
    // =========================================================================
    // Mode-Specific Parameter Formatting
    // =========================================================================
    // Each formatXxxParam function handles continuous parameters but returns
    // kResultFalse for StringListParameters (dropdowns), which must be handled
    // by the base class EditControllerEx1::getParamStringByValue().
    // =========================================================================

    Steinberg::tresult result = Steinberg::kResultFalse;

    if (id >= kGranularBaseId && id <= kGranularEndId) {
        result = formatGranularParam(id, valueNormalized, string);
    }
    else if (id >= kSpectralBaseId && id <= kSpectralEndId) {
        result = formatSpectralParam(id, valueNormalized, string);
    }
    else if (id >= kShimmerBaseId && id <= kShimmerEndId) {
        result = formatShimmerParam(id, valueNormalized, string);
    }
    else if (id >= kTapeBaseId && id <= kTapeEndId) {
        result = formatTapeParam(id, valueNormalized, string);
    }
    else if (id >= kBBDBaseId && id <= kBBDEndId) {
        result = formatBBDParam(id, valueNormalized, string);
    }
    else if (id >= kDigitalBaseId && id <= kDigitalEndId) {
        result = formatDigitalParam(id, valueNormalized, string);
    }
    else if (id >= kPingPongBaseId && id <= kPingPongEndId) {
        result = formatPingPongParam(id, valueNormalized, string);
    }
    else if (id >= kReverseBaseId && id <= kReverseEndId) {
        result = formatReverseParam(id, valueNormalized, string);
    }
    else if (id >= kMultiTapBaseId && id <= kMultiTapEndId) {
        result = formatMultiTapParam(id, valueNormalized, string);
    }
    else if (id >= kFreezeBaseId && id <= kFreezeEndId) {
        result = formatFreezeParam(id, valueNormalized, string);
    }
    else if (id >= kDuckingBaseId && id <= kDuckingEndId) {
        result = formatDuckingParam(id, valueNormalized, string);
    }

    // If the mode-specific formatter didn't handle it (returns kResultFalse),
    // fall back to base class. This is essential for StringListParameters
    // (dropdowns) which use their own toString() method.
    if (result != Steinberg::kResultOk) {
        return EditControllerEx1::getParamStringByValue(id, valueNormalized, string);
    }

    return result;
}

Steinberg::tresult PLUGIN_API Controller::getParamValueByString(
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::TChar* string,
    Steinberg::Vst::ParamValue& valueNormalized) {

    switch (id) {
        case kGainId: {
            // Parse dB value from string
            char asciiString[128];
            Steinberg::UString(string, 128).toAscii(asciiString, 128);

            double dB = 0.0;
            if (std::sscanf(asciiString, "%lf", &dB) == 1) {
                // Convert dB to linear, then to normalized
                double linearGain = std::pow(10.0, dB / 20.0);
                valueNormalized = linearGain / 2.0;
                return Steinberg::kResultTrue;
            }
            return Steinberg::kResultFalse;
        }

        default:
            return EditControllerEx1::getParamValueByString(
                id, string, valueNormalized);
    }
}

// ==============================================================================
// IEditController - Parameter Changes (DEBUG LOGGING)
// ==============================================================================

Steinberg::tresult PLUGIN_API Controller::setParamNormalized(
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value) {

#if defined(_DEBUG) && defined(_WIN32)
    if (id == kModeId) {
        char tempPath[MAX_PATH];
        GetTempPathA(MAX_PATH, tempPath);
        std::string logPath = std::string(tempPath) + "iterum_debug.log";
        std::ofstream log(logPath, std::ios::app);

        log << "\n=== setParamNormalized kModeId ===\n";
        log << "  Input normalized value: " << value << "\n";

        // Use SDK toPlain() to get the mode index
        if (auto* param = getParameterObject(kModeId)) {
            auto plainValue = param->toPlain(value);
            log << "  SDK toPlain() result: " << plainValue << "\n";
            log << "  As integer index: " << static_cast<int>(plainValue) << "\n";

            // Log parameter info using the ParameterInfo struct
            log << "  Parameter stepCount: " << param->getInfo().stepCount << "\n";
            log << "  Parameter defaultNormalized: " << param->getInfo().defaultNormalizedValue << "\n";
        } else {
            log << "  ERROR: getParameterObject(kModeId) returned nullptr!\n";
        }

        // Log the current state of the UI if editor is open
        if (activeEditor_) {
            if (auto* frame = activeEditor_->getFrame()) {
                // Find the Mode COptionMenu
                auto* modeControl = findControlByTag(frame, kModeId);
                if (modeControl) {
                    log << "  COptionMenu state BEFORE update:\n";
                    log << "    getValue(): " << modeControl->getValue() << "\n";
                    log << "    getValueNormalized(): " << modeControl->getValueNormalized() << "\n";
                    if (auto* optMenu = dynamic_cast<VSTGUI::COptionMenu*>(modeControl)) {
                        log << "    getCurrentIndex(): " << optMenu->getCurrentIndex() << "\n";
                        log << "    getNbEntries(): " << optMenu->getNbEntries() << "\n";
                    }
                }

                // Find UIViewSwitchContainer and log its state
                VSTGUI::ViewIterator it(frame);
                while (*it) {
                    if (auto* viewSwitch = dynamic_cast<VSTGUI::UIViewSwitchContainer*>(*it)) {
                        log << "  UIViewSwitchContainer state BEFORE update:\n";
                        log << "    currentViewIndex: " << viewSwitch->getCurrentViewIndex() << "\n";
                        break;
                    }
                    // Check child containers
                    if (auto* container = (*it)->asViewContainer()) {
                        VSTGUI::ViewIterator childIt(container);
                        while (*childIt) {
                            if (auto* viewSwitch = dynamic_cast<VSTGUI::UIViewSwitchContainer*>(*childIt)) {
                                log << "  UIViewSwitchContainer state BEFORE update:\n";
                                log << "    currentViewIndex: " << viewSwitch->getCurrentViewIndex() << "\n";
                                break;
                            }
                            ++childIt;
                        }
                    }
                    ++it;
                }
            }
        }

        log << "  Calling EditControllerEx1::setParamNormalized...\n";
        log.flush();
    }
#endif

    // Call base class - this is the ONLY thing that actually happens
    auto result = EditControllerEx1::setParamNormalized(id, value);

    // NOTE: Conditional visibility for delay time controls is handled by
    // VisibilityController instances via IDependent mechanism (see didOpen).
    // DO NOT manipulate UI controls here - setParamNormalized can be called
    // from non-UI threads (automation, state loading).

    // Update TapPatternEditor snap division when parameter changes (Spec 046)
    // This is safe because the dropdown interaction happens on UI thread
    if (id == kMultiTapSnapDivisionId && tapPatternEditor_) {
        int snapIndex = static_cast<int>(value * 5.0 + 0.5);
        tapPatternEditor_->setSnapDivision(static_cast<SnapDivision>(snapIndex));
    }

#if defined(_DEBUG) && defined(_WIN32)
    if (id == kModeId) {
        char tempPath[MAX_PATH];
        GetTempPathA(MAX_PATH, tempPath);
        std::string logPath = std::string(tempPath) + "iterum_debug.log";
        std::ofstream log(logPath, std::ios::app);

        log << "  Base class returned: " << (result == Steinberg::kResultTrue ? "kResultTrue" : "other") << "\n";

        // Log state AFTER the base class update
        if (activeEditor_) {
            if (auto* frame = activeEditor_->getFrame()) {
                auto* modeControl = findControlByTag(frame, kModeId);
                if (modeControl) {
                    log << "  COptionMenu state AFTER update:\n";
                    log << "    getValue(): " << modeControl->getValue() << "\n";
                    log << "    getValueNormalized(): " << modeControl->getValueNormalized() << "\n";
                    if (auto* optMenu = dynamic_cast<VSTGUI::COptionMenu*>(modeControl)) {
                        log << "    getCurrentIndex(): " << optMenu->getCurrentIndex() << "\n";
                    }
                }

                // Find UIViewSwitchContainer AFTER update
                VSTGUI::ViewIterator it(frame);
                while (*it) {
                    if (auto* viewSwitch = dynamic_cast<VSTGUI::UIViewSwitchContainer*>(*it)) {
                        log << "  UIViewSwitchContainer state AFTER update:\n";
                        log << "    currentViewIndex: " << viewSwitch->getCurrentViewIndex() << "\n";
                        break;
                    }
                    if (auto* container = (*it)->asViewContainer()) {
                        VSTGUI::ViewIterator childIt(container);
                        while (*childIt) {
                            if (auto* viewSwitch = dynamic_cast<VSTGUI::UIViewSwitchContainer*>(*childIt)) {
                                log << "  UIViewSwitchContainer state AFTER update:\n";
                                log << "    currentViewIndex: " << viewSwitch->getCurrentViewIndex() << "\n";
                                break;
                            }
                            ++childIt;
                        }
                    }
                    ++it;
                }
            }
        }

        log << "=== END setParamNormalized ===\n\n";
        log.flush();
    }
#endif

    return result;
}

// ==============================================================================
// VST3EditorDelegate
// ==============================================================================

// =============================================================================
// PresetBrowserButton: Button that opens the preset browser
// =============================================================================
class PresetBrowserButton : public VSTGUI::CTextButton {
public:
    PresetBrowserButton(const VSTGUI::CRect& size, Controller* controller)
        : CTextButton(size, nullptr, -1, "Presets")
        , controller_(controller)
    {
        setFrameColor(VSTGUI::CColor(80, 80, 85));
        setTextColor(VSTGUI::CColor(255, 255, 255));
    }

    VSTGUI::CMouseEventResult onMouseDown(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override
    {
        if (buttons.isLeftButton() && controller_) {
            controller_->openPresetBrowser();
            return VSTGUI::kMouseEventHandled;
        }
        return CTextButton::onMouseDown(where, buttons);
    }

private:
    Controller* controller_ = nullptr;
};

// =============================================================================
// SavePresetButton: Button that opens standalone save dialog (Spec 042)
// =============================================================================
class SavePresetButton : public VSTGUI::CTextButton {
public:
    SavePresetButton(const VSTGUI::CRect& size, Controller* controller)
        : CTextButton(size, nullptr, -1, "Save Preset")
        , controller_(controller)
    {
        setFrameColor(VSTGUI::CColor(80, 80, 85));
        setTextColor(VSTGUI::CColor(255, 255, 255));
    }

    VSTGUI::CMouseEventResult onMouseDown(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override
    {
        if (buttons.isLeftButton() && controller_) {
            controller_->openSavePresetDialog();
            return VSTGUI::kMouseEventHandled;
        }
        return CTextButton::onMouseDown(where, buttons);
    }

private:
    Controller* controller_ = nullptr;
};

VSTGUI::CView* Controller::createCustomView(
    VSTGUI::UTF8StringPtr name,
    const VSTGUI::UIAttributes& attributes,
    const VSTGUI::IUIDescription* description,
    VSTGUI::VST3Editor* editor) {
    // ==========================================================================
    // Constitution Principle V: Create custom views here
    // Return nullptr to use default view creation
    // ==========================================================================

    // Silence unused parameter warnings
    (void)description;
    (void)editor;

    // Preset Browser Button (Spec 042)
    if (VSTGUI::UTF8StringView(name) == "PresetBrowserButton") {
        // Read origin and size from UIAttributes to get correct positioning
        VSTGUI::CPoint origin(0, 0);
        VSTGUI::CPoint size(80, 24);
        attributes.getPointAttribute("origin", origin);
        attributes.getPointAttribute("size", size);
        VSTGUI::CRect rect(origin.x, origin.y, origin.x + size.x, origin.y + size.y);
        return new PresetBrowserButton(rect, this);
    }

    // Save Preset Button (Spec 042) - Quick save shortcut
    if (VSTGUI::UTF8StringView(name) == "SavePresetButton") {
        VSTGUI::CPoint origin(0, 0);
        VSTGUI::CPoint size(60, 24);
        attributes.getPointAttribute("origin", origin);
        attributes.getPointAttribute("size", size);
        VSTGUI::CRect rect(origin.x, origin.y, origin.x + size.x, origin.y + size.y);
        return new SavePresetButton(rect, this);
    }

    // TapPatternEditor (Spec 046) - Custom tap pattern visual editor
    if (VSTGUI::UTF8StringView(name) == "TapPatternEditor") {
        VSTGUI::CPoint origin(0, 0);
        VSTGUI::CPoint size(400, 150);
        attributes.getPointAttribute("origin", origin);
        attributes.getPointAttribute("size", size);
        VSTGUI::CRect rect(origin.x, origin.y, origin.x + size.x, origin.y + size.y);

        auto* patternEditor = new TapPatternEditor(rect);

        // Initialize with current parameter values (T033)
        for (size_t i = 0; i < 16; ++i) {
            // Time ratios (IDs 950-965)
            if (auto* timeParam = getParameterObject(kMultiTapCustomTime0Id + static_cast<Steinberg::Vst::ParamID>(i))) {
                patternEditor->setTapTimeRatio(i, static_cast<float>(timeParam->getNormalized()));
            }
            // Levels (IDs 966-981)
            if (auto* levelParam = getParameterObject(kMultiTapCustomLevel0Id + static_cast<Steinberg::Vst::ParamID>(i))) {
                patternEditor->setTapLevel(i, static_cast<float>(levelParam->getNormalized()));
            }
        }

        // Initialize tap count from parameter
        if (auto* tapCountParam = getParameterObject(kMultiTapTapCountId)) {
            // Tap count is 2-16, normalized 0-1 maps to 2-16
            float normalized = static_cast<float>(tapCountParam->getNormalized());
            int tapCount = static_cast<int>(2 + normalized * 14 + 0.5f);
            patternEditor->setActiveTapCount(static_cast<size_t>(tapCount));
        }

        // Initialize snap division from parameter (T058)
        if (auto* snapParam = getParameterObject(kMultiTapSnapDivisionId)) {
            // Snap division: 0-5 (off, 1/4, 1/8, 1/16, 1/32, triplet)
            float normalized = static_cast<float>(snapParam->getNormalized());
            int snapIndex = static_cast<int>(normalized * 5.0f + 0.5f);
            patternEditor->setSnapDivision(static_cast<SnapDivision>(snapIndex));
        }

        // Store reference for parameter updates
        tapPatternEditor_ = patternEditor;

        // Set up parameter callback to notify host of changes
        patternEditor->setParameterCallback(
            [this](Steinberg::Vst::ParamID paramId, float normalizedValue) {
                if (auto* param = getParameterObject(paramId)) {
                    param->setNormalized(static_cast<Steinberg::Vst::ParamValue>(normalizedValue));
                    performEdit(paramId, static_cast<Steinberg::Vst::ParamValue>(normalizedValue));
                }
            });

        return patternEditor;
    }

    return nullptr;
}

void Controller::didOpen(VSTGUI::VST3Editor* editor) {
    // Store editor reference for manual UI control
    activeEditor_ = editor;

    // =========================================================================
    // Option Menu Configuration
    //
    // Native Windows popup (setupGenericOptionMenu false):
    //   + Click to open, click to select (standard behavior)
    //   - May have WM_COMMAND message issues in some hosts
    //
    // Generic VSTGUI menu (setupGenericOptionMenu true):
    //   + Works reliably across all hosts
    //   - Uses hold-to-select behavior (hold mouse, drag to item, release)
    //
    // Currently: Using native Windows popup for standard click behavior
    // If selection doesn't work in your host, enable the generic menu below
    // =========================================================================
    if (editor) {
        if (auto* frame = editor->getFrame()) {
            if (auto* platformFrame = frame->getPlatformFrame()) {
                // Use generic VSTGUI menu for reliable cross-host behavior
                platformFrame->setupGenericOptionMenu(true);
            }
            // UIViewSwitchContainer is automatically controlled via
            // template-switch-control="Mode" in editor.uidesc

            // =====================================================================
            // Conditional Visibility: Delay Time Controls
            // =====================================================================
            // Digital and PingPong modes have a delay time control that should be
            // hidden when time mode is "Synced" (since time value is ignored).
            //
            // Thread-Safe Pattern:
            // - Create VisibilityController instances that register as IDependent
            // - Parameter changes trigger IDependent::update() on UI thread
            // - UpdateHandler automatically defers updates to UI thread
            // - VSTGUI controls are ONLY manipulated on UI thread
            //
            // Dynamic Lookup Pattern:
            // - UIViewSwitchContainer destroys/recreates controls on view switch
            // - DO NOT cache control pointers - they become dangling after switch
            // - VisibilityController uses control TAG for dynamic lookup
            // - Each update() looks up current control by tag (survives view switch)
            // =====================================================================

            // Create visibility controllers for Digital mode
            // Hide delay time label + control when time mode is "Synced" (>= 0.5)
            // NOTE: Pass &activeEditor_ (pointer to member) so VisibilityController
            // always gets the CURRENT editor, avoiding dangling pointer crashes
            // when the editor is closed and reopened.
            if (auto* digitalTimeMode = getParameterObject(kDigitalTimeModeId)) {
                digitalDelayTimeVisibilityController_ = new VisibilityController(
                    &activeEditor_, digitalTimeMode, {9901, kDigitalDelayTimeId}, 0.5f, true);
            }

            // Hide Age label + control when Era is "Pristine" (< 0.25)
            // Era values: 0 = Pristine (0.0), 1 = 80s (0.5), 2 = LoFi (1.0)
            // Show Age when Era >= 0.25 (80s or LoFi)
            if (auto* digitalEra = getParameterObject(kDigitalEraId)) {
                digitalAgeVisibilityController_ = new VisibilityController(
                    &activeEditor_, digitalEra, {9902, kDigitalAgeId}, 0.25f, false);
            }

            // Create visibility controllers for PingPong mode
            // Hide delay time label + control when time mode is "Synced" (>= 0.5)
            if (auto* pingPongTimeMode = getParameterObject(kPingPongTimeModeId)) {
                pingPongDelayTimeVisibilityController_ = new VisibilityController(
                    &activeEditor_, pingPongTimeMode, {9903, kPingPongDelayTimeId}, 0.5f, true);
            }

            // Create visibility controllers for Granular mode
            // Hide delay time label + control when time mode is "Synced" (>= 0.5)
            if (auto* granularTimeMode = getParameterObject(kGranularTimeModeId)) {
                granularDelayTimeVisibilityController_ = new VisibilityController(
                    &activeEditor_, granularTimeMode, {9904, kGranularDelayTimeId}, 0.5f, true);
            }

            // Create visibility controllers for Spectral mode (spec 041)
            // Hide base delay label + control when time mode is "Synced" (>= 0.5)
            if (auto* spectralTimeMode = getParameterObject(kSpectralTimeModeId)) {
                spectralBaseDelayVisibilityController_ = new VisibilityController(
                    &activeEditor_, spectralTimeMode, {9912, kSpectralBaseDelayId}, 0.5f, true);
            }

            // Create visibility controllers for 6 delay modes with tempo sync
            // Hide delay time when time mode is "Synced" (>= 0.5)
            if (auto* shimmerTimeMode = getParameterObject(kShimmerTimeModeId)) {
                shimmerDelayTimeVisibilityController_ = new VisibilityController(
                    &activeEditor_, shimmerTimeMode, {9905, kShimmerDelayTimeId}, 0.5f, true);
            }
            if (auto* bbdTimeMode = getParameterObject(kBBDTimeModeId)) {
                bbdDelayTimeVisibilityController_ = new VisibilityController(
                    &activeEditor_, bbdTimeMode, {9906, kBBDDelayTimeId}, 0.5f, true);
            }
            if (auto* reverseTimeMode = getParameterObject(kReverseTimeModeId)) {
                reverseChunkSizeVisibilityController_ = new VisibilityController(
                    &activeEditor_, reverseTimeMode, {9907, kReverseChunkSizeId}, 0.5f, true);
            }
            // MultiTap has no TimeMode - BaseTime and Tempo controls removed (simplified design)
            if (auto* freezeTimeMode = getParameterObject(kFreezeTimeModeId)) {
                freezeDelayTimeVisibilityController_ = new VisibilityController(
                    &activeEditor_, freezeTimeMode, {9909, kFreezeDelayTimeId}, 0.5f, true);
            }
            if (auto* duckingTimeMode = getParameterObject(kDuckingTimeModeId)) {
                duckingDelayTimeVisibilityController_ = new VisibilityController(
                    &activeEditor_, duckingTimeMode, {9910, kDuckingDelayTimeId}, 0.5f, true);
            }

            // Create NoteValue visibility controllers for all 10 delay modes
            // Show note value label + control when time mode is "Synced" (>= 0.5)
            // NOTE: showWhenBelow = false means visible when value >= threshold
            if (auto* granularTimeMode = getParameterObject(kGranularTimeModeId)) {
                granularNoteValueVisibilityController_ = new VisibilityController(
                    &activeEditor_, granularTimeMode, {9920, kGranularNoteValueId}, 0.5f, false);
            }
            if (auto* spectralTimeMode = getParameterObject(kSpectralTimeModeId)) {
                spectralNoteValueVisibilityController_ = new VisibilityController(
                    &activeEditor_, spectralTimeMode, {9921, kSpectralNoteValueId}, 0.5f, false);
            }
            if (auto* shimmerTimeMode = getParameterObject(kShimmerTimeModeId)) {
                shimmerNoteValueVisibilityController_ = new VisibilityController(
                    &activeEditor_, shimmerTimeMode, {9922, kShimmerNoteValueId}, 0.5f, false);
            }
            if (auto* bbdTimeMode = getParameterObject(kBBDTimeModeId)) {
                bbdNoteValueVisibilityController_ = new VisibilityController(
                    &activeEditor_, bbdTimeMode, {9923, kBBDNoteValueId}, 0.5f, false);
            }
            if (auto* digitalTimeMode = getParameterObject(kDigitalTimeModeId)) {
                digitalNoteValueVisibilityController_ = new VisibilityController(
                    &activeEditor_, digitalTimeMode, {9924, kDigitalNoteValueId}, 0.5f, false);
            }
            if (auto* pingPongTimeMode = getParameterObject(kPingPongTimeModeId)) {
                pingPongNoteValueVisibilityController_ = new VisibilityController(
                    &activeEditor_, pingPongTimeMode, {9925, kPingPongNoteValueId}, 0.5f, false);
            }
            if (auto* reverseTimeMode = getParameterObject(kReverseTimeModeId)) {
                reverseNoteValueVisibilityController_ = new VisibilityController(
                    &activeEditor_, reverseTimeMode, {9926, kReverseNoteValueId}, 0.5f, false);
            }
            // MultiTap Note Value: Show when Pattern is Mathematical (GoldenRatio+)
            // Simplified design: No TimeMode dependency. Pattern >= 14/19 means mathematical.
            // Preset patterns (0-13) derive timing from pattern name + tempo.
            // Mathematical patterns (14-19) use Note Value + tempo for baseTimeMs.
            if (auto* multitapPattern = getParameterObject(kMultiTapTimingPatternId)) {
                multitapNoteValueVisibilityController_ = new VisibilityController(
                    &activeEditor_, multitapPattern,
                    {9931, 9927, 9930, kMultiTapNoteValueId, kMultiTapNoteModifierId},  // Section + labels + dropdowns
                    14.0f / 19.0f, false);  // Show when pattern >= 14/19 (mathematical)
            }
            if (auto* freezeTimeMode = getParameterObject(kFreezeTimeModeId)) {
                freezeNoteValueVisibilityController_ = new VisibilityController(
                    &activeEditor_, freezeTimeMode, {9928, kFreezeNoteValueId}, 0.5f, false);
            }
            if (auto* duckingTimeMode2 = getParameterObject(kDuckingTimeModeId)) {
                duckingNoteValueVisibilityController_ = new VisibilityController(
                    &activeEditor_, duckingTimeMode2, {9929, kDuckingNoteValueId}, 0.5f, false);
            }

            // =====================================================================
            // Dynamic Version Label
            // =====================================================================
            // Set version label text from version.json instead of hardcoded string
            // Tag 9999 is assigned to the version label in editor.uidesc
            // =====================================================================
            std::function<VSTGUI::CTextLabel*(VSTGUI::CViewContainer*, int32_t)> findTextLabel;
            findTextLabel = [&findTextLabel](VSTGUI::CViewContainer* container, int32_t tag) -> VSTGUI::CTextLabel* {
                if (!container) return nullptr;
                VSTGUI::ViewIterator it(container);
                while (*it) {
                    if (auto* label = dynamic_cast<VSTGUI::CTextLabel*>(*it)) {
                        if (label->getTag() == tag) {
                            return label;
                        }
                    }
                    if (auto* childContainer = (*it)->asViewContainer()) {
                        if (auto* found = findTextLabel(childContainer, tag)) {
                            return found;
                        }
                    }
                    ++it;
                }
                return nullptr;
            };

            // Find and update version label (tag 9999)
            if (auto* versionLabel = findTextLabel(frame, 9999)) {
                versionLabel->setText(UI_VERSION_STR);
            }

            // =====================================================================
            // Preset Browser View (Spec 042)
            // =====================================================================
            // Create the preset browser view as an overlay covering the full frame.
            // The view is initially hidden and shown via openPresetBrowser().
            // =====================================================================
            if (presetManager_) {
                auto frameSize = frame->getViewSize();
                presetBrowserView_ = new PresetBrowserView(frameSize, presetManager_.get());
                frame->addView(presetBrowserView_);

                // Save Preset Dialog - standalone dialog for quick save from main UI
                savePresetDialogView_ = new SavePresetDialogView(frameSize, presetManager_.get());
                frame->addView(savePresetDialogView_);
            }
        }
    }

#if defined(_DEBUG) && defined(_WIN32)
    {
        char tempPath[MAX_PATH];
        GetTempPathA(MAX_PATH, tempPath);
        std::string logPath = std::string(tempPath) + "iterum_debug.log";
        std::ofstream log(logPath, std::ios::app);

        log << "\n========================================\n";
        log << "=== didOpen called ===\n";
        log << "========================================\n";

        if (editor) {
            log << "Editor pointer: OK\n";

            if (auto* frame = editor->getFrame()) {
                log << "Frame exists\n";
                auto size = frame->getViewSize();
                log << "Frame size: " << size.getWidth() << "x" << size.getHeight() << "\n";
                log << "Frame has " << frame->getNbViews() << " child views\n";

                // Log full view hierarchy
                log << "\n--- VIEW HIERARCHY ---\n";
                logViewHierarchy(frame, log, 0);

                // Find and log Mode control (tag 2)
                log << "\n--- MODE CONTROL SEARCH ---\n";
                auto* modeControl = findControlByTag(frame, kModeId);
                if (modeControl) {
                    log << "Found Mode control at tag " << kModeId << "\n";
                    log << "  Value: " << modeControl->getValue() << "\n";
                    log << "  ValueNormalized: " << modeControl->getValueNormalized() << "\n";
                    if (auto* optMenu = dynamic_cast<VSTGUI::COptionMenu*>(modeControl)) {
                        log << "  Type: COptionMenu\n";
                        log << "  Current index: " << optMenu->getCurrentIndex() << "\n";
                        log << "  Nb entries: " << optMenu->getNbEntries() << "\n";
                    }
                } else {
                    log << "ERROR: Mode control (tag " << kModeId << ") NOT FOUND!\n";
                }

                log << "\n--- END OF DIDOPEN LOG ---\n";
            } else {
                log << "ERROR: Frame is NULL!\n";
            }
        } else {
            log << "ERROR: Editor pointer is NULL!\n";
        }

        log.flush();
    }
#endif
}

// Helper to safely deactivate a visibility controller
static void deactivateController(Steinberg::IPtr<Steinberg::FObject>& controller) {
    if (controller) {
        if (auto* vc = dynamic_cast<VisibilityController*>(controller.get())) {
            vc->deactivate();
        } else if (auto* cvc = dynamic_cast<CompoundVisibilityController*>(controller.get())) {
            cvc->deactivate();
        }
    }
}

void Controller::willClose(VSTGUI::VST3Editor* editor) {
    // Called before editor closes
    (void)editor;

    // PHASE 1: Deactivate ALL visibility controllers FIRST
    // This ensures any in-flight or pending deferred updates will be safely ignored.
    // The atomic isActive_ flag is checked at the very start of update().
    deactivateController(digitalDelayTimeVisibilityController_);
    deactivateController(digitalAgeVisibilityController_);
    deactivateController(pingPongDelayTimeVisibilityController_);
    deactivateController(granularDelayTimeVisibilityController_);
    deactivateController(spectralBaseDelayVisibilityController_);
    deactivateController(shimmerDelayTimeVisibilityController_);
    deactivateController(bbdDelayTimeVisibilityController_);
    deactivateController(reverseChunkSizeVisibilityController_);
    // MultiTap has no BaseTime/Tempo visibility controllers (simplified design)
    deactivateController(freezeDelayTimeVisibilityController_);
    deactivateController(duckingDelayTimeVisibilityController_);
    deactivateController(granularNoteValueVisibilityController_);
    deactivateController(spectralNoteValueVisibilityController_);
    deactivateController(shimmerNoteValueVisibilityController_);
    deactivateController(bbdNoteValueVisibilityController_);
    deactivateController(digitalNoteValueVisibilityController_);
    deactivateController(pingPongNoteValueVisibilityController_);
    deactivateController(reverseNoteValueVisibilityController_);
    deactivateController(multitapNoteValueVisibilityController_);
    deactivateController(freezeNoteValueVisibilityController_);
    deactivateController(duckingNoteValueVisibilityController_);

    // PHASE 2: Clear activeEditor_ so any update() that passes the isActive check
    // will still return early when it checks for a valid editor.
    activeEditor_ = nullptr;

    // PHASE 3: Destroy visibility controllers (removes dependents and releases refs)
    // Now safe because: (1) isActive_ is false, (2) activeEditor_ is nullptr
    digitalDelayTimeVisibilityController_ = nullptr;
    digitalAgeVisibilityController_ = nullptr;
    pingPongDelayTimeVisibilityController_ = nullptr;
    granularDelayTimeVisibilityController_ = nullptr;
    spectralBaseDelayVisibilityController_ = nullptr;  // spec 041

    // Tempo sync visibility controllers
    shimmerDelayTimeVisibilityController_ = nullptr;
    bbdDelayTimeVisibilityController_ = nullptr;
    reverseChunkSizeVisibilityController_ = nullptr;
    // MultiTap has no BaseTime/Tempo visibility controllers (simplified design)
    freezeDelayTimeVisibilityController_ = nullptr;
    duckingDelayTimeVisibilityController_ = nullptr;

    // NoteValue visibility controllers
    granularNoteValueVisibilityController_ = nullptr;
    spectralNoteValueVisibilityController_ = nullptr;
    shimmerNoteValueVisibilityController_ = nullptr;
    bbdNoteValueVisibilityController_ = nullptr;
    digitalNoteValueVisibilityController_ = nullptr;
    pingPongNoteValueVisibilityController_ = nullptr;
    reverseNoteValueVisibilityController_ = nullptr;
    multitapNoteValueVisibilityController_ = nullptr;
    freezeNoteValueVisibilityController_ = nullptr;
    duckingNoteValueVisibilityController_ = nullptr;

    // Preset browser view is owned by the frame and will be cleaned up automatically
    presetBrowserView_ = nullptr;
    savePresetDialogView_ = nullptr;

    // TapPatternEditor is owned by the frame and will be cleaned up automatically
    tapPatternEditor_ = nullptr;
}

// ==============================================================================
// Preset Browser (Spec 042)
// ==============================================================================

void Controller::openPresetBrowser() {
    if (presetBrowserView_ && !presetBrowserView_->isOpen()) {
        // Get current mode from parameter
        int currentMode = -1;  // Default to "All"
        if (auto* modeParam = getParameterObject(kModeId)) {
            currentMode = static_cast<int>(modeParam->toPlain(modeParam->getNormalized()));
        }

        presetBrowserView_->open(currentMode);
    }
}

void Controller::openSavePresetDialog() {
    if (savePresetDialogView_ && !savePresetDialogView_->isOpen()) {
        // Get current mode from parameter
        int currentMode = -1;  // Default to "All"
        if (auto* modeParam = getParameterObject(kModeId)) {
            currentMode = static_cast<int>(modeParam->toPlain(modeParam->getNormalized()));
        }

        savePresetDialogView_->open(currentMode);
    }
}

void Controller::closePresetBrowser() {
    if (presetBrowserView_ && presetBrowserView_->isOpen()) {
        presetBrowserView_->close();
    }
}

// ==============================================================================
// State Serialization for Preset Saving
// ==============================================================================

Steinberg::MemoryStream* Controller::createComponentStateStream() {
    // Create a memory stream and serialize current parameter values
    // in the same format as Processor::getState()
    auto* stream = new Steinberg::MemoryStream();
    Steinberg::IBStreamer streamer(stream, kLittleEndian);

    // Helper to get denormalized float from controller parameter
    auto getFloat = [this](Steinberg::Vst::ParamID id, float defaultVal, float scale = 1.0f) -> float {
        if (auto* param = getParameterObject(id)) {
            return static_cast<float>(param->toPlain(param->getNormalized())) * scale;
        }
        return defaultVal;
    };

    // Helper to get int32 from controller parameter
    auto getInt32 = [this](Steinberg::Vst::ParamID id, Steinberg::int32 defaultVal) -> Steinberg::int32 {
        if (auto* param = getParameterObject(id)) {
            return static_cast<Steinberg::int32>(param->toPlain(param->getNormalized()));
        }
        return defaultVal;
    };

    // Write global parameters (must match Processor::getState order)
    // Gain: normalized 0-1 maps to 0-2 linear
    float gain = static_cast<float>(getParamNormalized(kGainId) * 2.0);
    streamer.writeFloat(gain);

    // Mode (0-10)
    Steinberg::int32 mode = getInt32(kModeId, 0);
    streamer.writeInt32(mode);

    // Granular params - must match saveGranularParams order exactly
    streamer.writeFloat(getFloat(kGranularGrainSizeId, 100.0f));
    streamer.writeFloat(getFloat(kGranularDensityId, 10.0f));
    streamer.writeFloat(getFloat(kGranularDelayTimeId, 500.0f));
    streamer.writeFloat(getFloat(kGranularPitchId, 0.0f));
    streamer.writeFloat(getFloat(kGranularPitchSprayId, 0.0f));
    streamer.writeFloat(getFloat(kGranularPositionSprayId, 0.0f));
    streamer.writeFloat(getFloat(kGranularPanSprayId, 0.0f));
    streamer.writeFloat(getFloat(kGranularReverseProbId, 0.0f));
    streamer.writeInt32(getInt32(kGranularFreezeId, 0));
    streamer.writeFloat(getFloat(kGranularFeedbackId, 0.5f));
    streamer.writeFloat(getFloat(kGranularMixId, 0.5f));
    streamer.writeInt32(getInt32(kGranularEnvelopeTypeId, 0));
    streamer.writeInt32(getInt32(kGranularTimeModeId, 0));
    streamer.writeInt32(getInt32(kGranularNoteValueId, 4));
    streamer.writeFloat(getFloat(kGranularJitterId, 0.0f));
    streamer.writeInt32(getInt32(kGranularPitchQuantId, 0));
    streamer.writeFloat(getFloat(kGranularTextureId, 0.0f));
    streamer.writeFloat(getFloat(kGranularStereoWidthId, 0.0f));

    // Spectral params - must match saveSpectralParams order exactly
    streamer.writeInt32(getInt32(kSpectralFFTSizeId, 2048));
    streamer.writeFloat(getFloat(kSpectralBaseDelayId, 250.0f));
    streamer.writeFloat(getFloat(kSpectralSpreadId, 500.0f));
    streamer.writeInt32(getInt32(kSpectralSpreadDirectionId, 0));
    streamer.writeFloat(getFloat(kSpectralFeedbackId, 0.5f));
    streamer.writeFloat(getFloat(kSpectralFeedbackTiltId, 0.0f));
    streamer.writeInt32(getInt32(kSpectralFreezeId, 0));
    streamer.writeFloat(getFloat(kSpectralDiffusionId, 0.5f));
    streamer.writeFloat(getFloat(kSpectralMixId, 50.0f));
    streamer.writeInt32(getInt32(kSpectralSpreadCurveId, 0));
    streamer.writeFloat(getFloat(kSpectralStereoWidthId, 0.5f));
    streamer.writeInt32(getInt32(kSpectralTimeModeId, 0));
    streamer.writeInt32(getInt32(kSpectralNoteValueId, 4));

    // Ducking params - must match saveDuckingParams order exactly
    streamer.writeInt32(getInt32(kDuckingEnabledId, 0));
    streamer.writeFloat(getFloat(kDuckingThresholdId, -30.0f));
    streamer.writeFloat(getFloat(kDuckingDuckAmountId, 50.0f));
    streamer.writeFloat(getFloat(kDuckingAttackTimeId, 10.0f));
    streamer.writeFloat(getFloat(kDuckingReleaseTimeId, 200.0f));
    streamer.writeFloat(getFloat(kDuckingHoldTimeId, 50.0f));
    streamer.writeInt32(getInt32(kDuckingDuckTargetId, 0));
    streamer.writeInt32(getInt32(kDuckingSidechainFilterEnabledId, 0));
    streamer.writeFloat(getFloat(kDuckingSidechainFilterCutoffId, 80.0f));
    streamer.writeFloat(getFloat(kDuckingDelayTimeId, 500.0f));
    streamer.writeFloat(getFloat(kDuckingFeedbackId, 0.0f));
    streamer.writeFloat(getFloat(kDuckingMixId, 50.0f));

    // Freeze params - must match saveFreezeParams order exactly
    streamer.writeInt32(getInt32(kFreezeEnabledId, 0));
    streamer.writeFloat(getFloat(kFreezeDelayTimeId, 500.0f));
    streamer.writeFloat(getFloat(kFreezeFeedbackId, 0.5f));
    streamer.writeFloat(getFloat(kFreezePitchSemitonesId, 0.0f));
    streamer.writeFloat(getFloat(kFreezePitchCentsId, 0.0f));
    streamer.writeFloat(getFloat(kFreezeShimmerMixId, 0.0f));
    streamer.writeFloat(getFloat(kFreezeDecayId, 0.5f));
    streamer.writeFloat(getFloat(kFreezeDiffusionAmountId, 0.3f));
    streamer.writeFloat(getFloat(kFreezeDiffusionSizeId, 0.5f));
    streamer.writeInt32(getInt32(kFreezeFilterEnabledId, 0));
    streamer.writeInt32(getInt32(kFreezeFilterTypeId, 0));
    streamer.writeFloat(getFloat(kFreezeFilterCutoffId, 1000.0f));
    streamer.writeFloat(getFloat(kFreezeMixId, 0.5f));

    // Reverse params - must match saveReverseParams order exactly
    streamer.writeFloat(getFloat(kReverseChunkSizeId, 500.0f));
    streamer.writeFloat(getFloat(kReverseCrossfadeId, 50.0f));
    streamer.writeInt32(getInt32(kReversePlaybackModeId, 0));
    streamer.writeFloat(getFloat(kReverseFeedbackId, 0.0f));
    streamer.writeInt32(getInt32(kReverseFilterEnabledId, 0));
    streamer.writeFloat(getFloat(kReverseFilterCutoffId, 4000.0f));
    streamer.writeInt32(getInt32(kReverseFilterTypeId, 0));
    streamer.writeFloat(getFloat(kReverseMixId, 0.5f));

    // Shimmer params - must match saveShimmerParams order exactly
    streamer.writeFloat(getFloat(kShimmerDelayTimeId, 500.0f));
    streamer.writeFloat(getFloat(kShimmerPitchSemitonesId, 12.0f));
    streamer.writeFloat(getFloat(kShimmerPitchCentsId, 0.0f));
    streamer.writeFloat(getFloat(kShimmerPitchBlendId, 100.0f));
    streamer.writeFloat(getFloat(kShimmerFeedbackId, 0.5f));
    streamer.writeFloat(getFloat(kShimmerDiffusionAmountId, 50.0f));
    streamer.writeFloat(getFloat(kShimmerDiffusionSizeId, 50.0f));
    streamer.writeInt32(getInt32(kShimmerFilterEnabledId, 0));
    streamer.writeFloat(getFloat(kShimmerFilterCutoffId, 4000.0f));
    streamer.writeFloat(getFloat(kShimmerMixId, 50.0f));

    // Tape params - must match saveTapeParams order exactly
    streamer.writeFloat(getFloat(kTapeMotorSpeedId, 500.0f));
    streamer.writeFloat(getFloat(kTapeMotorInertiaId, 300.0f));
    streamer.writeFloat(getFloat(kTapeWearId, 0.3f));
    streamer.writeFloat(getFloat(kTapeSaturationId, 0.5f));
    streamer.writeFloat(getFloat(kTapeAgeId, 0.3f));
    streamer.writeInt32(getInt32(kTapeSpliceEnabledId, 0));
    streamer.writeFloat(getFloat(kTapeSpliceIntensityId, 0.5f));
    streamer.writeFloat(getFloat(kTapeFeedbackId, 0.4f));
    streamer.writeFloat(getFloat(kTapeMixId, 0.5f));
    streamer.writeInt32(getInt32(kTapeHead1EnabledId, 1));
    streamer.writeInt32(getInt32(kTapeHead2EnabledId, 0));
    streamer.writeInt32(getInt32(kTapeHead3EnabledId, 0));
    streamer.writeFloat(getFloat(kTapeHead1LevelId, 1.0f));
    streamer.writeFloat(getFloat(kTapeHead2LevelId, 1.0f));
    streamer.writeFloat(getFloat(kTapeHead3LevelId, 1.0f));
    streamer.writeFloat(getFloat(kTapeHead1PanId, 0.0f));
    streamer.writeFloat(getFloat(kTapeHead2PanId, 0.0f));
    streamer.writeFloat(getFloat(kTapeHead3PanId, 0.0f));

    // BBD params - must match saveBBDParams order exactly
    streamer.writeFloat(getFloat(kBBDDelayTimeId, 300.0f));
    streamer.writeFloat(getFloat(kBBDFeedbackId, 0.4f));
    streamer.writeFloat(getFloat(kBBDModDepthId, 0.0f));
    streamer.writeFloat(getFloat(kBBDModRateId, 0.5f));
    streamer.writeFloat(getFloat(kBBDAgeId, 0.2f));
    streamer.writeInt32(getInt32(kBBDEraId, 0));
    streamer.writeFloat(getFloat(kBBDMixId, 0.5f));

    // Digital params - must match saveDigitalParams order exactly
    streamer.writeFloat(getFloat(kDigitalDelayTimeId, 500.0f));
    streamer.writeInt32(getInt32(kDigitalTimeModeId, 0));
    streamer.writeInt32(getInt32(kDigitalNoteValueId, 4));
    streamer.writeFloat(getFloat(kDigitalFeedbackId, 0.5f));
    streamer.writeInt32(getInt32(kDigitalLimiterCharacterId, 0));
    streamer.writeInt32(getInt32(kDigitalEraId, 0));
    streamer.writeFloat(getFloat(kDigitalAgeId, 0.0f));
    streamer.writeFloat(getFloat(kDigitalModDepthId, 0.0f));
    streamer.writeFloat(getFloat(kDigitalModRateId, 0.5f));
    streamer.writeInt32(getInt32(kDigitalModWaveformId, 0));
    streamer.writeFloat(getFloat(kDigitalMixId, 0.5f));
    streamer.writeFloat(getFloat(kDigitalWidthId, 100.0f));

    // PingPong params - must match savePingPongParams order exactly
    streamer.writeFloat(getFloat(kPingPongDelayTimeId, 500.0f));
    streamer.writeInt32(getInt32(kPingPongTimeModeId, 1));
    streamer.writeInt32(getInt32(kPingPongNoteValueId, 4));
    streamer.writeInt32(getInt32(kPingPongLRRatioId, 0));
    streamer.writeFloat(getFloat(kPingPongFeedbackId, 0.5f));
    streamer.writeFloat(getFloat(kPingPongCrossFeedbackId, 1.0f));
    streamer.writeFloat(getFloat(kPingPongWidthId, 100.0f));
    streamer.writeFloat(getFloat(kPingPongModDepthId, 0.0f));
    streamer.writeFloat(getFloat(kPingPongModRateId, 1.0f));
    streamer.writeFloat(getFloat(kPingPongMixId, 0.5f));

    // MultiTap params - must match saveMultiTapParams order exactly
    // Simplified design: No TimeMode, BaseTime, or Tempo parameters
    streamer.writeInt32(getInt32(kMultiTapNoteValueId, 2));      // Default: Quarter
    streamer.writeInt32(getInt32(kMultiTapNoteModifierId, 0));   // Default: None
    streamer.writeInt32(getInt32(kMultiTapTimingPatternId, 2));
    streamer.writeInt32(getInt32(kMultiTapSpatialPatternId, 2));
    streamer.writeInt32(getInt32(kMultiTapTapCountId, 4));
    streamer.writeFloat(getFloat(kMultiTapFeedbackId, 0.5f));
    streamer.writeFloat(getFloat(kMultiTapFeedbackLPCutoffId, 20000.0f));
    streamer.writeFloat(getFloat(kMultiTapFeedbackHPCutoffId, 20.0f));
    streamer.writeFloat(getFloat(kMultiTapMorphTimeId, 500.0f));
    streamer.writeFloat(getFloat(kMultiTapMixId, 50.0f));

    // Custom Pattern Data (spec 046)
    for (int i = 0; i < 16; ++i) {
        float defaultTime = static_cast<float>(i + 1) / 17.0f;
        streamer.writeFloat(getFloat(kMultiTapCustomTime0Id + i, defaultTime));
    }
    for (int i = 0; i < 16; ++i) {
        streamer.writeFloat(getFloat(kMultiTapCustomLevel0Id + i, 1.0f));
    }
    streamer.writeInt32(getInt32(kMultiTapSnapDivisionId, 0));   // Default: Off

    // Seek to beginning so the stream can be read
    stream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);

    return stream;
}

// ==============================================================================
// Preset Loading Helpers
// ==============================================================================

void Controller::editParamWithNotify(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value) {
    using namespace Steinberg::Vst;

    // Clamp value to valid range
    value = std::max(0.0, std::min(1.0, value));

    // Full edit cycle to notify host of parameter change
    beginEdit(id);
    setParamNormalized(id, value);
    performEdit(id, value);
    endEdit(id);
}

bool Controller::loadComponentStateWithNotify(Steinberg::IBStream* state) {
    // ==========================================================================
    // Load component state with host notification
    // Uses the same loadXxxParamsToController template functions as setComponentState
    // to ensure parsing logic is never duplicated
    // ==========================================================================

    if (!state) {
        return false;
    }

    Steinberg::IBStreamer streamer(state, kLittleEndian);
    Steinberg::int32 intVal = 0;
    float floatVal = 0.0f;

    // Lambda to call editParamWithNotify (captures this)
    auto setParamWithNotify = [this](Steinberg::Vst::ParamID id, double val) {
        editParamWithNotify(id, val);
    };

    // Global parameters (not part of mode params, handled directly)
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kGainId, static_cast<double>(floatVal / 2.0f));
    }
    if (streamer.readInt32(intVal)) {
        editParamWithNotify(kModeId, static_cast<double>(intVal) / 10.0);
    }

    // All mode params use the shared template functions
    // This ensures loadComponentStateWithNotify and syncXxxParamsToController
    // use identical parsing logic - eliminating the bug class where they get out of sync
    loadGranularParamsToController(streamer, setParamWithNotify);
    loadSpectralParamsToController(streamer, setParamWithNotify);
    loadDuckingParamsToController(streamer, setParamWithNotify);
    loadFreezeParamsToController(streamer, setParamWithNotify);
    loadReverseParamsToController(streamer, setParamWithNotify);
    loadShimmerParamsToController(streamer, setParamWithNotify);
    loadTapeParamsToController(streamer, setParamWithNotify);
    loadBBDParamsToController(streamer, setParamWithNotify);
    loadDigitalParamsToController(streamer, setParamWithNotify);
    loadPingPongParamsToController(streamer, setParamWithNotify);
    loadMultiTapParamsToController(streamer, setParamWithNotify);

    return true;
}

} // namespace Iterum
