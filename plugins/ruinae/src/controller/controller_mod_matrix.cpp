// ==============================================================================
// Controller: Mod Matrix & Ring Indicator Wiring
// ==============================================================================
// Extracted from controller.cpp - handles wireModMatrixGrid(),
// syncModMatrixGrid(), pushAllGlobalRouteParams(), wireModRingIndicator(),
// rebuildRingIndicators(), selectModulationRoute().
// ==============================================================================

#include "controller.h"
#include "plugin_ids.h"
#include "ui/mod_matrix_grid.h"
#include "ui/mod_ring_indicator.h"

#include "pluginterfaces/base/ibstream.h"

#include "controller/mod_ring_dest_ids.h"

#include <cmath>

namespace Ruinae {

// Maps destination index to the actual VST parameter ID of that knob (mod ring sync).

static constexpr std::array<Steinberg::Vst::ParamID,
    Krate::Plugins::kNumGlobalDestinations> kGlobalDestParamIds = {{
    kGlobalFilterCutoffId,    // 0: Global Filter Cutoff
    kGlobalFilterResonanceId, // 1: Global Filter Resonance
    kMasterGainId,            // 2: Master Volume
    kDelayMixId,              // 3: Effect Mix
    kFilterCutoffId,          // 4: All Voice Filter Cutoff
    kMixerPositionId,         // 5: All Voice Morph Position
    kTranceGateRateId,        // 6: All Voice TranceGate Rate
    kMixerTiltId,             // 7: All Voice Spectral Tilt
    kFilterResonanceId,       // 8: All Voice Resonance
    kFilterEnvAmountId,       // 9: All Voice Filter Env Amount
    // Arpeggiator destinations (078-modulation-integration)
    kArpFreeRateId,           // 10: Arp Rate
    kArpGateLengthId,         // 11: Arp Gate Length
    kArpOctaveRangeId,        // 12: Arp Octave Range
    kArpSwingId,              // 13: Arp Swing
    kArpSpiceId,              // 14: Arp Spice
}};

static_assert(kVoiceDestParamIds.size() == Krate::Plugins::kVoiceDestNames.size(),
    "kVoiceDestParamIds must match kVoiceDestNames size");
static_assert(kGlobalDestParamIds.size() == Krate::Plugins::kGlobalDestNames.size(),
    "kGlobalDestParamIds must match kGlobalDestNames size");

void Controller::wireModMatrixGrid(Krate::Plugins::ModMatrixGrid* grid) {
    if (!grid) return;

    cacheView(modMatrixGrid_, grid);

    // T048: Set ParameterCallback for direct parameter changes (T039-T041)
    // Suppress sync: the grid is the source of truth during user interaction.
    // The controller's own Parameter is set first: hosts do not echo performEdit
    // back, and syncModMatrixGrid rebuilds the grid from these Parameters (R-083).
    grid->setParameterCallback(
        [this](int32_t paramId, float normalizedValue) {
            const auto id = static_cast<Steinberg::Vst::ParamID>(paramId);
            const auto value = static_cast<double>(normalizedValue);
            suppressModMatrixSync_ = true;
            setParamNormalized(id, value);
            performEdit(id, value);
            suppressModMatrixSync_ = false;
        });

    // T048: Set BeginEditCallback (T042)
    grid->setBeginEditCallback(
        [this](int32_t paramId) {
            const auto id = static_cast<Steinberg::Vst::ParamID>(paramId);
            suppressModMatrixSync_ = true;
            openGridGestureId_ = id;
            beginEdit(id);
            suppressModMatrixSync_ = false;
        });

    // T048: Set EndEditCallback (T042)
    grid->setEndEditCallback(
        [this](int32_t paramId) {
            const auto id = static_cast<Steinberg::Vst::ParamID>(paramId);
            suppressModMatrixSync_ = true;
            endEdit(id);
            if (openGridGestureId_ == id)
                openGridGestureId_ = Steinberg::Vst::kNoParamId;
            suppressModMatrixSync_ = false;
        });

    // T048: Set RouteChangedCallback (T049, T088)
    grid->setRouteChangedCallback(
        [this](int tab, int slot, const Krate::Plugins::ModRoute& route) {
            if (tab == 0) {
                // Global routes use VST params. Only fields that differ from the
                // controller's parameters are sent: an amount drag already sent its
                // amount inside its own gesture, and must not write source/dest (R-083).
                // A new route also resets the slot's stale detail params (R-143).
                // Suppress sync-back: grid is the source of truth here
                suppressModMatrixSync_ = true;
                pushGlobalRouteSlot(slot, route, true);
                suppressModMatrixSync_ = false;
            } else {
                // Voice routes use IMessage (T088)
                // Suppress re-entrant VoiceModRouteState responses: VST3's
                // sendMessage→notify chain is synchronous, so the processor's
                // response would overwrite the grid during this callback.
                suppressVoiceRouteSync_ = true;
                sendVoiceRouteToProcessor(slot, route);
                suppressVoiceRouteSync_ = false;
            }
        });

    // T048: Set RouteRemovedCallback (T088)
    grid->setRouteRemovedCallback(
        [this](int tab, [[maybe_unused]] int slot) {
            if (tab == 0) {
                // Grid has already shifted routes up after removal,
                // so ALL slot parameters must be re-synced from the grid's
                // current state — not just the removed slot.
                suppressModMatrixSync_ = true;
                pushAllGlobalRouteParams();
                suppressModMatrixSync_ = false;
            } else if (modMatrixGrid_) {  // null after terminate with the editor open
                // Voice routes: send full re-sync via IMessage.
                // Suppress re-entrant VoiceModRouteState responses — VST3's
                // synchronous sendMessage→notify would overwrite the grid
                // mid-loop, corrupting the shift-up operation.
                suppressVoiceRouteSync_ = true;
                for (int i = 0; i < Krate::Plugins::kMaxVoiceRoutes; ++i) {
                    sendVoiceRouteToProcessor(i, modMatrixGrid_->getVoiceRoute(i));
                }
                suppressVoiceRouteSync_ = false;
            }
        });

    // Sync initial state from current parameters to the grid
    syncModMatrixGrid();

    // Apply cached voice routes (may have arrived before grid existed)
    for (int i = 0; i < Krate::Plugins::kMaxVoiceRoutes; ++i) {
        modMatrixGrid_->setVoiceRoute(i, cachedVoiceRoutes_[static_cast<size_t>(i)]);
    }
}

void Controller::syncModMatrixGrid() {
    if (!modMatrixGrid_) return;

    // Read current parameter values and build ModRoute for each slot
    for (int i = 0; i < Krate::Plugins::kMaxGlobalRoutes; ++i) {
        Krate::Plugins::ModRoute route;

        // Source: DSP index 0-14 → UI index (dspIdx - 1), clamped to 0-(kModSourceCount-2)
        auto* srcParam = getParameterObject(
            static_cast<Steinberg::Vst::ParamID>(Krate::Plugins::modSlotSourceId(i)));
        int dspSrcIdx = 0;
        if (srcParam) {
            dspSrcIdx = static_cast<int>(
                std::round(srcParam->getNormalized() * (kModSourceCount - 1)));
            int uiSrcIdx = std::clamp(dspSrcIdx - 1, 0, kModSourceCount - 2);
            route.source = static_cast<uint8_t>(uiSrcIdx);
        }

        // Destination: DSP index 0..kNumGlobalDestinations-1 maps directly to global tab dest index
        auto* dstParam = getParameterObject(
            static_cast<Steinberg::Vst::ParamID>(Krate::Plugins::modSlotDestinationId(i)));
        if (dstParam) {
            int dstIdx = static_cast<int>(
                std::round(dstParam->getNormalized() * (kModDestCount - 1)));
            route.destination = static_cast<Krate::Plugins::ModDestination>(
                std::clamp(dstIdx, 0, Krate::Plugins::kNumGlobalDestinations - 1));
        }

        // Amount
        auto* amtParam = getParameterObject(
            static_cast<Steinberg::Vst::ParamID>(Krate::Plugins::modSlotAmountId(i)));
        if (amtParam) {
            route.amount = static_cast<float>(amtParam->getNormalized() * 2.0 - 1.0);
        }

        // Detail params
        auto* curveParam = getParameterObject(
            static_cast<Steinberg::Vst::ParamID>(Krate::Plugins::modSlotCurveId(i)));
        if (curveParam) {
            route.curve = static_cast<uint8_t>(std::clamp(
                static_cast<int>(std::round(curveParam->getNormalized() * 3.0)),
                0, 3));
        }

        auto* smoothParam = getParameterObject(
            static_cast<Steinberg::Vst::ParamID>(Krate::Plugins::modSlotSmoothId(i)));
        if (smoothParam) {
            route.smoothMs = static_cast<float>(smoothParam->getNormalized() * 100.0);
        }

        auto* scaleParam = getParameterObject(
            static_cast<Steinberg::Vst::ParamID>(Krate::Plugins::modSlotScaleId(i)));
        if (scaleParam) {
            route.scale = static_cast<uint8_t>(std::clamp(
                static_cast<int>(std::round(scaleParam->getNormalized() * 4.0)),
                0, 4));
        }

        auto* bypassParam = getParameterObject(
            static_cast<Steinberg::Vst::ParamID>(Krate::Plugins::modSlotBypassId(i)));
        if (bypassParam) {
            route.bypass = bypassParam->getNormalized() >= 0.5;
        }

        // Route is active if DSP source is not None (0) — None means empty slot
        route.active = (dspSrcIdx > 0);

        modMatrixGrid_->setGlobalRoute(i, route);
    }
}

void Controller::pushAllGlobalRouteParams() {
    if (!modMatrixGrid_) return;

    // removeRoute shifts whole routes (all seven fields), so every slot pushes all
    // seven, and emptied slots get the defaults the grid shows (R-082, R-143).
    for (int i = 0; i < Krate::Plugins::kMaxGlobalRoutes; ++i) {
        pushGlobalRouteSlot(i, modMatrixGrid_->getGlobalRoute(i), false);
    }
}

void Controller::pushGlobalRouteSlot(int slot, const Krate::Plugins::ModRoute& route,
                                     bool onlyIfChanged) {
    // An inactive slot is the empty slot: source None, every other field at its default.
    const Krate::Plugins::ModRoute r = route.active ? route : Krate::Plugins::ModRoute{};

    // UI source index i maps to DSP ModSource i + 1 (skip None=0)
    const double srcNorm = (route.active && kModSourceCount > 1)
        ? static_cast<double>(static_cast<int>(r.source) + 1) /
              static_cast<double>(kModSourceCount - 1)
        : 0.0;
    const double dstNorm = (kModDestCount > 1)
        ? static_cast<double>(static_cast<int>(r.destination)) /
              static_cast<double>(kModDestCount - 1)
        : 0.0;
    // Same float arithmetic as the grid's own parameter callbacks, so an unchanged
    // field compares equal.
    const auto amtNorm = static_cast<double>((r.amount + 1.0f) / 2.0f);
    const auto curveNorm = static_cast<double>(
        static_cast<float>(r.curve) /
        static_cast<float>(Krate::Plugins::kCurveTypeNames.size() - 1));
    const auto smoothNorm = static_cast<double>(r.smoothMs / 100.0f);
    const auto scaleNorm = static_cast<double>(
        static_cast<float>(r.scale) /
        static_cast<float>(Krate::Plugins::kScaleNames.size() - 1));
    const double bypassNorm = r.bypass ? 1.0 : 0.0;

    auto id = [](int32_t paramId) { return static_cast<Steinberg::Vst::ParamID>(paramId); };
    pushGlobalRouteParam(id(Krate::Plugins::modSlotSourceId(slot)), srcNorm, onlyIfChanged);
    pushGlobalRouteParam(id(Krate::Plugins::modSlotDestinationId(slot)), dstNorm, onlyIfChanged);
    pushGlobalRouteParam(id(Krate::Plugins::modSlotAmountId(slot)), amtNorm, onlyIfChanged);
    pushGlobalRouteParam(id(Krate::Plugins::modSlotCurveId(slot)), curveNorm, onlyIfChanged);
    pushGlobalRouteParam(id(Krate::Plugins::modSlotSmoothId(slot)), smoothNorm, onlyIfChanged);
    pushGlobalRouteParam(id(Krate::Plugins::modSlotScaleId(slot)), scaleNorm, onlyIfChanged);
    pushGlobalRouteParam(id(Krate::Plugins::modSlotBypassId(slot)), bypassNorm, onlyIfChanged);
}

void Controller::pushGlobalRouteParam(Steinberg::Vst::ParamID id, double value,
                                      bool onlyIfChanged) {
    if (onlyIfChanged && std::abs(getParamNormalized(id) - value) < 1e-9)
        return;

    setParamNormalized(id, value);
    if (id == openGridGestureId_) {
        // The grid holds this parameter's gesture open (a drag): edit inside it.
        performEdit(id, value);
        return;
    }
    beginEdit(id);
    performEdit(id, value);
    endEdit(id);
}

void Controller::sendVoiceRouteToProcessor(int slot, const Krate::Plugins::ModRoute& route) {
    auto msg = Steinberg::owned(allocateMessage());
    if (!msg) return;
    msg->setMessageID("VoiceModRouteUpdate");
    auto* attrs = msg->getAttributes();
    if (!attrs) return;
    attrs->setInt("slotIndex", static_cast<Steinberg::int64>(slot));
    attrs->setInt("source", static_cast<Steinberg::int64>(route.source));
    attrs->setInt("destination", static_cast<Steinberg::int64>(route.destination));
    attrs->setFloat("amount", static_cast<double>(route.amount));
    attrs->setInt("curve", static_cast<Steinberg::int64>(route.curve));
    attrs->setFloat("smoothMs", static_cast<double>(route.smoothMs));
    attrs->setInt("scale", static_cast<Steinberg::int64>(route.scale));
    attrs->setInt("bypass", route.bypass ? 1 : 0);
    attrs->setInt("active", route.active ? 1 : 0);
    sendMessage(msg);
}

void Controller::wireModRingIndicator(Krate::Plugins::ModRingIndicator* indicator) {
    if (!indicator) return;

    int destIdx = indicator->getDestinationIndex();
    if (destIdx < 0 || destIdx >= kMaxRingIndicators) return;

    // Nulled when the indicator is destroyed (CachedViewRegistry), like every cached view.
    cacheView(ringIndicators_[static_cast<size_t>(destIdx)], indicator);

    // Wire controller for cross-component communication
    indicator->setController(this);

    // Wire click-to-select callback (FR-027, T070)
    indicator->setSelectCallback(
        [this](int sourceIndex, int destIndex) {
            selectModulationRoute(sourceIndex, destIndex);
        });

    // Sync initial base value from destination knob parameter
    if (static_cast<size_t>(destIdx) < kVoiceDestParamIds.size()) {
        auto destParamId = kVoiceDestParamIds[static_cast<size_t>(destIdx)];
        auto* param = getParameterObject(destParamId);
        if (param) {
            indicator->setBaseValue(static_cast<float>(param->getNormalized()));
        }
    }

    // Sync initial arc state from current parameters
    rebuildRingIndicators();
}

void Controller::selectModulationRoute(int sourceIndex, int destIndex) {
    // Mediate selection to ModMatrixGrid (FR-027, T070)
    if (modMatrixGrid_) {
        modMatrixGrid_->selectRoute(sourceIndex, destIndex);
    }
}

void Controller::rebuildRingIndicators() {
    // Read all global routes and build ArcInfo lists per destination (T071)
    // First, collect all active routes grouped by destination
    using ArcInfo = Krate::Plugins::ModRingIndicator::ArcInfo;

    // Build route data from current parameters
    struct RouteData {
        int sourceIndex = 0;
        int destIndex = 0;
        float amount = 0.0f;
        bool bypass = false;
        bool active = false;
    };

    std::array<RouteData, Krate::Plugins::kMaxGlobalRoutes> routes{};

    for (int i = 0; i < Krate::Plugins::kMaxGlobalRoutes; ++i) {
        auto* srcParam = getParameterObject(
            static_cast<Steinberg::Vst::ParamID>(Krate::Plugins::modSlotSourceId(i)));
        auto* dstParam = getParameterObject(
            static_cast<Steinberg::Vst::ParamID>(Krate::Plugins::modSlotDestinationId(i)));
        auto* amtParam = getParameterObject(
            static_cast<Steinberg::Vst::ParamID>(Krate::Plugins::modSlotAmountId(i)));
        auto* bypassParam = getParameterObject(
            static_cast<Steinberg::Vst::ParamID>(Krate::Plugins::modSlotBypassId(i)));

        int dspSrcIdx = 0;
        if (srcParam) {
            dspSrcIdx = static_cast<int>(
                std::round(srcParam->getNormalized() * (kModSourceCount - 1)));
            // DSP index → UI index (subtract 1, clamp to valid source range)
            routes[static_cast<size_t>(i)].sourceIndex =
                std::clamp(dspSrcIdx - 1, 0, kModSourceCount - 2);
        }
        if (dstParam) {
            routes[static_cast<size_t>(i)].destIndex = static_cast<int>(
                std::round(dstParam->getNormalized() * (kModDestCount - 1)));
        }
        if (amtParam) {
            routes[static_cast<size_t>(i)].amount =
                static_cast<float>(amtParam->getNormalized() * 2.0 - 1.0);
        }
        if (bypassParam) {
            routes[static_cast<size_t>(i)].bypass = bypassParam->getNormalized() >= 0.5;
        }

        // Route is active if DSP source is not None (0)
        routes[static_cast<size_t>(i)].active = (dspSrcIdx > 0);
    }

    // For each destination with a ring indicator, build the arc list.
    // Ring indicators use voice dest indices (0..kNumVoiceDestinations-1) and sit on voice knobs.
    // Match global routes to ring indicators via parameter ID so that
    // e.g. global dest 4 (All Voice Filter Cutoff) shows on ring indicator 0
    // (which sits on the per-voice filter cutoff knob).
    for (int destIdx = 0; destIdx < kMaxRingIndicators; ++destIdx) {
        auto* indicator = ringIndicators_[static_cast<size_t>(destIdx)];
        if (!indicator) continue;

        auto indicatorParamId = kVoiceDestParamIds[static_cast<size_t>(destIdx)];

        std::vector<ArcInfo> arcs;
        for (int i = 0; i < Krate::Plugins::kMaxGlobalRoutes; ++i) {
            const auto& r = routes[static_cast<size_t>(i)];
            if (!r.active) continue;
            if (r.destIndex < 0 ||
                static_cast<size_t>(r.destIndex) >= kGlobalDestParamIds.size())
                continue;
            if (kGlobalDestParamIds[static_cast<size_t>(r.destIndex)] != indicatorParamId)
                continue;

            ArcInfo arc;
            arc.amount = r.amount;
            arc.color = Krate::Plugins::sourceColorForTab(0, r.sourceIndex);
            arc.sourceIndex = r.sourceIndex;
            arc.destIndex = r.destIndex;
            arc.bypassed = r.bypass;
            arcs.push_back(arc);
        }

        indicator->setArcs(arcs);
    }
}

} // namespace Ruinae
