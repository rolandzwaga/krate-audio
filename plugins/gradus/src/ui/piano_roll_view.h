// =============================================================================
// PianoRollView — VSTGUI custom view for the Sequencer Note lane
// =============================================================================
// Spec 142 (Gradus Piano-Roll Step Sequencer), Phase 6.
//
// Renders a 48-row (C2..B5) x 32-column piano roll bound to the Sequencer
// Note lane's 64 step parameters (32 pitches + 32 rest flags), length, and
// playhead per contracts/piano-roll-view.md. Mouse handling delegates to the
// VSTGUI-free state machine in piano_roll_view_logic.h (humble-object
// pattern matching pin_flag_strip_logic.h).
//
// IDependent contract: registers on the 64 step params + length + playhead
// in attached(), unregisters in removed() AND the destructor (defense in
// depth). Idempotent — multiple attached/removed calls are safe.
// =============================================================================

#pragma once

#include "piano_roll_view_logic.h"
#include "../plugin_ids.h"

#include "vstgui/lib/cview.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cframe.h"

#include "base/source/fobject.h"
#include "public.sdk/source/vst/vsteditcontroller.h"

#include <array>
#include <cstdint>

namespace Gradus {

// Namespace alias for the humble-object logic helpers. Declared at namespace
// scope (not class scope) so MSVC's class-body name lookup picks it up
// during member function signature parsing. Qualified with leading "::" so
// the outer "Gradus" name doesn't shadow itself when we're nested inside
// "namespace Gradus { ... }".
namespace PianoRollLogic = ::Gradus::PianoRollViewLogic;

class PianoRollView : public VSTGUI::CView,
                       public Steinberg::FObject
{
public:
    /// Construct a PianoRollView bound to a controller. The controller must
    /// outlive the view (standard VSTGUI lifecycle: editor owns view; the
    /// Gradus Controller nulls its `pianoRollView_` cache in willClose).
    PianoRollView(const VSTGUI::CRect& size,
                  Steinberg::Vst::EditController* controller)
        : VSTGUI::CView(size)
        , controller_(controller)
    {
        // Initial cache reflects parameter defaults; refreshed in attached().
        for (auto& s : steps_) {
            s.pitch  = 60;
            s.isRest = true;
        }
    }

    ~PianoRollView() override
    {
        // Defense in depth: if removed() wasn't called we still tear down
        // dependents cleanly.
        unregisterDependentsIfRegistered();
    }

    // --- CView overrides ---

    void draw(VSTGUI::CDrawContext* context) override
    {
        drawGrid(context);
        drawNotes(context);
        drawPlayhead(context);
        setDirty(false);
    }

    VSTGUI::CMouseEventResult onMouseDown(VSTGUI::CPoint& where,
                                          const VSTGUI::CButtonState& buttons) override
    {
        if (!getMouseEnabled()) return VSTGUI::kMouseEventNotHandled;
        const auto local = toLocal(where);
        const int activeLength = currentActiveLength();
        const int step = PianoRollLogic::stepFromX(static_cast<float>(local.x),
                                          static_cast<float>(getViewSize().getWidth()),
                                          activeLength);
        if (step < 0) return VSTGUI::kMouseEventNotHandled;
        const int pitch = PianoRollLogic::pitchFromY(static_cast<float>(local.y),
                                            static_cast<float>(getViewSize().getHeight()));
        if (pitch < 0) return VSTGUI::kMouseEventNotHandled;

        if (buttons.isRightButton()) {
            auto edit = stateMachine_.onRightMouseDown(step, steps_[stepIndex(step)]);
            applyEdit(edit);
            invalid();
            return VSTGUI::kMouseEventHandled;
        }

        if (buttons.isLeftButton()) {
            stateMachine_.onLeftMouseDown(step, pitch);
            return VSTGUI::kMouseEventHandled;
        }
        return VSTGUI::kMouseEventNotHandled;
    }

    VSTGUI::CMouseEventResult onMouseMoved(VSTGUI::CPoint& where,
                                           const VSTGUI::CButtonState& buttons) override
    {
        if (stateMachine_.state() != PianoRollLogic::MouseStateMachine::State::kDragging) {
            return VSTGUI::kMouseEventNotHandled;
        }
        if (!buttons.isLeftButton()) {
            return VSTGUI::kMouseEventNotHandled;
        }
        const auto local = toLocal(where);
        const int activeLength = currentActiveLength();
        // Drag clamps out-of-bounds x to nearest in-range column.
        const int step = PianoRollLogic::stepFromXClamped(
            static_cast<float>(local.x),
            static_cast<float>(getViewSize().getWidth()),
            activeLength);
        auto edit = stateMachine_.onMouseMovedDragging(step);
        if (edit.valid) {
            applyEdit(edit);
            invalid();
        }
        return VSTGUI::kMouseEventHandled;
    }

    VSTGUI::CMouseEventResult onMouseUp(VSTGUI::CPoint& where,
                                        const VSTGUI::CButtonState& buttons) override
    {
        (void)where; (void)buttons;
        if (stateMachine_.state() != PianoRollLogic::MouseStateMachine::State::kDragging) {
            return VSTGUI::kMouseEventNotHandled;
        }
        const int startStep = stateMachine_.dragStartStep();
        PianoRollLogic::StepData currentAtStart{ 60, true };
        if (startStep >= 0 && startStep < PianoRollLogic::kMaxSteps) {
            currentAtStart = steps_[stepIndex(startStep)];
        }
        auto edit = stateMachine_.onLeftMouseUp(currentAtStart);
        if (edit.valid) {
            applyEdit(edit);
            invalid();
        }
        return VSTGUI::kMouseEventHandled;
    }

    bool attached(VSTGUI::CView* parent) override
    {
        const bool result = VSTGUI::CView::attached(parent);
        if (!dependentsRegistered_ && controller_) {
            registerAllDependents();
            refreshAllFromController();
            dependentsRegistered_ = true;
        }
        invalid();
        return result;
    }

    bool removed(VSTGUI::CView* parent) override
    {
        unregisterDependentsIfRegistered();
        return VSTGUI::CView::removed(parent);
    }

    // --- IDependent override (UI thread, deferred) ---
    void PLUGIN_API update(Steinberg::FUnknown* changedUnknown,
                           Steinberg::int32 message) override
    {
        if (message != Steinberg::IDependent::kChanged) return;
        if (!controller_) return;
        // Identify which parameter fired and refresh only that cell.
        auto* changedParam = Steinberg::FCast<Steinberg::Vst::Parameter>(changedUnknown);
        if (!changedParam) return;
        const Steinberg::Vst::ParamID id = changedParam->getInfo().id;

        if (id >= kArpSequencerNoteLaneStep0Id && id <= kArpSequencerNoteLaneStep31Id) {
            const int i = static_cast<int>(id - kArpSequencerNoteLaneStep0Id);
            steps_[stepIndex(i)].pitch = readPitch(id);
            invalid();
            return;
        }
        if (id >= kArpSequencerNoteLaneRestStep0Id && id <= kArpSequencerNoteLaneRestStep31Id) {
            const int i = static_cast<int>(id - kArpSequencerNoteLaneRestStep0Id);
            steps_[stepIndex(i)].isRest = readRest(id);
            invalid();
            return;
        }
        if (id == kArpSequencerNoteLaneLengthId) {
            activeLength_ = readLength();
            invalid();
            return;
        }
        if (id == kArpSequencerNoteLanePlayheadId) {
            playheadStep_ = readPlayhead();
            invalid();
            return;
        }
    }

    // --- Configuration ---
    void setAccentColor(VSTGUI::CColor color) { accentColor_ = color; invalid(); }

    CLASS_METHODS(PianoRollView, VSTGUI::CView)

private:
    // -------------------------------------------------------------------------
    // State
    // -------------------------------------------------------------------------
    Steinberg::Vst::EditController* controller_ = nullptr;
    PianoRollLogic::StepArray                steps_{};
    int                             activeLength_         = 16;
    int                             playheadStep_         = -1;
    bool                            dependentsRegistered_ = false;
    PianoRollLogic::MouseStateMachine        stateMachine_{};

    // Drawing palette.
    VSTGUI::CColor accentColor_     {0xD4, 0xA8, 0x56, 0xFF}; // gold
    VSTGUI::CColor backgroundColor_ {0x1A, 0x1A, 0x2E, 0xFF};
    VSTGUI::CColor gridLineColor_   {0x30, 0x30, 0x44, 0xFF};
    VSTGUI::CColor restColor_       {0x44, 0x44, 0x55, 0x80};
    VSTGUI::CColor inactiveColor_   {0x10, 0x10, 0x18, 0xFF};
    VSTGUI::CColor playheadColor_   {0xFF, 0xFF, 0xFF, 0x30};

    // -------------------------------------------------------------------------
    // Helpers
    // -------------------------------------------------------------------------
    [[nodiscard]] static int stepIndex(int s) noexcept {
        if (s < 0) return 0;
        if (s >= PianoRollLogic::kMaxSteps) return PianoRollLogic::kMaxSteps - 1;
        return s;
    }

    [[nodiscard]] int currentActiveLength() const noexcept {
        return PianoRollLogic::clampActiveLength(activeLength_);
    }

    [[nodiscard]] VSTGUI::CPoint toLocal(const VSTGUI::CPoint& parent) const noexcept {
        const auto& size = getViewSize();
        return VSTGUI::CPoint{ parent.x - size.left, parent.y - size.top };
    }

    [[nodiscard]] VSTGUI::CRect cellRect(int step, int pitch) const noexcept {
        const auto& size = getViewSize();
        const auto w   = static_cast<float>(size.getWidth());
        const auto h   = static_cast<float>(size.getHeight());
        const int    len = currentActiveLength();
        const float  cw  = PianoRollLogic::colWidth(w, len);
        const float  rh  = PianoRollLogic::rowHeight(h);
        const float  x   = static_cast<float>(step) * cw;
        const int    row = PianoRollLogic::kMidiHigh - pitch;
        const float  y   = static_cast<float>(row) * rh;
        return VSTGUI::CRect{
            size.left + static_cast<VSTGUI::CCoord>(x),
            size.top  + static_cast<VSTGUI::CCoord>(y),
            size.left + static_cast<VSTGUI::CCoord>(x + cw),
            size.top  + static_cast<VSTGUI::CCoord>(y + rh)
        };
    }

    // -------------------------------------------------------------------------
    // Drawing
    // -------------------------------------------------------------------------
    void drawGrid(VSTGUI::CDrawContext* ctx)
    {
        const auto& size = getViewSize();
        ctx->setFillColor(backgroundColor_);
        ctx->drawRect(size, VSTGUI::kDrawFilled);

        // Inactive (right-of-length) region is dimmed.
        const int   len = currentActiveLength();
        const float w   = static_cast<float>(size.getWidth());
        const float cw  = PianoRollLogic::colWidth(w, len);
        if (len < PianoRollLogic::kMaxSteps && cw > 0.0f) {
            // We only paint visible columns 0..len-1 so nothing extra is
            // drawn beyond — but tinting helps if the visible width still
            // covers all 32 columns visually. (No-op here; reserved for
            // future styling.)
            (void)cw;
        }

        // Light horizontal lines once per octave (every 12 rows from the top).
        ctx->setFrameColor(gridLineColor_);
        ctx->setLineWidth(1.0);
        const float rh = PianoRollLogic::rowHeight(static_cast<float>(size.getHeight()));
        for (int row = 0; row <= PianoRollLogic::kPitchRows; row += 12) {
            const float y = static_cast<float>(row) * rh;
            ctx->drawLine(
                VSTGUI::CPoint{size.left,
                               size.top + static_cast<VSTGUI::CCoord>(y)},
                VSTGUI::CPoint{size.right,
                               size.top + static_cast<VSTGUI::CCoord>(y)});
        }
        // Vertical step boundaries (every 4 steps).
        for (int step = 0; step <= len; step += 4) {
            const float x = static_cast<float>(step) * cw;
            ctx->drawLine(
                VSTGUI::CPoint{size.left + static_cast<VSTGUI::CCoord>(x), size.top},
                VSTGUI::CPoint{size.left + static_cast<VSTGUI::CCoord>(x), size.bottom});
        }
    }

    void drawNotes(VSTGUI::CDrawContext* ctx)
    {
        const int len = currentActiveLength();
        for (int s = 0; s < len; ++s) {
            const auto& cell = steps_[stepIndex(s)];
            if (cell.isRest) {
                // Render a small rest marker centered horizontally on the row
                // (skip if pitch out of visible range — pitch 60 default).
                const int p = static_cast<int>(cell.pitch);
                if (p < PianoRollLogic::kMidiLow || p > PianoRollLogic::kMidiHigh) continue;
                auto rect = cellRect(s, p);
                rect.inset(2.0, 5.0);
                ctx->setFillColor(restColor_);
                ctx->drawRect(rect, VSTGUI::kDrawFilled);
            } else {
                const int p = static_cast<int>(cell.pitch);
                if (p < PianoRollLogic::kMidiLow || p > PianoRollLogic::kMidiHigh) continue;
                auto rect = cellRect(s, p);
                rect.inset(1.0, 1.0);
                ctx->setFillColor(accentColor_);
                ctx->drawRect(rect, VSTGUI::kDrawFilled);
            }
        }
    }

    void drawPlayhead(VSTGUI::CDrawContext* ctx)
    {
        if (playheadStep_ < 0) return;
        const auto& size = getViewSize();
        const int   len  = currentActiveLength();
        if (playheadStep_ >= len) return;
        const float cw   = PianoRollLogic::colWidth(static_cast<float>(size.getWidth()), len);
        const float x    = static_cast<float>(playheadStep_) * cw;
        VSTGUI::CRect r{
            size.left + static_cast<VSTGUI::CCoord>(x),
            size.top,
            size.left + static_cast<VSTGUI::CCoord>(x + cw),
            size.bottom
        };
        ctx->setFillColor(playheadColor_);
        ctx->drawRect(r, VSTGUI::kDrawFilled);
    }

    // -------------------------------------------------------------------------
    // Parameter binding
    // -------------------------------------------------------------------------
    [[nodiscard]] std::uint8_t readPitch(Steinberg::Vst::ParamID id) const
    {
        auto* p = controller_ ? controller_->getParameterObject(id) : nullptr;
        if (!p) return 60;
        // Pitches are RangeParameter(0..127). toPlain returns the int value.
        const double plain = p->toPlain(p->getNormalized());
        int v = static_cast<int>(plain + 0.5);
        if (v < 0) v = 0;
        if (v > 127) v = 127;
        return static_cast<std::uint8_t>(v);
    }

    [[nodiscard]] bool readRest(Steinberg::Vst::ParamID id) const
    {
        auto* p = controller_ ? controller_->getParameterObject(id) : nullptr;
        if (!p) return true;
        return p->getNormalized() >= 0.5;
    }

    [[nodiscard]] int readLength() const
    {
        auto* p = controller_ ? controller_->getParameterObject(
            kArpSequencerNoteLaneLengthId) : nullptr;
        if (!p) return 16;
        // Length is RangeParameter(1..32) — plain is the count directly.
        const double plain = p->toPlain(p->getNormalized());
        int v = static_cast<int>(plain + 0.5);
        return PianoRollLogic::clampActiveLength(v);
    }

    [[nodiscard]] int readPlayhead() const
    {
        auto* p = controller_ ? controller_->getParameterObject(
            kArpSequencerNoteLanePlayheadId) : nullptr;
        if (!p) return -1;
        return PianoRollLogic::playheadStepFromParam(p->getNormalized(), currentActiveLength());
    }

    void refreshAllFromController()
    {
        if (!controller_) return;
        activeLength_ = readLength();
        for (int i = 0; i < PianoRollLogic::kMaxSteps; ++i) {
            steps_[static_cast<size_t>(i)].pitch =
                readPitch(static_cast<Steinberg::Vst::ParamID>(
                    kArpSequencerNoteLaneStep0Id + i));
            steps_[static_cast<size_t>(i)].isRest =
                readRest(static_cast<Steinberg::Vst::ParamID>(
                    kArpSequencerNoteLaneRestStep0Id + i));
        }
        playheadStep_ = readPlayhead();
    }

    void registerAllDependents()
    {
        if (!controller_) return;
        auto reg = [&](Steinberg::Vst::ParamID id) {
            if (auto* p = controller_->getParameterObject(id)) {
                p->addDependent(this);
            }
        };
        for (int i = 0; i < PianoRollLogic::kMaxSteps; ++i) {
            reg(static_cast<Steinberg::Vst::ParamID>(kArpSequencerNoteLaneStep0Id + i));
            reg(static_cast<Steinberg::Vst::ParamID>(kArpSequencerNoteLaneRestStep0Id + i));
        }
        reg(kArpSequencerNoteLaneLengthId);
        reg(kArpSequencerNoteLanePlayheadId);
    }

    void unregisterDependentsIfRegistered()
    {
        if (!dependentsRegistered_ || !controller_) {
            dependentsRegistered_ = false;
            return;
        }
        auto unreg = [&](Steinberg::Vst::ParamID id) {
            if (auto* p = controller_->getParameterObject(id)) {
                p->removeDependent(this);
            }
        };
        for (int i = 0; i < PianoRollLogic::kMaxSteps; ++i) {
            unreg(static_cast<Steinberg::Vst::ParamID>(kArpSequencerNoteLaneStep0Id + i));
            unreg(static_cast<Steinberg::Vst::ParamID>(kArpSequencerNoteLaneRestStep0Id + i));
        }
        unreg(kArpSequencerNoteLaneLengthId);
        unreg(kArpSequencerNoteLanePlayheadId);
        dependentsRegistered_ = false;
    }

    // -------------------------------------------------------------------------
    // Edit dispatch
    // -------------------------------------------------------------------------
    void applyEdit(const PianoRollLogic::MouseStateMachine::PendingEdit& edit)
    {
        if (!edit.valid || !controller_) return;
        const int idx = stepIndex(edit.step);

        // Update local cache immediately (don't wait for update() round-trip).
        steps_[idx].pitch  = static_cast<std::uint8_t>(
            std::clamp(edit.pitch, 0, 127));
        steps_[idx].isRest = edit.isRest;

        // Pitch param: edit only when not a rest-only toggle (we still write
        // the pitch so the underlying value matches what the view drew).
        const auto pitchId =
            static_cast<Steinberg::Vst::ParamID>(kArpSequencerNoteLaneStep0Id + idx);
        auto* pitchParam = controller_->getParameterObject(pitchId);
        if (pitchParam) {
            const double norm = pitchParam->toNormalized(static_cast<double>(edit.pitch));
            editParam(pitchId, norm);
        }

        // Rest flag param.
        const auto restId =
            static_cast<Steinberg::Vst::ParamID>(kArpSequencerNoteLaneRestStep0Id + idx);
        editParam(restId, edit.isRest ? 1.0 : 0.0);
    }

    // Direct begin/perform/end on the bound controller. We avoid calling
    // Gradus::Controller::editParamWithNotify here because PianoRollView is a
    // generic CView that only sees the base EditController* type — the view
    // is unit-testable without coupling to the Gradus controller subclass.
    void editParam(Steinberg::Vst::ParamID id, double value) const
    {
        if (!controller_) return;
        double v = value;
        if (v < 0.0) v = 0.0;
        if (v > 1.0) v = 1.0;
        controller_->beginEdit(id);
        controller_->setParamNormalized(id, v);
        controller_->performEdit(id, v);
        controller_->endEdit(id);
    }
};

} // namespace Gradus
