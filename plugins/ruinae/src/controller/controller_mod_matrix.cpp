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

namespace Ruinae {

// Maps destination index to the actual VST parameter ID of that knob.
// Duplicated from controller.cpp — both TUs need these for mod ring sync.
static constexpr std::array<Steinberg::Vst::ParamID,
    Krate::Plugins::kNumVoiceDestinations> kVoiceDestParamIds = {{
    kFilterCutoffId,          // 0: Filter Cutoff
    kFilterResonanceId,       // 1: Filter Resonance
    kMixerPositionId,         // 2: Morph Position
    kDistortionDriveId,       // 3: Distortion Drive
    kTranceGateDepthId,       // 4: TranceGate Depth
    kOscATuneId,              // 5: OSC A Pitch
    kOscBTuneId,              // 6: OSC B Pitch
    kMixerTiltId,             // 7: Spectral Tilt
}};

static constexpr std::array<Steinberg::Vst::ParamID,
    Krate::Plugins::kNumGlobalDestinations> kGlobalDestParamIds = {{
    kGlobalFilterCutoffId,    // 0: Global Filter Cutoff
    kGlobalFilterResonanceId, // 1: Global Filter Resonance
    kMasterGainId,            // 2: Master Volume
    kDelayMixId,              // 3: Effect Mix
    kFilterCutoffId,          // 4: All Voice Filter Cutoff
    kMixerPositionId,         // 5: All Voice Morph Position
    kTranceGateDepthId,       // 6: All Voice TranceGate Rate
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

    modMatrixGrid_ = grid;

    // T048: Set ParameterCallback for direct parameter changes (T039-T041)
    // Suppress sync: the grid is the source of truth during user interaction
    grid->setParameterCallback(
        [this](int32_t paramId, float normalizedValue) {
            suppressModMatrixSync_ = true;
            performEdit(static_cast<Steinberg::Vst::ParamID>(paramId),
                        static_cast<double>(normalizedValue));
            suppressModMatrixSync_ = false;
        });

    // T048: Set BeginEditCallback (T042)
    grid->setBeginEditCallback(
        [this](int32_t paramId) {
            suppressModMatrixSync_ = true;
            beginEdit(static_cast<Steinberg::Vst::ParamID>(paramId));
            suppressModMatrixSync_ = false;
        });

    // T048: Set EndEditCallback (T042)
    grid->setEndEditCallback(
        [this](int32_t paramId) {
            suppressModMatrixSync_ = true;
            endEdit(static_cast<Steinberg::Vst::ParamID>(paramId));
            suppressModMatrixSync_ = false;
        });

    // T048: Set RouteChangedCallback (T049, T088)
    grid->setRouteChangedCallback(
        [this](int tab, int slot, const Krate::Plugins::ModRoute& route) {
            if (tab == 0) {
                // Global routes use VST params
                auto sourceId = static_cast<Steinberg::Vst::ParamID>(
                    Krate::Plugins::modSlotSourceId(slot));
                auto destId = static_cast<Steinberg::Vst::ParamID>(
                    Krate::Plugins::modSlotDestinationId(slot));
                auto amountId = static_cast<Steinberg::Vst::ParamID>(
                    Krate::Plugins::modSlotAmountId(slot));

                // UI source index 0-11 maps to DSP ModSource 1-12 (skip None=0)
                int dspSrcIdx = static_cast<int>(route.source) + 1;
                int dstIdx = static_cast<int>(route.destination);

                // Suppress sync-back: grid is the source of truth here
                suppressModMatrixSync_ = true;

                double srcNorm = (kModSourceCount > 1)
                    ? static_cast<double>(dspSrcIdx) / static_cast<double>(kModSourceCount - 1)
                    : 0.0;
                setParamNormalized(sourceId, srcNorm);
                beginEdit(sourceId);
                performEdit(sourceId, srcNorm);
                endEdit(sourceId);

                double dstNorm = (kModDestCount > 1)
                    ? static_cast<double>(dstIdx) / static_cast<double>(kModDestCount - 1)
                    : 0.0;
                setParamNormalized(destId, dstNorm);
                beginEdit(destId);
                performEdit(destId, dstNorm);
                endEdit(destId);

                double amtNorm = static_cast<double>((route.amount + 1.0f) / 2.0f);
                setParamNormalized(amountId, amtNorm);
                beginEdit(amountId);
                performEdit(amountId, amtNorm);
                endEdit(amountId);

                suppressModMatrixSync_ = false;
            } else {
                // Voice routes use IMessage (T088)
                auto msg = Steinberg::owned(allocateMessage());
                if (msg) {
                    msg->setMessageID("VoiceModRouteUpdate");
                    auto* attrs = msg->getAttributes();
                    if (attrs) {
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
                }
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
            } else {
                // Voice routes: send full re-sync via IMessage
                for (int i = 0; i < Krate::Plugins::kMaxVoiceRoutes; ++i) {
                    auto route = modMatrixGrid_->getVoiceRoute(i);
                    auto msg = Steinberg::owned(allocateMessage());
                    if (msg) {
                        msg->setMessageID("VoiceModRouteUpdate");
                        auto* attrs = msg->getAttributes();
                        if (attrs) {
                            attrs->setInt("slotIndex", static_cast<Steinberg::int64>(i));
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
                    }
                }
            }
        });

    // Sync initial state from current parameters to the grid
    syncModMatrixGrid();
}

void Controller::syncModMatrixGrid() {
    if (!modMatrixGrid_) return;

    // Read current parameter values and build ModRoute for each slot
    for (int i = 0; i < Krate::Plugins::kMaxGlobalRoutes; ++i) {
        Krate::Plugins::ModRoute route;

        // Source: DSP index 0-12 → UI index (dspIdx - 1), clamped to 0-11
        auto* srcParam = getParameterObject(
            static_cast<Steinberg::Vst::ParamID>(Krate::Plugins::modSlotSourceId(i)));
        int dspSrcIdx = 0;
        if (srcParam) {
            dspSrcIdx = static_cast<int>(
                std::round(srcParam->getNormalized() * (kModSourceCount - 1)));
            int uiSrcIdx = std::clamp(dspSrcIdx - 1, 0, Krate::Plugins::kNumGlobalSources - 1);
            route.source = static_cast<uint8_t>(uiSrcIdx);
        }

        // Destination: DSP index 0-6 maps directly to global tab dest index
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

    for (int i = 0; i < Krate::Plugins::kMaxGlobalRoutes; ++i) {
        auto route = modMatrixGrid_->getGlobalRoute(i);

        auto sourceId = static_cast<Steinberg::Vst::ParamID>(
            Krate::Plugins::modSlotSourceId(i));
        auto destId = static_cast<Steinberg::Vst::ParamID>(
            Krate::Plugins::modSlotDestinationId(i));
        auto amountId = static_cast<Steinberg::Vst::ParamID>(
            Krate::Plugins::modSlotAmountId(i));

        if (route.active) {
            // UI source index 0-11 maps to DSP ModSource 1-12 (skip None=0)
            int dspSrcIdx = static_cast<int>(route.source) + 1;
            int dstIdx = static_cast<int>(route.destination);

            double srcNorm = (kModSourceCount > 1)
                ? static_cast<double>(dspSrcIdx) / static_cast<double>(kModSourceCount - 1)
                : 0.0;
            setParamNormalized(sourceId, srcNorm);
            beginEdit(sourceId);
            performEdit(sourceId, srcNorm);
            endEdit(sourceId);

            double dstNorm = (kModDestCount > 1)
                ? static_cast<double>(dstIdx) / static_cast<double>(kModDestCount - 1)
                : 0.0;
            setParamNormalized(destId, dstNorm);
            beginEdit(destId);
            performEdit(destId, dstNorm);
            endEdit(destId);

            double amtNorm = static_cast<double>((route.amount + 1.0f) / 2.0f);
            setParamNormalized(amountId, amtNorm);
            beginEdit(amountId);
            performEdit(amountId, amtNorm);
            endEdit(amountId);
        } else {
            // Inactive slot: reset to defaults (source=None)
            beginEdit(sourceId);
            performEdit(sourceId, 0.0);
            endEdit(sourceId);

            beginEdit(destId);
            performEdit(destId, 0.0);
            endEdit(destId);

            beginEdit(amountId);
            performEdit(amountId, 0.5); // 0.5 normalized = 0.0 bipolar
            endEdit(amountId);
        }
    }
}

void Controller::wireModRingIndicator(Krate::Plugins::ModRingIndicator* indicator) {
    if (!indicator) return;

    int destIdx = indicator->getDestinationIndex();
    if (destIdx < 0 || destIdx >= kMaxRingIndicators) return;

    ringIndicators_[static_cast<size_t>(destIdx)] = indicator;

    // Wire controller for cross-component communication
    indicator->setController(this);

    // Wire removed callback so UIViewSwitchContainer template teardown
    // nulls the cached pointer (prevents dangling pointer crashes)
    indicator->setRemovedCallback(
        [this, destIdx]() {
            ringIndicators_[static_cast<size_t>(destIdx)] = nullptr;
        });

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
            // DSP index → UI index (subtract 1, clamp to 0-11)
            routes[static_cast<size_t>(i)].sourceIndex =
                std::clamp(dspSrcIdx - 1, 0, Krate::Plugins::kNumGlobalSources - 1);
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
    // Ring indicators use voice dest indices (0-6) and sit on voice knobs.
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
