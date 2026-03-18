// ==============================================================================
// Processor IMessage Handling (notify, sendSkipEvent, sendVoiceModRouteState)
// ==============================================================================

#include "processor.h"
#include "plugin_ids.h"

#include <krate/dsp/systems/voice_mod_types.h>

#include <algorithm>
#include <cstring>

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

        if (attrs->getInt("source", val) == Steinberg::kResultOk)
            route.source = static_cast<uint8_t>(std::clamp(val, Steinberg::int64{0}, Steinberg::int64{9}));

        if (attrs->getInt("destination", val) == Steinberg::kResultOk)
            route.destination = static_cast<uint8_t>(std::clamp(val, Steinberg::int64{0}, Steinberg::int64{kModDestCount - 1}));

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
// Arp Skip Event Sender (081-interaction-polish, FR-007, FR-008, FR-012)
// ==============================================================================

void Processor::sendSkipEvent(int lane, int step) {
    // FR-012: don't send when editor is closed
    if (!editorOpen_.load(std::memory_order_relaxed))
        return;

    if (lane < 0 || lane >= 6) return;
    if (step < 0 || step >= 32) return;

    auto* msg = skipMessages_[static_cast<size_t>(lane)].get();
    if (!msg) return;

    auto* attrs = msg->getAttributes();
    if (!attrs) return;

    attrs->setInt("lane", static_cast<Steinberg::int64>(lane));
    attrs->setInt("step", static_cast<Steinberg::int64>(step));
    sendMessage(msg);
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
