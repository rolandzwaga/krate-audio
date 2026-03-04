// ==============================================================================
// MIDI Event Dispatcher - Unit Tests
// ==============================================================================
// Tests for: plugins/shared/src/midi/midi_event_dispatcher.h
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "midi/midi_event_dispatcher.h"

#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"

#include <cstdint>
#include <vector>

using Catch::Approx;

// ==============================================================================
// Test Event List (minimal IEventList mock)
// ==============================================================================

class TestEventList : public Steinberg::Vst::IEventList {
public:
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void**) override {
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }

    Steinberg::int32 PLUGIN_API getEventCount() override {
        return static_cast<Steinberg::int32>(events_.size());
    }

    Steinberg::tresult PLUGIN_API getEvent(Steinberg::int32 index,
                                            Steinberg::Vst::Event& e) override {
        if (index < 0 || index >= static_cast<Steinberg::int32>(events_.size()))
            return Steinberg::kResultFalse;
        e = events_[static_cast<size_t>(index)];
        return Steinberg::kResultTrue;
    }

    Steinberg::tresult PLUGIN_API addEvent(Steinberg::Vst::Event& e) override {
        events_.push_back(e);
        return Steinberg::kResultTrue;
    }

    void addNoteOn(int16_t pitch, float velocity) {
        Steinberg::Vst::Event e{};
        e.type = Steinberg::Vst::Event::kNoteOnEvent;
        e.noteOn.channel = 0;
        e.noteOn.pitch = pitch;
        e.noteOn.velocity = velocity;
        e.noteOn.noteId = -1;
        e.noteOn.length = 0;
        e.noteOn.tuning = 0.0f;
        events_.push_back(e);
    }

    void addNoteOff(int16_t pitch) {
        Steinberg::Vst::Event e{};
        e.type = Steinberg::Vst::Event::kNoteOffEvent;
        e.noteOff.channel = 0;
        e.noteOff.pitch = pitch;
        e.noteOff.velocity = 0.0f;
        e.noteOff.noteId = -1;
        e.noteOff.tuning = 0.0f;
        events_.push_back(e);
    }

    void addPitchBend(int msb, int lsb) {
        Steinberg::Vst::Event e{};
        e.type = Steinberg::Vst::Event::kLegacyMIDICCOutEvent;
        e.midiCCOut.controlNumber =
            static_cast<uint8_t>(Steinberg::Vst::ControllerNumbers::kPitchBend);
        e.midiCCOut.channel = 0;
        e.midiCCOut.value = static_cast<Steinberg::int8>(msb);
        e.midiCCOut.value2 = static_cast<Steinberg::int8>(lsb);
        events_.push_back(e);
    }

private:
    std::vector<Steinberg::Vst::Event> events_;
};

// ==============================================================================
// Test Handlers
// ==============================================================================

/// Handler with all three callbacks (including pitch bend)
struct FullHandler {
    int noteOnCount = 0;
    int noteOffCount = 0;
    int pitchBendCount = 0;

    int16_t lastNoteOnPitch = -1;
    float lastNoteOnVelocity = -1.0f;
    int16_t lastNoteOffPitch = -1;
    float lastPitchBend = -99.0f;

    void onNoteOn(int16_t pitch, float velocity) {
        noteOnCount++;
        lastNoteOnPitch = pitch;
        lastNoteOnVelocity = velocity;
    }

    void onNoteOff(int16_t pitch) {
        noteOffCount++;
        lastNoteOffPitch = pitch;
    }

    void onPitchBend(float bipolar) {
        pitchBendCount++;
        lastPitchBend = bipolar;
    }
};

/// Handler without pitch bend (onPitchBend should not be called)
struct NoPitchBendHandler {
    int noteOnCount = 0;
    int noteOffCount = 0;
    int16_t lastNoteOnPitch = -1;
    float lastNoteOnVelocity = -1.0f;
    int16_t lastNoteOffPitch = -1;

    void onNoteOn(int16_t pitch, float velocity) {
        noteOnCount++;
        lastNoteOnPitch = pitch;
        lastNoteOnVelocity = velocity;
    }

    void onNoteOff(int16_t pitch) {
        noteOffCount++;
        lastNoteOffPitch = pitch;
    }
};

