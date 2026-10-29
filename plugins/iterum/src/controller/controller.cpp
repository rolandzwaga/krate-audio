// ==============================================================================
// Edit Controller Implementation
// ==============================================================================

#include "controller.h"
#include "custom_views.h"
#include "delay_time_sync_controller.h"
#include "visibility_controller.h"
#include "plugin_ids.h"
#include "version.h"

#include "preset/preset_manager.h"
#include "preset/iterum_preset_config.h"
#include "update/iterum_update_config.h"
#include "ui/preset_browser_view.h"
#include "ui/save_preset_dialog_view.h"
#include "ui/tap_pattern_editor.h"
#include "ui/update_banner_view.h"

#include "public.sdk/source/vst/vstparameters.h"
#include "pluginterfaces/vst/ivstcomponent.h"

#include "vstgui/lib/cframe.h"
#include "vstgui/lib/platform/iplatformframe.h"
#include "vstgui/lib/controls/ccontrol.h"
#include "vstgui/lib/controls/coptionmenu.h"
#include "vstgui/lib/controls/ctextlabel.h"
#include "vstgui/uidescription/uiviewswitchcontainer.h"
#include "vstgui/uidescription/uiattributes.h"

#include <vector>
#include <atomic>

// First control with a given tag under @p container (a plain ViewIterator walk). Used by tapEditor() and by the
// debug logging below.
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

#endif

