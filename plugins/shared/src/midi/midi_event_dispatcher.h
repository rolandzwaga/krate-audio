// ==============================================================================
// MIDI Event Dispatcher
// ==============================================================================
// Template-based VST3 MIDI event dispatcher that eliminates boilerplate
// processEvents() code across plugins.
//
// Constitution Principle II: Real-Time Audio Thread Safety
// - No allocation, no locks, no exceptions, no I/O
//
// Constitution Principle III: Modern C++ Standards
// - C++20 concepts for compile-time handler detection
//
// Usage:
//   void MyProcessor::processEvents(IEventList* events) {
//       Krate::Plugins::dispatchMidiEvents(events, *this);
//   }
//
// Handler must provide:
//   void onNoteOn(int16_t pitch, float velocity);
//   void onNoteOff(int16_t pitch);
//
// Handler may optionally provide:
//   void onPitchBend(float bipolar);  // detected at compile time
// ==============================================================================

#pragma once

#include <krate/dsp/core/midi_utils.h>

#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"

#include <concepts>
#include <cstdint>

namespace Krate::Plugins {

// ==============================================================================
// Handler Concepts
// ==============================================================================

/// Concept: handler supports pitch bend callback
template<typename H>
concept HasPitchBend = requires(H& h, float f) {
    { h.onPitchBend(f) };
};

/// Concept: handler supports noteId-aware note-on callback (MPE/polyphonic)
template<typename H>
concept HasNoteIdCallbacks = requires(H& h, int16_t pitch, float vel, int32_t noteId) {
    { h.onNoteOn(pitch, vel, noteId) };
};

/// Concept: handler supports noteId-aware note-off callback (MPE/polyphonic)
template<typename H>
concept HasNoteIdNoteOff = requires(H& h, int16_t pitch, int32_t noteId) {
    { h.onNoteOff(pitch, noteId) };
};

/// Concept: handler supports NoteExpression events (MPE per-note expression)
template<typename H>
concept HasNoteExpression = requires(H& h, int32_t noteId, uint32_t typeId, double value) {
    { h.onNoteExpression(noteId, typeId, value) };
};

// ==============================================================================
// dispatchMidiEvents
// ==============================================================================

/// Iterate a VST3 IEventList and dispatch note/pitch-bend events to a handler.
///
/// Handles the common boilerplate:
/// - Null-checks the event list
/// - Iterates with getEventCount()/getEvent()
/// - Velocity-0 noteOn → onNoteOff (MIDI convention)
/// - kLegacyMIDICCOutEvent with kPitchBend → 14-bit decode → onPitchBend
///   (only if handler has onPitchBend, detected at compile time)
///
/// @tparam Handler Must have onNoteOn(int16_t, float) and onNoteOff(int16_t).
///         May optionally have onPitchBend(float).
/// @param events VST3 event list (may be null)
/// @param handler The handler to dispatch events to
///
template<typename Handler>
void dispatchMidiEvents(Steinberg::Vst::IEventList* events, Handler& handler)
{
    if (!events)
        return;

    const Steinberg::int32 numEvents = events->getEventCount();

    for (Steinberg::int32 i = 0; i < numEvents; ++i)
    {
        Steinberg::Vst::Event event{};
        if (events->getEvent(i, event) != Steinberg::kResultTrue)
            continue;

        switch (event.type)
        {
        case Steinberg::Vst::Event::kNoteOnEvent:
        {
            // Velocity-0 noteOn is treated as noteOff per MIDI convention
            if (event.noteOn.velocity <= 0.0f)
            {
                if constexpr (HasNoteIdNoteOff<Handler>)
                    handler.onNoteOff(event.noteOn.pitch, event.noteOn.noteId);
                else
                    handler.onNoteOff(event.noteOn.pitch);
            }
            else
            {
                if constexpr (HasNoteIdCallbacks<Handler>)
                    handler.onNoteOn(event.noteOn.pitch, event.noteOn.velocity,
                                     event.noteOn.noteId);
                else
                    handler.onNoteOn(event.noteOn.pitch, event.noteOn.velocity);
            }
            break;
        }

        case Steinberg::Vst::Event::kNoteOffEvent:
        {
            if constexpr (HasNoteIdNoteOff<Handler>)
                handler.onNoteOff(event.noteOff.pitch, event.noteOff.noteId);
            else
                handler.onNoteOff(event.noteOff.pitch);
            break;
        }

        case Steinberg::Vst::Event::kNoteExpressionValueEvent:
        {
            if constexpr (HasNoteExpression<Handler>)
            {
                handler.onNoteExpression(
                    event.noteExpressionValue.noteId,
                    event.noteExpressionValue.typeId,
                    event.noteExpressionValue.value);
            }
            break;
        }

        case Steinberg::Vst::Event::kLegacyMIDICCOutEvent:
        {
            if constexpr (HasPitchBend<Handler>)
            {
                if (event.midiCCOut.controlNumber ==
                    Steinberg::Vst::ControllerNumbers::kPitchBend)
                {
                    int msb = static_cast<int>(event.midiCCOut.value) & 0x7F;
                    int lsb = static_cast<int>(event.midiCCOut.value2) & 0x7F;
                    float bipolar = Krate::DSP::decodePitchBend(msb, lsb);
                    handler.onPitchBend(bipolar);
                }
            }
            break;
        }

        default:
            break;
        }
    }
}

} // namespace Krate::Plugins