// Compile-time concept check
static_assert(Krate::Plugins::HasPitchBend<FullHandler>);
static_assert(!Krate::Plugins::HasPitchBend<NoPitchBendHandler>);

// ==============================================================================
// Tests
// ==============================================================================

TEST_CASE("dispatchMidiEvents dispatches VST3 MIDI events to handler",
          "[shared][midi][dispatcher]") {

    SECTION("Null events pointer does not call any handler methods") {
        FullHandler handler;
        Krate::Plugins::dispatchMidiEvents(nullptr, handler);
        REQUIRE(handler.noteOnCount == 0);
        REQUIRE(handler.noteOffCount == 0);
        REQUIRE(handler.pitchBendCount == 0);
    }

    SECTION("NoteOn event dispatches onNoteOn with correct pitch and velocity") {
        TestEventList events;
        events.addNoteOn(60, 0.8f);

        FullHandler handler;
        Krate::Plugins::dispatchMidiEvents(&events, handler);

        REQUIRE(handler.noteOnCount == 1);
        REQUIRE(handler.lastNoteOnPitch == 60);
        REQUIRE(handler.lastNoteOnVelocity == Approx(0.8f));
        REQUIRE(handler.noteOffCount == 0);
    }

    SECTION("Velocity-0 noteOn dispatches onNoteOff instead") {
        TestEventList events;
        events.addNoteOn(64, 0.0f);

        FullHandler handler;
        Krate::Plugins::dispatchMidiEvents(&events, handler);

        REQUIRE(handler.noteOnCount == 0);
        REQUIRE(handler.noteOffCount == 1);
        REQUIRE(handler.lastNoteOffPitch == 64);
    }

    SECTION("NoteOff event dispatches onNoteOff with correct pitch") {
        TestEventList events;
        events.addNoteOff(72);

        FullHandler handler;
        Krate::Plugins::dispatchMidiEvents(&events, handler);

        REQUIRE(handler.noteOffCount == 1);
        REQUIRE(handler.lastNoteOffPitch == 72);
        REQUIRE(handler.noteOnCount == 0);
    }

    SECTION("Pitch bend event dispatches onPitchBend when handler supports it") {
        TestEventList events;
        events.addPitchBend(64, 0); // center

        FullHandler handler;
        Krate::Plugins::dispatchMidiEvents(&events, handler);

        REQUIRE(handler.pitchBendCount == 1);
        REQUIRE(handler.lastPitchBend == Approx(0.0f).margin(0.001f));
    }

    SECTION("Pitch bend max up") {
        TestEventList events;
        events.addPitchBend(127, 127);

        FullHandler handler;
        Krate::Plugins::dispatchMidiEvents(&events, handler);

        REQUIRE(handler.pitchBendCount == 1);
        REQUIRE(handler.lastPitchBend == Approx(1.0f).margin(0.001f));
    }

    SECTION("Pitch bend event NOT dispatched when handler lacks onPitchBend") {
        TestEventList events;
        events.addPitchBend(127, 127);

        NoPitchBendHandler handler;
        Krate::Plugins::dispatchMidiEvents(&events, handler);

        // No pitch bend count field -- just verify note counts are 0
        REQUIRE(handler.noteOnCount == 0);
        REQUIRE(handler.noteOffCount == 0);
    }

    SECTION("Multiple events dispatched in order") {
        TestEventList events;
        events.addNoteOn(60, 0.5f);
        events.addNoteOn(64, 0.7f);
        events.addNoteOff(60);

        FullHandler handler;
        Krate::Plugins::dispatchMidiEvents(&events, handler);

        REQUIRE(handler.noteOnCount == 2);
        REQUIRE(handler.noteOffCount == 1);
        REQUIRE(handler.lastNoteOnPitch == 64);
        REQUIRE(handler.lastNoteOffPitch == 60);
    }

    SECTION("Empty event list does not call any handler methods") {
        TestEventList events;

        FullHandler handler;
        Krate::Plugins::dispatchMidiEvents(&events, handler);

        REQUIRE(handler.noteOnCount == 0);
        REQUIRE(handler.noteOffCount == 0);
        REQUIRE(handler.pitchBendCount == 0);
    }
}
