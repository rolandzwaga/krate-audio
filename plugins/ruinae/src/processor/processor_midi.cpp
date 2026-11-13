// ==============================================================================
// Processor MIDI Event Handling (processEvents, onNoteOn, onNoteOff)
// ==============================================================================

#include "processor.h"
#include "plugin_ids.h"

#include "midi/midi_event_dispatcher.h"

#include <algorithm>
#include <cstdint>


namespace Ruinae {

// ==============================================================================
// MIDI Event Handling
// ==============================================================================

namespace {

/// One event, presented as an event list of one. Lets Ruinae hand the shared
/// dispatcher one event at a time, so it knows each event's sampleOffset while
/// the dispatcher's callbacks run (they carry none). It holds a copy, so the
/// host list is read once per event.
class SingleEventView final : public Steinberg::Vst::IEventList {
public:
    explicit SingleEventView(const Steinberg::Vst::Event& event) noexcept : event_(event) {}
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID /*iid*/, void** obj) override {
        if (obj) *obj = nullptr;
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }
    Steinberg::int32 PLUGIN_API getEventCount() override { return 1; }
    Steinberg::tresult PLUGIN_API getEvent(Steinberg::int32 index, Steinberg::Vst::Event& e) override {
        if (index != 0) return Steinberg::kResultFalse;
        e = event_;
        return Steinberg::kResultTrue;
    }
    Steinberg::tresult PLUGIN_API addEvent(Steinberg::Vst::Event& /*e*/) override {
        return Steinberg::kResultFalse;
    }

private:
    Steinberg::Vst::Event event_;
};

} // namespace

void Processor::processEvents(Steinberg::Vst::IEventList* events, Steinberg::int32 numSamples) {
    // The shared dispatcher does not read Event.sampleOffset, so Ruinae hands it
    // one event at a time and records the offset first: the callbacks below
    // queue the engine's part of each event at that offset, and process()
    // renders the engine in segments split there (sample-accurate notes).
    if (!events) return;
    const Steinberg::int32 count = events->getEventCount();
    const auto lastSample = static_cast<uint32_t>(numSamples > 0 ? numSamples - 1 : 0);
    for (Steinberg::int32 i = 0; i < count; ++i) {
        Steinberg::Vst::Event e{};
        if (events->getEvent(i, e) != Steinberg::kResultTrue) continue;
        currentEventOffset_ = std::min(static_cast<uint32_t>(std::max<Steinberg::int32>(e.sampleOffset, 0)),
                                       lastSample);
        SingleEventView view(e);
        Krate::Plugins::dispatchMidiEvents(&view, *this);
    }
    currentEventOffset_ = 0;
}

void Processor::queueTimedEvent(const TimedEvent& event) noexcept {
    if (timedEventCount_ >= kMaxTimedEvents) {
        // Queue full: apply everything queued so far, then this event, now (at
        // the buffer start). That gives up their timing for this buffer but
        // keeps their order, so a note-off never runs before its note-on.
        flushTimedEvents();
        applyTimedEvent(event);
        return;
    }
    // Insertion keeps the queue sorted by offset and stable for equal offsets.
    size_t i = timedEventCount_;
    while (i > 0 && timedEvents_[i - 1].offset > event.offset) {
        timedEvents_[i] = timedEvents_[i - 1];
        --i;
    }
    timedEvents_[i] = event;
    ++timedEventCount_;
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static) accesses engine_
void Processor::applyTimedEvent(const TimedEvent& event) noexcept {
    switch (event.kind) {
        case TimedEventKind::NoteOn:
            if (event.noteId >= 0) engine_.noteOn(event.pitch, event.velocity, event.noteId);
            else engine_.noteOn(event.pitch, event.velocity);
            break;
        case TimedEventKind::NoteOff:
            if (event.noteId >= 0) engine_.noteOff(event.pitch, event.noteId);
            else engine_.noteOff(event.pitch);
            break;
        case TimedEventKind::ArpNoteOn:
            engine_.noteOn(event.pitch, event.velocity, event.legato);
            break;
        case TimedEventKind::ArpNoteOff:
            engine_.noteOff(event.pitch);
            break;
        case TimedEventKind::Expression:
            engine_.setNoteExpression(event.noteId, event.typeId, event.value);
            break;
        case TimedEventKind::PitchBend:
            engine_.setPitchBend(static_cast<float>(event.value));
            break;
    }
}

