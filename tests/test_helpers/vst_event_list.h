#pragma once

// =============================================================================
// Shared VST3 IEventList test mock
// =============================================================================
// Minimal, header-only IEventList for feeding MIDI note events into a
// processor's process() call (or capturing events it emits). Previously
// copy-pasted (with only class-name and noteId-convention differences) into
// ~50 plugin test files as TestEventList / NoteTestEventList / etc.
//
// Two flavours cover the two note-id conventions found in the codebase:
//   EventList               -> note events get noteId = -1   (the common case)
//   EventListNoteIdEqPitch  -> note events get noteId = pitch (MPE / voice-id
//                              tracking tests that rely on the pitch as id)
//
// Migrated files alias their local mock to the matching flavour, e.g.
//     using TestEventList = Krate::Test::EventList;
//
// SCOPE: reproduces the standard helpers addNoteOn(pitch, velocity,
// sampleOffset = 0) and addNoteOff(pitch, sampleOffset = 0), both on channel 0
// with tuning 0 and length 0 — byte-identical to the mocks they replace. Mocks
// with non-standard helpers (noteOn()/makeNoteOn(), reordered args, explicit
// noteId/channel parameters) or that capture output for golden byte-identical
// comparison are left local.
//
// FUnknown methods are intentionally non-refcounting: the mock lives on the
// stack for the duration of a single process() call and is never retained.
// =============================================================================

#include <pluginterfaces/vst/ivstevents.h>

#include <vector>

namespace Krate::Test {

class EventList : public Steinberg::Vst::IEventList {
public:
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID /*iid*/,
                                                 void** /*obj*/) override {
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }

    Steinberg::int32 PLUGIN_API getEventCount() override {
        return static_cast<Steinberg::int32>(events_.size());
    }

    Steinberg::tresult PLUGIN_API getEvent(Steinberg::int32 index,
                                           Steinberg::Vst::Event& e) override {
        if (index < 0 || index >= static_cast<Steinberg::int32>(events_.size())) {
            return Steinberg::kResultFalse;
        }
        e = events_[static_cast<size_t>(index)];
        return Steinberg::kResultTrue;
    }

    Steinberg::tresult PLUGIN_API addEvent(Steinberg::Vst::Event& e) override {
        events_.push_back(e);
        return Steinberg::kResultTrue;
    }

    void addNoteOn(Steinberg::int16 pitch, float velocity,
                   Steinberg::int32 sampleOffset = 0) {
        Steinberg::Vst::Event e{};
        e.type = Steinberg::Vst::Event::kNoteOnEvent;
        e.sampleOffset = sampleOffset;
        e.noteOn.channel = 0;
        e.noteOn.pitch = pitch;
        e.noteOn.velocity = velocity;
        e.noteOn.noteId = noteIdFor(pitch);
        e.noteOn.tuning = 0.0f;
        e.noteOn.length = 0;
        events_.push_back(e);
    }

    void addNoteOff(Steinberg::int16 pitch, Steinberg::int32 sampleOffset = 0) {
        Steinberg::Vst::Event e{};
        e.type = Steinberg::Vst::Event::kNoteOffEvent;
        e.sampleOffset = sampleOffset;
        e.noteOff.channel = 0;
        e.noteOff.pitch = pitch;
        e.noteOff.velocity = 0.0f;
        e.noteOff.noteId = noteIdFor(pitch);
        e.noteOff.tuning = 0.0f;
        events_.push_back(e);
    }

    void clear() { events_.clear(); }

protected:
    // Note-id assigned by addNoteOn/addNoteOff. Base = -1 ("no note id").
    virtual Steinberg::int32 noteIdFor(Steinberg::int16 /*pitch*/) const {
        return -1;
    }

    std::vector<Steinberg::Vst::Event> events_;
};

// Variant that tags each note event with noteId = pitch (used by MPE / voice-id
// tracking tests). addNoteOn/addNoteOff are non-virtual but call the virtual
// noteIdFor(), so aliasing to this type yields pitch-tagged events.
class EventListNoteIdEqPitch : public EventList {
protected:
    Steinberg::int32 noteIdFor(Steinberg::int16 pitch) const override {
        return pitch;
    }
};

}  // namespace Krate::Test
