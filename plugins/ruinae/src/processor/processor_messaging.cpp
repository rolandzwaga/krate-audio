// ==============================================================================
// Processor IMessage Handling (notify, sendVoiceModRouteState)
// ==============================================================================

#include "processor.h"
#include "plugin_ids.h"

#include <krate/dsp/systems/voice_mod_types.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace Ruinae {

// ==============================================================================
// IMessage: Receive Controller Messages (T085)
// ==============================================================================

Steinberg::tresult PLUGIN_API Processor::notify(Steinberg::Vst::IMessage* message) {
    if (!message)
        return Steinberg::kInvalidArgument;

    if (strcmp(message->getMessageID(), "VoiceModRouteUpdate") == 0) {
        auto* attrs = message->getAttributes();
        if (!attrs)
            return Steinberg::kResultFalse;

        Steinberg::int64 slotIndex = 0;
        if (attrs->getInt("slotIndex", slotIndex) != Steinberg::kResultOk)
            return Steinberg::kResultFalse;

        if (slotIndex < 0 || slotIndex >= Krate::Plugins::kMaxVoiceRoutes)
            return Steinberg::kResultFalse;

        // Build local route, then atomic-store into the slot
        auto route = voiceRoutes_[static_cast<size_t>(slotIndex)].load();

        Steinberg::int64 val = 0;
        double dval = 0.0;

        // Bounds of the voice enums, not the global matrix's (R-109). The
        // destination is the voice tab's UI index; applyModRoutingParams maps it.
        if (attrs->getInt("source", val) == Steinberg::kResultOk)
            route.source = static_cast<uint8_t>(std::clamp(
                val, Steinberg::int64{0},
                Steinberg::int64{static_cast<int>(Krate::DSP::VoiceModSource::NumSources) - 1}));

        if (attrs->getInt("destination", val) == Steinberg::kResultOk)
            route.destination = static_cast<uint8_t>(std::clamp(
                val, Steinberg::int64{0},
                Steinberg::int64{Krate::Plugins::kNumVoiceDestinations - 1}));

        if (attrs->getFloat("amount", dval) == Steinberg::kResultOk)
            route.amount = static_cast<float>(std::clamp(dval, -1.0, 1.0));

        if (attrs->getInt("curve", val) == Steinberg::kResultOk)
            route.curve = static_cast<uint8_t>(std::clamp(val, Steinberg::int64{0}, Steinberg::int64{3}));

        if (attrs->getFloat("smoothMs", dval) == Steinberg::kResultOk)
            route.smoothMs = static_cast<float>(std::clamp(dval, 0.0, 100.0));

        if (attrs->getInt("scale", val) == Steinberg::kResultOk)
            route.scale = static_cast<uint8_t>(std::clamp(val, Steinberg::int64{0}, Steinberg::int64{4}));

        if (attrs->getInt("bypass", val) == Steinberg::kResultOk)
            route.bypass = static_cast<uint8_t>(val != 0 ? 1 : 0);

        if (attrs->getInt("active", val) == Steinberg::kResultOk)
            route.active = static_cast<uint8_t>(val != 0 ? 1 : 0);

        voiceRoutes_[static_cast<size_t>(slotIndex)].store(route);

        // Send authoritative state back to controller (T086)
        sendVoiceModRouteState();

        return Steinberg::kResultOk;
    }

    if (strcmp(message->getMessageID(), "VoiceModRouteRemove") == 0) {
        auto* attrs = message->getAttributes();
        if (!attrs)
            return Steinberg::kResultFalse;

        Steinberg::int64 slotIndex = 0;
        if (attrs->getInt("slotIndex", slotIndex) != Steinberg::kResultOk)
            return Steinberg::kResultFalse;

        if (slotIndex < 0 || slotIndex >= Krate::Plugins::kMaxVoiceRoutes)
            return Steinberg::kResultFalse;

        // Deactivate the slot (atomic store of default-constructed route)
        voiceRoutes_[static_cast<size_t>(slotIndex)].store(Krate::Plugins::VoiceModRoute{});

        // Send authoritative state back to controller (T086)
        sendVoiceModRouteState();

        return Steinberg::kResultOk;
    }

    // LoadPresetState: a preset-browser load (R-196). The controller hands over
    // the whole component state; it takes the same path as setState.
    if (strcmp(message->getMessageID(), "LoadPresetState") == 0) {
        auto* attrs = message->getAttributes();
        if (!attrs)
            return Steinberg::kResultFalse;

        const void* data = nullptr;
        Steinberg::uint32 size = 0;
        if (attrs->getBinary("bytes", data, size) != Steinberg::kResultOk || !data)
            return Steinberg::kResultFalse;
        Steinberg::int64 arpOnly = 0;
        attrs->getInt("arpOnly", arpOnly);

        const auto* first = static_cast<const char*>(data);
        return loadStateBytes(std::vector<char>(first, first + size), arpOnly != 0);
    }

    // EditorState message: controller tells processor whether editor is open (Phase 11c)
    if (strcmp(message->getMessageID(), "EditorState") == 0) {
        auto* attrs = message->getAttributes();
        if (attrs) {
            Steinberg::int64 open = 0;
            if (attrs->getInt("open", open) == Steinberg::kResultOk) {
                editorOpen_.store(open != 0, std::memory_order_relaxed);
            }
        }
        return Steinberg::kResultOk;
    }

    return AudioEffect::notify(message);
}

// ==============================================================================
// Voice Route State Sender (T086)
// ==============================================================================

void Processor::sendVoiceModRouteState() {
    if (!voiceRouteSyncMsg_) return;

    auto* attrs = voiceRouteSyncMsg_->getAttributes();
    if (!attrs) return;

    // Count active routes (atomic load per slot)
    Steinberg::int64 activeCount = 0;
    for (const auto& ar : voiceRoutes_) {
        if (ar.active.load(std::memory_order_relaxed) != 0) ++activeCount;
    }
    attrs->setInt("routeCount", activeCount);

    // Pack route data as binary blob (14 bytes per route, 16 routes = 224 bytes)
    // Per contract: source(1), dest(1), amount(4), curve(1), smoothMs(4), scale(1),
    //              bypass(1), active(1) = 14 bytes
    static constexpr size_t kBytesPerRoute = 14;
    static constexpr size_t kTotalBytes = kBytesPerRoute * Krate::Plugins::kMaxVoiceRoutes;
    uint8_t buffer[kTotalBytes]{};

    for (int i = 0; i < Krate::Plugins::kMaxVoiceRoutes; ++i) {
        auto r = voiceRoutes_[static_cast<size_t>(i)].load();
        auto* ptr = &buffer[static_cast<size_t>(i) * kBytesPerRoute];

        ptr[0] = r.source;
        ptr[1] = r.destination;
        std::memcpy(&ptr[2], &r.amount, sizeof(float));
        ptr[6] = r.curve;
        std::memcpy(&ptr[7], &r.smoothMs, sizeof(float));
        ptr[11] = r.scale;
        ptr[12] = r.bypass;
        ptr[13] = r.active;
    }

    attrs->setBinary("routeData", buffer, kTotalBytes);
    sendMessage(voiceRouteSyncMsg_);
}

} // namespace Ruinae