#include "parameters/bbd_params.h"
#include "parameters/digital_params.h"
#include "parameters/freeze_params.h"
#include "parameters/granular_params.h"
#include "parameters/multitap_params.h"
#include "parameters/pattern_calculator.h"
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
    // Set default to Digital (index 5) - normalized value = 5/9 - as both the
    // starting value and the registered default "reset to default" returns to
    modeParam->setNormalized(5.0 / 9.0);
    modeParam->getInfo().defaultNormalizedValue = 5.0 / 9.0;
    parameters.addParameter(modeParam);

    // ==========================================================================
    // Mode-Specific Parameter Registration
    // ==========================================================================

    registerGranularParams(parameters);  // Granular Delay (spec 034)
    registerSpectralParams(parameters);  // Spectral Delay (spec 033)
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
    presetManager_ = std::make_unique<Krate::Plugins::PresetManager>(makeIterumPresetConfig(), nullptr, this);

    // Set state provider callback for preset saving
    presetManager_->setStateProvider([this]() -> Steinberg::IBStream* {
        return this->createComponentStateStream();
    });

    // Set load provider callback for preset loading
    presetManager_->setLoadProvider(
        [this](Steinberg::IBStream* state,
               const Krate::Plugins::PresetInfo& /*info*/) -> bool {
            return this->loadComponentStateWithNotify(state);
        });

    // Update checker
    updateChecker_ = std::make_unique<Krate::Plugins::UpdateChecker>(makeIterumUpdateConfig());

    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API Controller::terminate() {
    updateChecker_.reset();
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

    bulkParamLoad_ = true;  // Suppress per-param view updates during bulk load
    FrameInvalidationGuard frameGuard(activeEditor_);  // Suppress VSTGUI invalidRect

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
        // Convert mode index (0-9) to normalized (0.0-1.0)
        setParamNormalized(kModeId, static_cast<double>(mode) / 9.0);
    }

    // ==========================================================================
    // Sync mode-specific parameters (order MUST match Processor::getState)
    // ==========================================================================

    syncGranularParamsToController(streamer, *this);  // Granular Delay (spec 034)
    syncSpectralParamsToController(streamer, *this);  // Spectral Delay (spec 033)
    syncFreezeParamsToController(streamer, *this);    // Freeze Mode (spec 031)
    syncReverseParamsToController(streamer, *this);   // Reverse Delay (spec 030)
    syncShimmerParamsToController(streamer, *this);   // Shimmer Delay (spec 029)
    syncTapeParamsToController(streamer, *this);      // Tape Delay (spec 024)
    syncBBDParamsToController(streamer, *this);       // BBD Delay (spec 025)
    syncDigitalParamsToController(streamer, *this);   // Digital Delay (spec 026)
    syncPingPongParamsToController(streamer, *this);  // PingPong Delay (spec 027)
    syncMultiTapParamsToController(streamer, *this);  // MultiTap Delay (spec 028)
    syncTapeSyncParamsToController(streamer, *this);  // Tape tempo sync (after every mode)
    syncMultiTapLengthParamsToController(streamer, *this);  // MultiTap Custom Length (the last field)

    bulkParamLoad_ = false;  // Re-enable per-param view updates
    syncAllViews();           // Single batch sync of all custom views

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
                std::snprintf(text, sizeof(text), "%.1f dB", dB);

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
        // Custom Time is shown in ms of the span the Length and the reported Tempo give (design 4.4)
        result = formatMultiTapParam(id, valueNormalized, string,
                                     getParamNormalized(kMultiTapCustomLengthId),
                                     getParamNormalized(kMultiTapTempoId));
    }
    else if (id >= kFreezeBaseId && id <= kFreezeEndId) {
        // Hits and Rotation are counted against the current Steps (handleFreezeParamChange does the same).
        result = formatFreezeParam(id, valueNormalized, string,
                                   getParamNormalized(kFreezeEuclideanStepsId));
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
            // Custom Time "750.0 ms" and Custom Level "-6.0 dB" parse back through the formatter's inverse (4.4)
            if (parseMultiTapParam(id, string, getParamNormalized(kMultiTapCustomLengthId),
                                   getParamNormalized(kMultiTapTempoId), valueNormalized) == Steinberg::kResultTrue) {
                return Steinberg::kResultTrue;
            }
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

    // The count before this set, for the placement a user Taps gesture triggers in performEdit (design 3.3)
    if (id == kMultiTapTapCountId) {
        tapCountBeforeSet_ = static_cast<int>(2.0 + getParamNormalized(kMultiTapTapCountId) * 14.0 + 0.5);
    }

    // Call base class - this is the ONLY thing that actually happens
    // (the tap editor's bridge receives the change synchronously inside it, design 3.6)
    auto result = EditControllerEx1::setParamNormalized(id, value);

    // The Hits and Rotation readouts are counted against the current Steps (formatFreezeParam), but VST3Editor
    // redraws a CParamDisplay only when its own parameter notifies: tell both that their text changed.
    if (id == kFreezeEuclideanStepsId) {
        for (auto dependentId : {kFreezeEuclideanHitsId, kFreezeEuclideanRotationId}) {
            if (auto* param = getParameterObject(dependentId)) {
                param->deferUpdate();
            }
        }
    }

    // NOTE: Conditional visibility is handled by VisibilityController instances, and the tap pattern editor by
    // its TapEditorParamBridge, via the IDependent mechanism (see didOpen). No view pointer is cached here.

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

Steinberg::tresult Controller::performEdit(Steinberg::Vst::ParamID tag,
                                           Steinberg::Vst::ParamValue valueNormalized) {
    const auto result = EditControllerEx1::performEdit(tag, valueNormalized);

    // A Taps increase by a user gesture, while the editor shows the Custom pattern: the view places the taps this
    // gesture revealed whose stored place is taken. Automation and bulk loads never call performEdit, so a count
    // they raise is only revealed, never placed; a decrease places nothing (design 3.2, 3.3).
    if (tag == kMultiTapTapCountId && !bulkParamLoad_ &&
        getParamNormalized(kMultiTapTimingPatternId) >= 0.99) {
        const int newCount = static_cast<int>(2.0 + valueNormalized * 14.0 + 0.5);
        if (newCount > tapCountBeforeSet_) {
            if (auto* view = tapEditor()) {
                view->placeRevealedTaps(static_cast<size_t>(tapCountBeforeSet_), static_cast<size_t>(newCount));
            }
        }
    }
    return result;
}

// ==============================================================================
// VST3EditorDelegate
// ==============================================================================

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

    // Copy Pattern Button (Spec 046 - User Story 4)
    if (VSTGUI::UTF8StringView(name) == "CopyPatternButton") {
        VSTGUI::CPoint origin(0, 0);
        VSTGUI::CPoint size(90, 22);
        attributes.getPointAttribute("origin", origin);
        attributes.getPointAttribute("size", size);
        VSTGUI::CRect rect(origin.x, origin.y, origin.x + size.x, origin.y + size.y);
        auto* button = new CopyPatternButton(rect, this);
        button->setTag(kMultiTapCopyPatternButtonTagId);  // For visibility control
        return button;
    }

    // Reset Pattern Button (Spec 046 - T075)
    if (VSTGUI::UTF8StringView(name) == "ResetPatternButton") {
        VSTGUI::CPoint origin(0, 0);
        VSTGUI::CPoint size(60, 22);
        attributes.getPointAttribute("origin", origin);
        attributes.getPointAttribute("size", size);
        VSTGUI::CRect rect(origin.x, origin.y, origin.x + size.x, origin.y + size.y);
        auto* button = new ResetPatternButton(rect, this);
        button->setTag(kMultiTapResetPatternButtonTagId);  // For visibility control
        return button;
    }

    // TapPatternEditor (Spec 046) - Custom tap pattern visual editor
    if (VSTGUI::UTF8StringView(name) == "TapPatternEditor") {
        VSTGUI::CPoint origin(0, 0);
        VSTGUI::CPoint size(400, 150);
        attributes.getPointAttribute("origin", origin);
        attributes.getPointAttribute("size", size);
        VSTGUI::CRect rect(origin.x, origin.y, origin.x + size.x, origin.y + size.y);

        auto* patternEditor = new TapPatternEditor(rect);

        // Tag 920 is not a parameter: the view is found by it (tapEditor()), and binds to nothing
        patternEditor->setTag(kMultiTapPatternEditorTagId);

        // Colour and font tokens from the uidesc (design 5.7); the Palette's defaults stand in for a missing token
        if (description) {
            TapPatternEditor::Palette palette;
            const auto readColor = [description](const char* name, VSTGUI::CColor& out) {
                VSTGUI::CColor color;
                if (description->getColor(name, color)) out = color;
            };
            readColor("slider-track", palette.sliderTrack);
            readColor("border", palette.border);
            readColor("slider-frame", palette.sliderFrame);
            readColor("accent", palette.accent);
            readColor("accent-dim", palette.accentDim);
            readColor("text", palette.text);
            readColor("text-dim", palette.textDim);
            readColor("text-light", palette.textLight);
            readColor("background", palette.background);
            if (auto* font = description->getFont("value-font")) {
                palette.valueFont = font;
            }
            patternEditor->setPalette(palette);
        }

        // Every write of the view: begin / perform / end on the tap's own parameter (design 3.5). perform sets the
        // parameter first, so the bridge reflects it at once, then tells the host.
        TapEditSink sink;
        sink.begin = [this](Steinberg::Vst::ParamID paramId) { beginEdit(paramId); };
        sink.end = [this](Steinberg::Vst::ParamID paramId) { endEdit(paramId); };
        sink.perform = [this](Steinberg::Vst::ParamID paramId, float normalizedValue) {
            if (auto* param = getParameterObject(paramId)) {
                param->setNormalized(static_cast<Steinberg::Vst::ParamValue>(normalizedValue));
                performEdit(paramId, static_cast<Steinberg::Vst::ParamValue>(normalizedValue));
            }
        };
        patternEditor->setEditSink(std::move(sink));

        // Every value the view shows, then the read-only pattern. The reveal writes nothing, so the order of
        // the count and the tap values no longer matters (design 3.6).
        pushAllToTapEditor(*patternEditor);

        return patternEditor;
    }

    // The update banner, the manual check button and the version label: the shared
    // views every Krate editor carries.
    if (auto* shared = Krate::Plugins::createSharedEditorView(
            name, attributes, updateChecker_.get(), VERSION_STR, updateBannerView_)) {
        return shared;
    }

    return nullptr;
}