void Processor::flushTimedEvents() noexcept {
    for (size_t i = 0; i < timedEventCount_; ++i) applyTimedEvent(timedEvents_[i]);
    timedEventCount_ = 0;
}

void Processor::renderEngineSegments(float* left, float* right, size_t numSamples,
                                     const Krate::DSP::BlockContext& blockCtx,
                                     const float* sidechainL, const float* sidechainR) noexcept {
    size_t next = 0;
    size_t pos = 0;
    while (pos < numSamples) {
        while (next < timedEventCount_ && timedEvents_[next].offset <= pos) {
            applyTimedEvent(timedEvents_[next++]);
        }
        const size_t end = (next < timedEventCount_)
            ? std::min(static_cast<size_t>(timedEvents_[next].offset), numSamples)
            : numSamples;
        // The engine reads the transport per call (trance gate sync), so each
        // segment gets the musical time of its first sample.
        Krate::DSP::BlockContext segment = blockCtx;
        segment.blockSize = end - pos;
        if (pos > 0) {
            segment.transportPositionSamples += static_cast<int64_t>(pos);
            if (segment.projectTimeMusicValid) {
                segment.projectTimeMusic += static_cast<double>(pos) / blockCtx.sampleRate
                                          * (blockCtx.tempoBPM / 60.0);
            }
        }
        engine_.setBlockContext(segment);
        engine_.setSidechainInput(sidechainL ? sidechainL + pos : nullptr,
                                  sidechainR ? sidechainR + pos : nullptr);
        engine_.processBlock(left + pos, right + pos, end - pos);
        pos = end;
    }
    // Events past the buffer (none after clamping) apply now.
    while (next < timedEventCount_) applyTimedEvent(timedEvents_[next++]);
    timedEventCount_ = 0;
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
        queueTimedEvent({.offset = currentEventOffset_, .kind = TimedEventKind::NoteOn,
                         .pitch = midiPitch, .velocity = midiVelocity});
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
        queueTimedEvent({.offset = currentEventOffset_, .kind = TimedEventKind::NoteOff, .pitch = midiPitch});
    }
}

// ==============================================================================
// MPE Callbacks (noteId-aware overloads)
// ==============================================================================

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
void Processor::onNoteOn(int16_t pitch, float velocity, int32_t noteId) {
    auto midiPitch = static_cast<uint8_t>(pitch);
    auto midiVelocity = static_cast<uint8_t>(velocity * 127.0f + 0.5f);

    const int opMode = arpParams_.operatingMode.load(std::memory_order_relaxed);

    const bool arpRunning = (opMode != kArpOff);
    const bool arpDispatchesNotes = (opMode == kArpMIDI || opMode == kArpMIDIMod);

    if (arpRunning) {
        arpCore_.noteOn(midiPitch, midiVelocity);
    }
    if (!arpDispatchesNotes) {
        queueTimedEvent({.offset = currentEventOffset_, .kind = TimedEventKind::NoteOn,
                         .pitch = midiPitch, .velocity = midiVelocity, .noteId = noteId});
    }
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
void Processor::onNoteOff(int16_t pitch, int32_t noteId) {
    auto midiPitch = static_cast<uint8_t>(pitch);

    const int opMode = arpParams_.operatingMode.load(std::memory_order_relaxed);

    const bool arpRunning = (opMode != kArpOff);
    const bool arpDispatchesNotes = (opMode == kArpMIDI || opMode == kArpMIDIMod);

    if (arpRunning) {
        arpCore_.noteOff(midiPitch);
    }
    if (!arpDispatchesNotes) {
        queueTimedEvent({.offset = currentEventOffset_, .kind = TimedEventKind::NoteOff,
                         .pitch = midiPitch, .noteId = noteId});
    }
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
void Processor::onNoteExpression(int32_t noteId, uint32_t typeId, double value) {
    // Queued like the notes, so an expression lands after a note-on it follows.
    queueTimedEvent({.offset = currentEventOffset_, .kind = TimedEventKind::Expression,
                     .noteId = noteId, .typeId = typeId, .value = value});
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
void Processor::onPitchBend(float bipolar) {
    queueTimedEvent({.offset = currentEventOffset_, .kind = TimedEventKind::PitchBend,
                     .value = static_cast<double>(bipolar)});
}

} // namespace Ruinae
