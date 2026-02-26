# Copy/Paste Contract

## Clipboard Structure

See `data-model.md` for the authoritative `LaneClipboard` struct and `ClipboardLaneType` enum definitions. Summary: `LaneClipboard` holds `std::array<float, 32> values`, `int length`, `ClipboardLaneType sourceType`, and `bool hasData`. It is in-memory and editor-scoped (cleared on editor close).

## Copy Operation

1. User right-clicks lane header -> context menu appears
2. User selects "Copy"
3. Controller reads all step values from the lane as normalized floats
4. Controller stores: values, length, sourceType in clipboard
5. Sets `hasData = true`
6. Enables Paste option on all lane headers

## Paste Operation

1. User right-clicks lane header -> context menu appears (Paste enabled if clipboard has data)
2. User selects "Paste"
3. Controller retrieves clipboard data
4. Apply clipboard values verbatim (same-type and cross-type paste both copy normalized values directly -- no additional range conversion; see Cross-Type Normalization below)
5. Update target lane length to match clipboard length
6. Each step: beginEdit/performEdit/endEdit for undo support

## Cross-Type Normalization

Source values are already stored as normalized 0.0-1.0. No additional normalization is needed because:
- Velocity: 0-1 -> already 0-1 normalized
- Gate: 0-1 -> already 0-1 normalized
- Pitch: stored as normalized (0.5 = 0 semitones) -> already 0-1
- Ratchet: stored as normalized (count-1)/3 -> already 0-1
- Modifier: stored as bitmask/15 -> already 0-1
- Condition: stored as index/17 -> already 0-1

Since all lanes store step values as normalized 0.0-1.0 at the VST parameter level, cross-type paste simply copies the normalized values. The target lane's parameter registration handles denormalization to the correct plain range.

The key insight: all step parameters are already normalized at the VST boundary. Cross-type paste preserves the normalized shape. A velocity ramp (0.0, 0.33, 0.67, 1.0) pasted into pitch becomes a pitch ramp from -24 to +24 semitones because the pitch parameter maps 0.0->-24 and 1.0->+24.

## Context Menu

```
Right-click on lane header:
+----------+
| Copy     |
| Paste    |  <- grayed out if clipboard.hasData == false
+----------+
```

## Implementation Location

- Clipboard storage: `Ruinae::Controller` member (`Krate::Plugins::LaneClipboard clipboard_`)
- Context menu: `ArpLaneHeader::handleRightClick()` with callbacks to controller
- Paste enabled state: Propagated from controller to all lane headers via `setPasteEnabled()`