// ==============================================================================
// createSubController - Delay Time Sync sub-controllers
// ==============================================================================
VSTGUI::IController* Controller::createSubController(
    VSTGUI::UTF8StringPtr name,
    const VSTGUI::IUIDescription* /*description*/,
    [[maybe_unused]] VSTGUI::VST3Editor* editor)
{
    if (!name)
        return nullptr;

    const std::string controllerName(name);

    // VSTGUI takes ownership of the returned pointer (raw new is mandated)
    // editor (VST3Editor*) is the IController parent for delegation
    if (controllerName == "GranularTimeSyncController")
        return new DelayTimeSyncController(kGranularDelayTimeId, kGranularTimeModeId, kGranularNoteValueId, editor);
    if (controllerName == "SpectralTimeSyncController")
        return new DelayTimeSyncController(kSpectralBaseDelayId, kSpectralTimeModeId, kSpectralNoteValueId, editor);
    if (controllerName == "ShimmerTimeSyncController")
        return new DelayTimeSyncController(kShimmerDelayTimeId, kShimmerTimeModeId, kShimmerNoteValueId, editor);
    if (controllerName == "TapeTimeSyncController")
        return new DelayTimeSyncController(kTapeMotorSpeedId, kTapeTimeModeId, kTapeNoteValueId, editor);
    if (controllerName == "BBDTimeSyncController")
        return new DelayTimeSyncController(kBBDDelayTimeId, kBBDTimeModeId, kBBDNoteValueId, editor);
    if (controllerName == "DigitalTimeSyncController")
        return new DelayTimeSyncController(kDigitalDelayTimeId, kDigitalTimeModeId, kDigitalNoteValueId, editor);
    if (controllerName == "PingPongTimeSyncController")
        return new DelayTimeSyncController(kPingPongDelayTimeId, kPingPongTimeModeId, kPingPongNoteValueId, editor);
    if (controllerName == "ReverseTimeSyncController")
        return new DelayTimeSyncController(kReverseChunkSizeId, kReverseTimeModeId, kReverseNoteValueId, editor);

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
            // Conditional Visibility Controllers
            // =====================================================================
            // NOTE: Delay time / note value visibility is now handled by
            // DelayTimeSyncController sub-controllers (tag swapping pattern).
            // Only non-time-mode visibility controllers remain here.
            // =====================================================================

            // Hide Age label + control when Era is "Pristine" (< 0.25)
            // Era values: 0 = Pristine (0.0), 1 = 80s (0.5), 2 = LoFi (1.0)
            // Show Age when Era >= 0.25 (80s or LoFi)
            if (auto* digitalEra = getParameterObject(kDigitalEraId)) {
                digitalAgeVisibilityController_ = new VisibilityController(
                    &activeEditor_, digitalEra, {9902, kDigitalAgeId}, 0.25f, false);
            }

            // MultiTap Note Value: Show when Pattern is Mathematical (14-18), NOT Custom (19)
            // Patterns 14-18 need Note Value for baseTimeMs calculation.
            // Pattern 19 (Custom) has its own editor and doesn't use Note Value.
            // Uses range mode: show when 14/19 <= value < 19/19
            if (auto* multitapPattern = getParameterObject(kMultiTapTimingPatternId)) {
                multitapNoteValueVisibilityController_ = new ContainerVisibilityController(
                    &activeEditor_, multitapPattern,
                    kMultiTapNoteValueId,  // Find container by child control's tag (Note Value dropdown)
                    14.0f / 19.0f, 19.0f / 19.0f);  // NOLINT(misc-redundant-expression) Range mode: patterns 14-18 only

                // TAP PATTERN fieldset (design section 6): the fieldset and the editor (920) are always visible
                // (the editor shows every pattern, read-only unless Custom); Snap (913), Length (914), their
                // labels (922) and Reset (923) show only when pattern == Custom (index 19, normalized 1.0;
                // pattern 18 is at 18/19 = 0.947, so the threshold 0.99 selects only 19)
                customPatternControlsVisibilityController_ = new VisibilityController(
                    &activeEditor_, multitapPattern,
                    {kMultiTapSnapDivisionId, kMultiTapCustomLengthId, kMultiTapPatternEditorLabelTagId,
                     kMultiTapResetPatternButtonTagId},
                    0.99f, false);  // Show when pattern >= 0.99 (Custom pattern only)

                // Copy Pattern button: visible when NOT Custom pattern (patterns 0-18)
                // Button allows copying current pattern to Custom before editing
                copyPatternButtonVisibilityController_ = new VisibilityController(
                    &activeEditor_, multitapPattern,
                    {kMultiTapCopyPatternButtonTagId},  // Button tag
                    0.99f, true);  // Show when pattern < 0.99 (NOT Custom)
            }

            // =====================================================================
            // Freeze Mode Pattern Container Visibility (Spec 069)
            // =====================================================================
            // Show pattern-specific containers based on FreezePatternType selection.
            // 4 patterns: Euclidean(0), GranularScatter(1), HarmonicDrones(2), NoiseBursts(3)
            // Normalized values: 0.0, 0.333, 0.667, 1.0
            // Range thresholds at midpoints: 0.166, 0.5, 0.833
            // =====================================================================
            if (auto* patternType = getParameterObject(kFreezePatternTypeId)) {
                // Euclidean container: visible when pattern type == 0 (value < 0.166)
                freezeEuclideanContainerController_ = new ContainerVisibilityController(
                    &activeEditor_, patternType,
                    kFreezeEuclideanStepsId,  // Child control tag to find container
                    -0.01f, 0.166f);  // Range: show when value < 0.166

                // Granular Scatter container: visible when pattern type == 1 (0.166 <= value < 0.5)
                freezeGranularContainerController_ = new ContainerVisibilityController(
                    &activeEditor_, patternType,
                    kFreezeGranularDensityId,  // Child control tag
                    0.166f, 0.5f);  // Range: show when 0.166 <= value < 0.5

                // Harmonic Drones container: visible when pattern type == 2 (0.5 <= value < 0.833)
                freezeDronesContainerController_ = new ContainerVisibilityController(
                    &activeEditor_, patternType,
                    kFreezeDroneVoiceCountId,  // Child control tag
                    0.5f, 0.833f);  // Range: show when 0.5 <= value < 0.833

                // Noise Bursts container: visible when pattern type == 3 (value >= 0.833)
                freezeNoiseBurstsContainerController_ = new ContainerVisibilityController(
                    &activeEditor_, patternType,
                    kFreezeNoiseColorId,  // Child control tag
                    0.833f, 1.01f);  // Range: show when value >= 0.833
            }

            // =====================================================================
            // Tap pattern editor bridge (specs/iterum-tap-editor/design.md 3.6)
            // =====================================================================
            // Every parameter the editor shows reaches it synchronously, whoever set it (automation, host echo,
            // preset load after its bulk phase, the view's own sink). The view is looked up by tag each time.
            {
                std::vector<Steinberg::Vst::Parameter*> watched;
                for (Steinberg::Vst::ParamID id :
                     {kMultiTapTimingPatternId, kMultiTapSpatialPatternId, kMultiTapTapCountId, kMultiTapNoteValueId,
                      kMultiTapNoteModifierId, kMultiTapSnapDivisionId, kMultiTapCustomLengthId, kMultiTapTempoId}) {
                    watched.push_back(getParameterObject(id));
                }
                for (Steinberg::Vst::ParamID i = 0; i < kCustomPatternMaxTaps; ++i) {
                    watched.push_back(getParameterObject(kMultiTapCustomTime0Id + i));
                    watched.push_back(getParameterObject(kMultiTapCustomLevel0Id + i));
                }
                tapEditorBridge_ = Steinberg::owned(new TapEditorParamBridge(
                    std::move(watched), [this](Steinberg::Vst::ParamID id) { onTapEditorParamChanged(id); }));
            }

            // =====================================================================
            // Preset Browser View (Spec 042)
            // =====================================================================
            // Create the preset browser view as an overlay covering the full frame.
            // The view is initially hidden and shown via openPresetBrowser().
            // =====================================================================
            if (presetManager_) {
                auto frameSize = frame->getViewSize();
                presetBrowserView_ = new Krate::Plugins::PresetBrowserView(frameSize, presetManager_.get(), getIterumTabLabels());
                frame->addView(presetBrowserView_);

                // Save Preset Dialog - standalone dialog for quick save from main UI
                savePresetDialogView_ = new Krate::Plugins::SavePresetDialogView(frameSize, presetManager_.get());
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

    // Start update check and banner polling
    if (updateChecker_) {
        updateChecker_->checkForUpdate(false);
    }
    if (updateBannerView_) {
        updateBannerView_->startPolling();
    }
}

void Controller::willClose(VSTGUI::VST3Editor* editor) {
    // Called before editor closes
    (void)editor;

    // PHASE 1: Deactivate ALL visibility controllers FIRST
    // This ensures any in-flight or pending deferred updates will be safely ignored.
    // The atomic isActive_ flag is checked at the very start of update().
    // NOTE: Delay time / note value visibility controllers removed — now handled
    // by DelayTimeSyncController sub-controllers (tag swapping pattern).
    deactivateVisibilityController(digitalAgeVisibilityController_);
    deactivateVisibilityController(multitapNoteValueVisibilityController_);
    deactivateVisibilityController(customPatternControlsVisibilityController_);  // Spec 046
    deactivateVisibilityController(copyPatternButtonVisibilityController_);  // Spec 046
    if (tapEditorBridge_) {
        tapEditorBridge_->deactivate();  // the tap pattern editor's parameter bridge (design 3.6)
    }
    // Freeze mode pattern container visibility (Spec 069)
    deactivateVisibilityController(freezeEuclideanContainerController_);
    deactivateVisibilityController(freezeGranularContainerController_);
    deactivateVisibilityController(freezeDronesContainerController_);
    deactivateVisibilityController(freezeNoiseBurstsContainerController_);
    // PHASE 2: Clear activeEditor_ so any update() that passes the isActive check
    // will still return early when it checks for a valid editor.
    activeEditor_ = nullptr;

    // PHASE 3: Destroy visibility controllers (removes dependents and releases refs)
    // Now safe because: (1) isActive_ is false, (2) activeEditor_ is nullptr
    digitalAgeVisibilityController_ = nullptr;
    multitapNoteValueVisibilityController_ = nullptr;
    customPatternControlsVisibilityController_ = nullptr;  // Spec 046
    copyPatternButtonVisibilityController_ = nullptr;  // Spec 046
    tapEditorBridge_ = nullptr;
    // Freeze mode pattern container visibility (Spec 069)
    freezeEuclideanContainerController_ = nullptr;
    freezeGranularContainerController_ = nullptr;
    freezeDronesContainerController_ = nullptr;
    freezeNoiseBurstsContainerController_ = nullptr;
    // Preset browser view is owned by the frame and will be cleaned up automatically
    presetBrowserView_ = nullptr;
    savePresetDialogView_ = nullptr;

    // Stop update banner polling (view is owned by frame)
    if (updateBannerView_) {
        updateBannerView_->stopPolling();
        updateBannerView_ = nullptr;
    }
}

// ==============================================================================
// Preset Browser (Spec 042)
// ==============================================================================

void Controller::openPresetBrowser() {
    if (presetBrowserView_ && !presetBrowserView_->isOpen()) {
        // Get current mode as subcategory string
        std::string subcategory;
        if (auto* modeParam = getParameterObject(kModeId)) {
            int modeIndex = static_cast<int>(modeParam->toPlain(modeParam->getNormalized()));
            subcategory = delayModeToSubcategory(static_cast<DelayMode>(modeIndex));
        }

        presetBrowserView_->open(subcategory);
    }
}

void Controller::openSavePresetDialog() {
    if (savePresetDialogView_ && !savePresetDialogView_->isOpen()) {
        // Get current mode as subcategory string
        std::string subcategory;
        if (auto* modeParam = getParameterObject(kModeId)) {
            int modeIndex = static_cast<int>(modeParam->toPlain(modeParam->getNormalized()));
            subcategory = delayModeToSubcategory(static_cast<DelayMode>(modeIndex));
        }

        savePresetDialogView_->open(subcategory);
    }
}

void Controller::closePresetBrowser() {
    if (presetBrowserView_ && presetBrowserView_->isOpen()) {
        presetBrowserView_->close();
    }
}

// ==============================================================================
// Custom Pattern Editor - Copy from Pattern
// ==============================================================================

namespace {

/// Index of a normalized StringList value with @p maxIndex steps.
[[nodiscard]] int listIndex(double normalized, int maxIndex) {
    return std::clamp(static_cast<int>(normalized * maxIndex + 0.5), 0, maxIndex);
}

/// The Taps parameter (902) decoded as the processor decodes it: 2 + 14n (a plain Parameter: toPlain is identity).
[[nodiscard]] int tapCountFromNormalized(double normalized) {
    return std::clamp(static_cast<int>(2.0 + normalized * 14.0 + 0.5), 2, 16);
}

} // namespace

void Controller::copyCurrentPatternToCustom() {
    const int patternIndex = listIndex(getParamNormalized(kMultiTapTimingPatternId), 19);

    // Don't copy if already on Custom pattern (index 19)
    if (patternIndex == 19) {
        return;
    }

    // What the pattern plays at the tempo the processor last reported, on the Length the read-only display
    // shows (design 2.6 steps 1-4)
    const int tapCount = tapCountFromNormalized(getParamNormalized(kMultiTapTapCountId));
    const auto copy = customPatternFromPreset(
        patternIndex,
        listIndex(getParamNormalized(kMultiTapNoteValueId), 9),
        listIndex(getParamNormalized(kMultiTapNoteModifierId), 2),
        listIndex(getParamNormalized(kMultiTapSpatialPatternId), 6),
        static_cast<size_t>(tapCount),
        multiTapTempoFromNormalized(getParamNormalized(kMultiTapTempoId)));

    // Step 5: the n active times, the n active levels, Length, then Timing Pattern = Custom, each bracketed for
    // the host and set on the controller. Hidden taps keep their stored values. The view follows through the
    // bridge.
    for (size_t i = 0; i < copy.count; ++i) {
        editParamWithNotify(kMultiTapCustomTime0Id + static_cast<Steinberg::Vst::ParamID>(i),
                            static_cast<double>(copy.ratios[i]));
    }
    for (size_t i = 0; i < copy.count; ++i) {
        editParamWithNotify(kMultiTapCustomLevel0Id + static_cast<Steinberg::Vst::ParamID>(i),
                            static_cast<double>(copy.levels[i]));
    }
    editParamWithNotify(kMultiTapCustomLengthId,
                        static_cast<double>(static_cast<int>(copy.length)) / kCustomLengthMaxIndex);
    editParamWithNotify(kMultiTapTimingPatternId, 1.0);  // Custom: 19 / 19
}

void Controller::resetPatternToDefault() const {
    // The view writes every active tap bracketed through its sink (design 3.4)
    if (auto* view = tapEditor()) {
        view->resetToDefault();
    }
}

// ==============================================================================
// Tap pattern editor: lookup and pushes (specs/iterum-tap-editor/design.md 3.6, 5.9)
// ==============================================================================

TapPatternEditor* Controller::tapEditor() const {
    if (!activeEditor_) {
        return nullptr;
    }
    auto* frame = activeEditor_->getFrame();
    if (!frame) {
        return nullptr;
    }
    return dynamic_cast<TapPatternEditor*>(findControlByTag(frame, kMultiTapPatternEditorTagId));
}

void Controller::onTapEditorParamChanged(Steinberg::Vst::ParamID id) {
    // A bulk load pushes once, after the load (syncAllViews)
    if (bulkParamLoad_) {
        return;
    }
    if (auto* view = tapEditor()) {
        pushToTapEditor(id, *view);
    }
}

void Controller::pushToTapEditor(Steinberg::Vst::ParamID id, TapPatternEditor& view) {
    ++tapEditorIdPushCount_;
    pushToTapEditorImpl(id, view);

    // The read-only display is the preset pattern at the current tempo, timing and spatial pattern (design 5.9)
    switch (id) {
        case kMultiTapTimingPatternId:
        case kMultiTapSpatialPatternId:
        case kMultiTapTapCountId:
        case kMultiTapNoteValueId:
        case kMultiTapNoteModifierId:
        case kMultiTapTempoId:
            pushPresetPatternToTapEditor(view);
            break;
        default:
            break;
    }
}

void Controller::pushToTapEditorImpl(Steinberg::Vst::ParamID id, TapPatternEditor& view) {
    const double value = getParamNormalized(id);
    switch (id) {
        case kMultiTapSnapDivisionId:
            view.setSnapDivision(static_cast<SnapDivision>(listIndex(value, 21)));
            break;
        case kMultiTapTapCountId:
            view.setActiveTapCount(static_cast<size_t>(tapCountFromNormalized(value)));  // reveal only
            break;
        case kMultiTapCustomLengthId:
            view.setCustomLength(static_cast<CustomLength>(customLengthIndexFromNormalized(value)));
            break;
        case kMultiTapTempoId:
            view.setTempo(multiTapTempoFromNormalized(value));
            break;
        case kMultiTapTimingPatternId:
            view.setEditable(listIndex(value, 19) == 19);
            break;
        default:
            if (id >= kMultiTapCustomTime0Id && id <= kMultiTapCustomTime15Id) {
                view.setTapTimeRatio(id - kMultiTapCustomTime0Id, static_cast<float>(value));
            } else if (id >= kMultiTapCustomLevel0Id && id <= kMultiTapCustomLevel15Id) {
                view.setTapLevel(id - kMultiTapCustomLevel0Id, static_cast<float>(value));
            }
            break;
    }
}

void Controller::pushAllToTapEditor(TapPatternEditor& view) {
    ++tapEditorFullPushCount_;
    for (Steinberg::Vst::ParamID id : {kMultiTapSnapDivisionId, kMultiTapCustomLengthId, kMultiTapTempoId,
                                       kMultiTapTapCountId, kMultiTapTimingPatternId}) {
        pushToTapEditorImpl(id, view);
    }
    for (Steinberg::Vst::ParamID i = 0; i < kCustomPatternMaxTaps; ++i) {
        pushToTapEditorImpl(kMultiTapCustomTime0Id + i, view);
        pushToTapEditorImpl(kMultiTapCustomLevel0Id + i, view);
    }
    pushPresetPatternToTapEditor(view);
}

void Controller::pushPresetPatternToTapEditor(TapPatternEditor& view) {
    const int patternIndex = listIndex(getParamNormalized(kMultiTapTimingPatternId), 19);
    if (patternIndex == 19) {
        return;  // Custom: the editor shows the custom taps
    }
    const auto copy = customPatternFromPreset(
        patternIndex,
        listIndex(getParamNormalized(kMultiTapNoteValueId), 9),
        listIndex(getParamNormalized(kMultiTapNoteModifierId), 2),
        listIndex(getParamNormalized(kMultiTapSpatialPatternId), 6),
        static_cast<size_t>(tapCountFromNormalized(getParamNormalized(kMultiTapTapCountId))),
        multiTapTempoFromNormalized(getParamNormalized(kMultiTapTempoId)));
    view.setPresetPattern(copy.ratios, copy.levels, copy.count, copy.length);
}

// ==============================================================================
// State Serialization for Preset Saving
// ==============================================================================

Steinberg::MemoryStream* Controller::createComponentStateStream() {
    // Delegate to host via IComponent::getState() -- does NOT re-serialize
    // parameters from the controller. This guarantees the saved stream matches
    // Processor::getState() byte-for-byte (and avoids drift bugs such as the
    // legacy hand-written Freeze block, which wrote hardcoded placeholders
    // instead of the live freeze parameters). Caller owns the returned stream.
    Steinberg::FUnknownPtr<Steinberg::Vst::IComponent> component(getComponentHandler());
    if (!component)
        return nullptr;

    auto* stream = new Steinberg::MemoryStream();
    if (component->getState(stream) != Steinberg::kResultOk) {
        stream->release();
        return nullptr;
    }

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

    bulkParamLoad_ = true;  // Suppress per-param view updates during bulk load
    FrameInvalidationGuard frameGuard(activeEditor_);  // Suppress VSTGUI invalidRect

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
        editParamWithNotify(kModeId, static_cast<double>(intVal) / 9.0);
    }

    // All mode params use the shared template functions
    // This ensures loadComponentStateWithNotify and syncXxxParamsToController
    // use identical parsing logic - eliminating the bug class where they get out of sync
    loadGranularParamsToController(streamer, setParamWithNotify);
    loadSpectralParamsToController(streamer, setParamWithNotify);
    loadFreezeParamsToController(streamer, setParamWithNotify);
    loadReverseParamsToController(streamer, setParamWithNotify);
    loadShimmerParamsToController(streamer, setParamWithNotify);
    loadTapeParamsToController(streamer, setParamWithNotify);
    loadBBDParamsToController(streamer, setParamWithNotify);
    loadDigitalParamsToController(streamer, setParamWithNotify);
    loadPingPongParamsToController(streamer, setParamWithNotify);
    loadMultiTapParamsToController(streamer, setParamWithNotify);
    loadTapeSyncParamsToController(streamer, setParamWithNotify);
    loadMultiTapLengthParamsToController(streamer, setParamWithNotify);  // the last field

    bulkParamLoad_ = false;  // Re-enable per-param view updates
    syncAllViews();           // Single batch sync of all custom views

    return true;
}

// ==============================================================================
// Bulk Parameter Load - Batch View Sync
// ==============================================================================
// Called once after bulk parameter loads (setComponentState, loadComponentStateWithNotify)
// to sync all custom views from current parameter state. Replaces thousands of
// per-param invalidRect() calls with a single full-frame repaint.

void Controller::syncAllViews() {
    // The tap pattern editor skipped every per-id push during the load: one full push now (design 3.6)
    if (auto* view = tapEditor()) {
        pushAllToTapEditor(*view);
    }
}

} // namespace Iterum
