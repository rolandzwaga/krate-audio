// ==============================================================================
// Processor MIDI Event Handling (processEvents, onNoteOn, onNoteOff)
// ==============================================================================

#include "processor.h"
#include "plugin_ids.h"

#include "midi/midi_event_dispatcher.h"

namespace Ruinae {

// ==============================================================================
// MIDI Event Handling
// ==============================================================================

void Processor::processEvents(Steinberg::Vst::IEventList* events) {
    Krate::Plugins::dispatchMidiEvents(events, *this);
}

// ==============================================================================
// MIDI Dispatcher Callbacks (FR-006)
// ==============================================================================
// NOLINTNEXTLINE(readability-convert-member-functions-to-static) accesses members arpParams_, arpCore_, engine_
void Processor::onNoteOn(int16_t pitch, float velocity) {
    auto midiPitch = static_cast<uint8_t>(pitch);
    auto midiVelocity = static_cast<uint8_t>(velocity * 127.0f + 0.5f);

    const int opMode = arpParams_.operatingMode.load(std::memory_order_relaxed);

    // FR-006: route note-on based on arp operating mode
    const bool arpRunning = (opMode != kArpOff);
    const bool arpDispatchesNotes = (opMode == kArpMIDI || opMode == kArpMIDIMod);

    if (arpRunning) {
        // Feed note to arp core for pattern building
        arpCore_.noteOn(midiPitch, midiVelocity);
    }
    if (!arpDispatchesNotes) {
        // Direct to engine: Off mode or Mod-only mode (voices play held notes)
        engine_.noteOn(midiPitch, midiVelocity);
    }
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static) accesses members arpParams_, arpCore_, engine_
void Processor::onNoteOff(int16_t pitch) {
    auto midiPitch = static_cast<uint8_t>(pitch);

    const int opMode = arpParams_.operatingMode.load(std::memory_order_relaxed);

    const bool arpRunning = (opMode != kArpOff);
    const bool arpDispatchesNotes = (opMode == kArpMIDI || opMode == kArpMIDIMod);

    if (arpRunning) {
        arpCore_.noteOff(midiPitch);
    }
    if (!arpDispatchesNotes) {
        engine_.noteOff(midiPitch);
    }
}

} // namespace Ruinae
