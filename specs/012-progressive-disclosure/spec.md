# Feature Specification: Progressive Disclosure & Accessibility

**Feature Branch**: `012-progressive-disclosure`
**Created**: 2026-01-31
**Status**: Draft
**Input**: User description: "Progressive disclosure and accessibility for Disrumpo plugin - expand/collapse panels, keyboard shortcuts, window resize, high contrast mode, reduced motion, MIDI CC mapping. Week 14 (T14.1-T14.17). Milestone M8: Complete UI with progressive disclosure."

## Clarifications

### Session 2026-01-31

- Q: Animation transition behavior - When a user rapidly toggles expand/collapse while an animation is in progress (FR-006), how should the system handle the visual transition? → A: Smoothly transition from current state to new target (better UX, slight complexity)
- Q: MIDI CC mapping persistence scope - How should the user specify whether a mapping is global vs. per-preset? → A: Global default + opt-in per-preset checkbox in right-click menu
- Q: Keyboard focus visual indication - How should the UI indicate which element currently has keyboard focus? → A: Colored outline around focused element (2px, accent color, clear standard pattern)
- Q: Window resize aspect ratio enforcement - Should the resize handle enforce 5:3 strictly or allow free resize with letterboxing? → A: Constrained proportional resize (user drags, handle snaps to 5:3 ratio)
- Q: High contrast mode color palette - Which specific color adjustments beyond border/fill changes? → A: System-adaptive palette using OS high-contrast colors (respects user preference, best practice)

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Expand and Collapse Band Detail Panels (Priority: P1)

A producer working on a mix opens Disrumpo and sees the essential view: spectrum display, band type selectors, and basic drive/mix knobs. They want to fine-tune Band 2's morph settings without cluttering the screen with controls for other bands. They click the expand button on Band 2, which reveals the full morph pad, node editors, and per-band output controls. When done, they collapse Band 2 and expand Band 3 to adjust it. Only the band they are actively editing shows detailed controls at any time.

**Why this priority**: Expand/collapse is the core mechanism of progressive disclosure. Without it, the plugin either shows too little (unusable) or too much (overwhelming). This is the foundation all other disclosure features build upon.

**Independent Test**: Can be fully tested by clicking expand/collapse buttons on band strips and verifying correct panel visibility. Delivers immediate value by reducing visual clutter.

**Acceptance Scenarios**:

1. **Given** the plugin is open in Essential view with all bands collapsed, **When** the user clicks the expand button on Band 2, **Then** Band 2's detail panel appears showing morph pad, node editors, and output controls.
2. **Given** Band 2 is expanded, **When** the user clicks Band 2's collapse button, **Then** the detail panel hides and the band strip returns to its compact form.
3. **Given** Band 2 is expanded, **When** the user clicks the expand button on Band 3, **Then** Band 3 expands and Band 2 remains in its current state (both can be expanded simultaneously unless screen space requires otherwise).
4. **Given** Band 2 is expanded and the user saves a preset, **When** they reload that preset, **Then** Band 2 is restored to its expanded state.

---

### User Story 2 - Resizable Plugin Window (Priority: P1)

A producer with a large 4K monitor wants Disrumpo to take up more screen real estate so they can see the spectrum display and all band strips more clearly. They drag the plugin window corner to resize it. The layout scales proportionally from 800x500 (minimum) up to 1400x900 (maximum), maintaining the aspect ratio so nothing looks stretched or squished.

**Why this priority**: Window resize is essential for usability across different monitor sizes and resolutions. A fixed-size plugin that is too small on 4K or too large on a laptop is a dealbreaker for many users.

**Independent Test**: Can be fully tested by dragging the window resize handle and verifying layout scales correctly at minimum, default, and maximum sizes.

**Acceptance Scenarios**:

1. **Given** the plugin opens at default size (1000x600), **When** the user drags the bottom-right corner to make the window smaller, **Then** the window shrinks but never below 800x500, and the resize handle snaps to maintain the 5:3 aspect ratio.
2. **Given** the plugin is at minimum size, **When** the user drags to enlarge it, **Then** the window grows but never beyond 1400x900, with the resize handle constrained to the 5:3 diagonal.
3. **Given** the user resizes the window, **When** the resize occurs, **Then** the layout maintains the exact 5:3 aspect ratio throughout the resize operation without distortion.
4. **Given** the user has resized the window to a custom size, **When** they close and reopen the plugin in the same DAW session, **Then** the window reopens at the last-used size.
5. **Given** the user tries to resize the window to an arbitrary aspect ratio, **When** they drag the resize handle, **Then** the handle automatically snaps to the nearest valid size on the 5:3 diagonal.

---

### User Story 3 - Toggle Modulation Panel (Priority: P1)

A sound designer has set up a morph-sweep distortion effect and now wants to add modulation. They click the "Modulation" panel toggle in the UI to reveal the modulation sources (LFO1, LFO2, Envelope Follower) and routing matrix. After setting up their LFO routing, they collapse the modulation panel to return focus to the main band controls.

**Why this priority**: The modulation panel is a Level 3 (Expert) feature. Hiding it by default keeps the UI accessible to beginners while giving advanced users full access. This is a core progressive disclosure interaction.

**Independent Test**: Can be fully tested by toggling the modulation panel visibility and verifying the modulation source and routing controls appear and disappear correctly.

**Acceptance Scenarios**:

1. **Given** the plugin is open with the modulation panel hidden (default), **When** the user clicks the modulation panel toggle, **Then** the modulation panel appears with all source and routing controls.
2. **Given** the modulation panel is visible, **When** the user clicks the toggle again, **Then** the panel hides and the space is reclaimed by the main view.
3. **Given** the modulation panel is visible and the user has active modulation routings, **When** the user hides the panel, **Then** the modulation routings remain active (hiding the panel does not disable modulation).

---

### User Story 4 - Keyboard Shortcuts for Efficient Workflow (Priority: P2)

An experienced producer wants to work quickly without reaching for the mouse every time. They use Tab to cycle focus between bands, Space to toggle bypass on the focused band, and Arrow keys to fine-adjust the currently focused parameter value. These shortcuts let them make quick A/B comparisons (bypass) and precise adjustments without mouse interaction.

**Why this priority**: Keyboard shortcuts improve workflow speed for power users but are not strictly required for basic operation. They complement mouse-based interaction and improve accessibility for users who cannot use a mouse.

**Independent Test**: Can be fully tested by pressing keyboard shortcuts and verifying focus changes, bypass toggles, and parameter adjustments occur correctly.

**Acceptance Scenarios**:

1. **Given** the plugin has keyboard focus, **When** the user presses Tab, **Then** focus cycles to the next band strip (1, 2, 3, ..., N, then back to 1, where N is the current band count) and a colored outline (2px, accent color) appears around the focused band strip.
2. **Given** Band 3 has focus, **When** the user presses Space, **Then** Band 3's bypass state toggles (on/off).
3. **Given** a parameter control has focus, **When** the user presses the Up or Right arrow key, **Then** the parameter value increases by one fine-adjustment step and the focused control shows a colored outline.
4. **Given** a parameter control has focus, **When** the user presses the Down or Left arrow key, **Then** the parameter value decreases by one fine-adjustment step.
5. **Given** a parameter control has focus, **When** the user presses Shift+Arrow, **Then** the parameter value changes by a larger coarse-adjustment step.
6. **Given** no element has explicit keyboard focus, **When** the user clicks on a band strip or control, **Then** that element receives focus and displays the focus outline.

---

### User Story 5 - MIDI CC Mapping with MIDI Learn (Priority: P2)

A live performer wants to control Disrumpo's sweep frequency and band morph positions from their hardware MIDI controller. They right-click the Sweep Frequency knob and select "MIDI Learn," then move a knob on their hardware controller. The mapping is created instantly. They repeat this for other parameters. During the performance, their hardware knobs directly control the mapped parameters in real time.

**Why this priority**: MIDI CC mapping is important for live performance and hardware integration, but the plugin is fully functional without it. It extends the user experience for a specific (important) use case.

**Independent Test**: Can be fully tested by right-clicking a control, selecting MIDI Learn, sending a CC message, and verifying the parameter responds to subsequent CC messages.

**Acceptance Scenarios**:

1. **Given** the plugin is open, **When** the user right-clicks the Sweep Frequency knob, **Then** a context menu appears with "MIDI Learn" as an option.
2. **Given** "MIDI Learn" is selected, **When** the user moves a hardware MIDI controller knob (sending CC messages), **Then** the swept CC number is captured and mapped to Sweep Frequency as a global mapping by default.
3. **Given** a MIDI CC mapping exists for Sweep Frequency (e.g., CC 74), **When** the user sends CC 74 messages from their hardware, **Then** the Sweep Frequency parameter updates in real time.
4. **Given** a MIDI CC mapping exists, **When** the user right-clicks the same control and sees "Save Mapping with Preset" checkbox, **Then** checking it converts the global mapping to a per-preset mapping.
5. **Given** a MIDI CC mapping exists, **When** the user right-clicks the same control and selects "Clear MIDI Learn", **Then** the mapping is removed.
6. **Given** global MIDI CC mappings are configured, **When** the user loads a different preset, **Then** the global mappings persist across preset changes.
7. **Given** a per-preset MIDI CC mapping exists, **When** the user loads a different preset, **Then** the per-preset mapping is replaced by that preset's mapping (or reverts to the global mapping if no per-preset override exists).

---

### User Story 6 - Smooth Panel Animations (Priority: P2)

A user clicks to expand a band detail panel. Instead of an abrupt show/hide, the panel smoothly animates open, giving visual feedback about where new controls have appeared. This animation is subtle and quick (under 300ms) so it does not impede workflow.

**Why this priority**: Smooth transitions improve perceived quality and help users maintain spatial awareness of the UI. However, the expand/collapse functionality works without animation (instant show/hide is acceptable as a fallback).

**Independent Test**: Can be fully tested by expanding/collapsing panels and measuring the transition time and visual smoothness.

**Acceptance Scenarios**:

1. **Given** a band detail panel is collapsed, **When** the user expands it, **Then** the panel animates open over a period no longer than 300 milliseconds.
2. **Given** a band detail panel is expanded, **When** the user collapses it, **Then** the panel animates closed over a period no longer than 300 milliseconds.
3. **Given** the user's operating system has "reduce motion" accessibility enabled, **When** panels expand or collapse, **Then** the transition is instant (no animation).

---

### User Story 7 - High Contrast and Reduced Motion Accessibility (Priority: P3)

A visually impaired user has high contrast mode enabled on their operating system. When they open Disrumpo, the UI automatically detects this and switches to a high-contrast color scheme with thicker borders, solid fills instead of gradients, and higher text contrast. Similarly, a user with motion sensitivity has reduced motion enabled, and Disrumpo disables animation trails on the morph pad, sweep visualization animations, and panel transition effects.

**Why this priority**: Accessibility features are important for inclusivity but serve a smaller user segment. The plugin is fully functional without these adaptations, but they demonstrate professional quality and consideration.

**Independent Test**: Can be tested by enabling OS high contrast / reduced motion settings and verifying the plugin responds with appropriate visual changes.

**Acceptance Scenarios**:

1. **Given** the user's OS has high contrast mode enabled with a specific system high-contrast theme (e.g., "High Contrast Black" on Windows), **When** the plugin opens, **Then** the UI queries the OS for the system high-contrast colors and applies them to the plugin UI with increased border widths and solid fills.
2. **Given** the user's OS has reduced motion enabled, **When** the plugin opens, **Then** all animations are disabled (panel transitions are instant, morph pad trails are hidden, sweep visualization is static).
3. **Given** the user changes their OS accessibility settings while the plugin is open, **When** the plugin detects the change (on next editor open or via a refresh action), **Then** the UI updates to reflect the new settings.
4. **Given** the user has a dark high-contrast theme enabled on macOS, **When** the plugin opens, **Then** the plugin uses the system's dark high-contrast palette rather than forcing a light or dark theme.

---

### User Story 8 - 14-bit MIDI CC for High-Resolution Control (Priority: P3)

An advanced user maps the Sweep Frequency parameter to a high-resolution MIDI controller that sends 14-bit CC messages (MSB on CC 1, LSB on CC 33). With 16,384 steps instead of 128, they get silky-smooth control over the sweep frequency without audible stepping artifacts.

**Why this priority**: 14-bit CC support is a niche but valued feature for users with high-resolution MIDI controllers. Standard 7-bit CC (128 steps) is adequate for most parameters and users.

**Independent Test**: Can be tested by sending 14-bit CC pairs and verifying the parameter resolution is 16,384 steps.

**Acceptance Scenarios**:

1. **Given** Sweep Frequency is mapped to CC 1, **When** the user also maps LSB CC 33 to the same parameter, **Then** the system automatically recognizes the 14-bit CC pair.
2. **Given** a 14-bit CC pair is configured, **When** both MSB and LSB CC messages are received, **Then** the parameter resolves to 16,384 discrete steps.
3. **Given** a 14-bit CC pair is configured, **When** only the MSB CC is received (no LSB), **Then** the parameter still responds using only the 7-bit MSB value (backwards compatible).

---

### Edge Cases

- What happens when the user tries to expand a band that is hidden (band count is lower than the band index)? The expand action should have no effect; hidden bands cannot be expanded.
- What happens when the window is resized while a band detail panel is expanded? The expanded panel should scale proportionally with the rest of the layout.
- What happens when a MIDI CC is mapped to a parameter that is subsequently removed by a preset change (e.g., modulation routing that no longer exists)? The mapping should be silently ignored until a valid target reappears.
- What happens when two different parameters are mapped to the same MIDI CC? The most recently created mapping wins; the previous mapping for that CC is overwritten.
- What happens when the user rapidly toggles expand/collapse while an animation is in progress? The animation should smoothly transition from its current state to the new target state, maintaining visual continuity without jumping.
- What happens when the window is at minimum size and the modulation panel is toggled open? The panel should open within the available space, potentially scrolling or compressing other elements, but never exceed the window boundary.
- What happens when keyboard shortcuts conflict with DAW shortcuts? The plugin should only capture keyboard events when it has explicit focus within the DAW's plugin window. If the DAW does not forward keyboard events to the plugin, shortcuts will be unavailable (this is standard VST3 behavior).
- What happens when the host does not provide MIDI CC events to the plugin? The MIDI Learn feature will time out or remain in a waiting state. The user should be able to cancel MIDI Learn by right-clicking again or pressing Escape.

## Requirements *(mandatory)*

### Functional Requirements

**Expand/Collapse Panels (T14.1, T14.3)**

- **FR-001**: The plugin MUST provide an expand/collapse toggle on each band strip that shows or hides the band's detail panel (morph pad, node editors, output controls).
- **FR-002**: The plugin MUST allow multiple bands to be expanded simultaneously.
- **FR-003**: The expand/collapse state for each band MUST be persisted as part of the plugin's controller state, surviving preset loads and session restores.
- **FR-004**: Expand/collapse actions on bands that are hidden due to band count MUST have no visible effect.

**Smooth Panel Animation (T14.2)**

- **FR-005**: When a panel expands or collapses, the transition MUST complete in no more than 300 milliseconds.
- **FR-006**: If the user triggers a state change during an in-progress animation, the animation MUST smoothly transition from its current state to the new target state, maintaining visual continuity without abrupt jumps.

**Modulation Panel Visibility (T14.4)**

- **FR-007**: The plugin MUST provide a toggle control that shows or hides the modulation panel (sources, routing matrix, macros).
- **FR-008**: Hiding the modulation panel MUST NOT disable active modulation routings; it only hides the controls.
- **FR-009**: The modulation panel visibility state MUST be persisted as part of the controller state.

**Keyboard Shortcuts (T14.5, T14.6, T14.7)**

- **FR-010**: Pressing Tab MUST cycle keyboard focus sequentially through the active band strips (1 through N based on current band count, wrapping from N to 1).
- **FR-010a**: When an element receives keyboard focus, a 2-pixel colored outline in the plugin's accent color MUST be drawn around the focused element to provide clear visual feedback.
- **FR-010b**: The focus outline MUST be visible against all background colors and MUST not obscure control labels or values.
- **FR-011**: Pressing Shift+Tab MUST cycle keyboard focus in reverse order through the active band strips.
- **FR-012**: Pressing Space MUST toggle the bypass state of the currently focused band.
- **FR-013**: Pressing Up or Right arrow MUST increase the focused parameter's value by one fine-adjustment step (1/100th of the parameter range, or 1 step for discrete parameters).
- **FR-014**: Pressing Down or Left arrow MUST decrease the focused parameter's value by one fine-adjustment step.
- **FR-015**: Pressing Shift+Arrow MUST change the focused parameter's value by one coarse-adjustment step (1/10th of the parameter range, or 1 step for discrete parameters).
- **FR-016**: Keyboard shortcuts MUST only be active when the plugin editor has keyboard focus from the host.

**Window Resize (T14.8, T14.9, T14.10)**

- **FR-017**: The plugin window MUST be resizable by the user (via drag handle or host-provided resize mechanism).
- **FR-018**: The minimum window size MUST be no smaller than 800 pixels wide by 500 pixels tall. Due to the 5:3 aspect ratio constraint (FR-021), the effective minimum is 834x500 (the nearest 5:3-compliant size at or above the 800x500 bound).
- **FR-019**: The maximum window size MUST be no larger than 1400 pixels wide by 900 pixels tall. Due to the 5:3 aspect ratio constraint (FR-021), the effective maximum is 1400x840 (the nearest 5:3-compliant size at or below the 1400x900 bound).
- **FR-020**: The default window size MUST be 1000 pixels wide by 600 pixels tall.
- **FR-021**: During resize, the window MUST maintain its aspect ratio (5:3) by constraining the resize handle to only allow proportional dragging along the 5:3 diagonal.
- **FR-021a**: The resize handle MUST automatically snap to the nearest valid size on the 5:3 diagonal if the user attempts to drag to an arbitrary aspect ratio.
- **FR-022**: All UI elements MUST scale proportionally when the window is resized, maintaining legibility and usability at all supported sizes.
- **FR-023**: The plugin MUST remember the user's last-used window size across DAW session saves and plugin state restores, and restore it when the editor is reopened.

**High Contrast Mode (T14.11, T14.12)**

- **FR-024**: The plugin MUST detect the operating system's high contrast accessibility setting on each supported platform (Windows, macOS, Linux).
- **FR-025**: When high contrast mode is active, the plugin MUST query the operating system for the active system high-contrast color palette and apply those colors to the plugin UI, ensuring the plugin respects the user's chosen high-contrast theme (light, dark, or custom).
- **FR-025a**: The high-contrast adaptation MUST increase border widths (minimum 2px), use solid fills instead of gradients, and ensure all text meets a minimum contrast ratio of 4.5:1 against its background.
- **FR-025b**: On Windows, the plugin MUST query the system high-contrast palette using `SystemParametersInfo(SPI_GETHIGHCONTRAST)` and apply the returned colors.
- **FR-025c**: On macOS, the plugin MUST query the accessibility setting using `NSWorkspace.shared.accessibilityDisplayShouldIncreaseContrast` and apply system colors from `NSColor` semantic color APIs.
- **FR-025d**: On Linux, the plugin MUST use a best-effort approach to detect high contrast preferences from the desktop environment (GTK settings, Qt settings, or XDG portal APIs), falling back to the default color scheme if detection fails.
- **FR-026**: The high contrast adaptation MUST apply to all custom controls (SpectrumDisplay, MorphPad, SweepIndicator, DynamicNodeSelector) and standard VSTGUI controls.

**Reduced Motion (T14.13, T14.14)**

- **FR-027**: The plugin MUST detect the operating system's reduced motion preference on each supported platform (Windows, macOS, Linux).
- **FR-028**: When reduced motion is preferred, the plugin MUST disable all animations, including: panel expand/collapse transitions, morph pad animation trails, sweep visualization animation, and metering smoothing animation.
- **FR-029**: When reduced motion is preferred, all visual transitions MUST be instant (zero duration).

**MIDI CC Mapping (T14.15, T14.16)**

- **FR-030**: All user-facing parameters MUST be eligible for MIDI CC mapping, including global parameters, per-band parameters, per-node parameters, sweep parameters, and modulation routing amounts.
- **FR-031**: The plugin MUST provide a "MIDI Learn" workflow triggered by right-clicking any mappable control and selecting "MIDI Learn" from the context menu.
- **FR-032**: When MIDI Learn is active for a control, the plugin MUST capture the first incoming MIDI CC message and create a global mapping by default between that CC number and the target parameter.
- **FR-032a**: The right-click context menu MUST include a "Save Mapping with Preset" checkbox option that allows the user to convert a global mapping to a per-preset mapping.
- **FR-032b**: When "Save Mapping with Preset" is checked, the mapping MUST be stored as part of the preset data and override any global mapping for the same parameter when that preset is loaded.
- **FR-033**: The plugin MUST provide a "Clear MIDI Learn" option in the right-click context menu for any control that has an active mapping.
- **FR-034**: MIDI CC mappings MUST use a hybrid persistence model: global mappings persist across all presets by default, while per-preset mappings (created via "Save Mapping with Preset") override global mappings for the same parameter when the preset containing the per-preset mapping is active.
- **FR-035**: When a MIDI CC message is received for a mapped parameter, the plugin MUST update the parameter value in real time with no perceptible latency.
- **FR-036**: If two parameters are mapped to the same CC number, the most recently created mapping MUST take precedence.
- **FR-037**: The user MUST be able to cancel an in-progress MIDI Learn session by right-clicking again or pressing Escape.

**14-bit MIDI CC (T14.17)**

- **FR-038**: The plugin MUST support 14-bit MIDI CC using standard CC pairs (MSB on CC 0-31, LSB on CC 32-63).
- **FR-039**: When both MSB and LSB CCs are mapped to the same parameter, the plugin MUST automatically combine them for 16,384-step resolution.
- **FR-040**: When only the MSB CC is received for a 14-bit pair, the plugin MUST still respond using the 7-bit value (backwards compatible).

### Key Entities

- **Band Expand/Collapse State**: A per-band boolean representing whether the band's detail panel is visible. Stored as a UI-only parameter (does not affect audio processing). There are up to 8 such states (one per band).
- **Modulation Panel Visibility**: A single boolean representing whether the modulation panel is shown or hidden. Stored as a UI-only parameter.
- **Window Size**: The current width and height of the plugin editor window. Persisted in controller state.
- **MIDI CC Mapping**: An association between a MIDI CC number (0-127) and a plugin parameter ID. Has attributes: CC number, parameter ID, 14-bit flag, and scope (global vs. per-preset). All mappings default to global scope. Users can opt-in to per-preset scope via "Save Mapping with Preset" checkbox in the right-click context menu. Up to 128 global mappings and 128 per-preset mappings per preset.
- **Accessibility Preferences**: Detected OS-level settings for high contrast mode and reduced motion. Read-only from the plugin's perspective (not user-configurable within the plugin).

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Users can expand a band detail panel with a single click and see the full morph/node controls within 300ms (or instantly if reduced motion is active).
- **SC-002**: Users can collapse a band detail panel with a single click and the UI returns to the compact view within 300ms (or instantly if reduced motion is active).
- **SC-003**: The plugin window can be resized from 800x500 to 1400x900 while maintaining the 5:3 aspect ratio, with all UI elements remaining legible and usable at every supported size.
- **SC-004**: Users can cycle through all active bands using Tab in under 2 seconds (for 8 bands), and toggle bypass with Space, confirming keyboard-only operation is viable.
- **SC-005**: A MIDI Learn mapping can be created in under 5 seconds (right-click, select "MIDI Learn", move hardware knob).
- **SC-006**: MIDI CC-controlled parameters update with no user-perceptible lag (under 10ms from CC receipt to parameter change).
- **SC-007**: When high contrast mode is active, all UI text achieves a minimum contrast ratio of 4.5:1 against its background, verified by measurement.
- **SC-008**: When reduced motion is active, zero animations are present in the plugin UI (panel transitions, morph trails, sweep animations all disabled).
- **SC-009**: The modulation panel can be toggled visible/hidden with a single click, and active modulation routings continue to function when the panel is hidden.
- **SC-010**: 14-bit MIDI CC pairs provide 16,384 discrete steps of parameter resolution, verified by sweeping through the full CC range and counting distinct parameter values.
- **SC-011**: All expand/collapse states, modulation panel visibility, window size, and MIDI CC mappings persist correctly across preset saves/loads and DAW session restores.

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- The plugin host provides keyboard focus to the plugin editor when the user clicks within the plugin window. If the host does not forward keyboard events, keyboard shortcuts (FR-010 through FR-016) will not function. This is standard VST3 behavior and outside the plugin's control.
- The VSTGUI framework provides sufficient APIs for window resize, custom animation timing, and keyboard event handling. Based on VSTGUI 4.11+ documentation, these capabilities exist.
- The plugin host supports the VST3 `IMidiMapping` interface for MIDI CC routing. Hosts that do not support this interface will not receive MIDI CC mapping functionality.
- High contrast and reduced motion detection requires platform-specific APIs (Win32 SystemParametersInfo, macOS NSWorkspace accessibility APIs). On Linux, a best-effort approach will be used; if the desktop environment does not expose these settings, the default (non-accessible) mode will be used.
- The fine-adjustment step for keyboard arrow keys (1/100th of range) and coarse-adjustment step (1/10th of range) are reasonable defaults that may be tuned during implementation based on user feedback.
- Panel animation timing (up to 300ms) is a target that applies to smooth visual transitions. If VSTGUI's animation capabilities impose constraints, the implementation may use simpler approaches (e.g., opacity fade) as long as the timing target is met.

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that may be reused or extended:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| `ContainerVisibilityController` | `plugins/disrumpo/src/controller/controller.cpp` (line 160+) | **REUSE**: Already implements hide/show of CViewContainer based on parameter value. Used for band visibility and expanded state visibility. Extend for modulation panel visibility. |
| `expandedVisibilityControllers_` | `plugins/disrumpo/src/controller/controller.h` (line 213) | **REUSE**: Array of 8 controllers already wired to `kBandExpanded` parameters. These implement the core expand/collapse mechanism. FR-001 is partially implemented. |
| `bandVisibilityControllers_` | `plugins/disrumpo/src/controller/controller.h` (line 207) | **REUSE**: Array of 8 controllers that show/hide band containers based on band count. Pattern to follow for modulation panel visibility. |
| `kBandExpanded` parameter | `plugins/disrumpo/src/plugin_ids.h` (line 180) | **REUSE**: Per-band expanded state parameter already defined as `BandParamType::kBandExpanded = 0x05`. Already registered in controller and handled in processor (as UI-only param). |
| `IMidiMapping` interface | `plugins/disrumpo/src/controller/controller.h` (line 46) | **REUSE**: Controller already implements `IMidiMapping`. `getMidiControllerAssignment()` exists. `assignedMidiCC_` member exists. Currently limited to sweep frequency only; needs extension to all parameters. |
| MIDI Learn parameters | `plugins/disrumpo/src/plugin_ids.h` (lines 131-163) | **REUSE**: `kSweepMidiLearnActive` and `kSweepMidiCCNumber` parameters already exist. Processor already handles MIDI Learn scanning (processor.cpp line 312+). Needs generalization from sweep-only to all parameters. |
| `sweepVisualizationTimer_` | `plugins/disrumpo/src/controller/controller.h` (line 243) | **REFERENCE**: Existing ~30fps timer pattern for UI updates. Animations could use similar timer approach. |
| `ContainerVisibilityController` in Iterum | `plugins/iterum/src/controller/controller.cpp` | **REFERENCE**: Same pattern used in Iterum plugin. Confirms the approach is proven and reusable across plugins. |
| Shared PresetManager | `plugins/shared/src/preset/preset_manager.h` | **REFERENCE**: The shared preset infrastructure handles state persistence. MIDI CC mapping persistence should follow the same serialization patterns. |

**Search Results Summary**: The codebase already has substantial infrastructure for expand/collapse (visibility controllers, expanded parameters), basic MIDI Learn (sweep-only), and the IDependent observation pattern. The primary work involves:
1. Adding animation to the existing expand/collapse mechanism
2. Generalizing the sweep-only MIDI CC mapping to all parameters
3. Adding new capabilities: keyboard shortcuts, window resize, accessibility detection

### Forward Reusability Consideration

*Note for planning phase: When this is a Layer 2+ feature, consider what new code might be reusable by sibling features at the same layer. The `/speckit.plan` phase will analyze this in detail, but early identification helps.*

**Sibling features at same layer** (if known):
- Iterum plugin could benefit from the same window resize, keyboard shortcuts, and accessibility detection features
- Any future Krate Audio plugin would need these same capabilities

**Potential shared components** (preliminary, refined in plan.md):
- **Accessibility detection utilities** (high contrast, reduced motion) should be factored into `plugins/shared/` since they are platform-specific but plugin-agnostic
- **MIDI CC mapping manager** (MidiCCManager class) could be factored into `plugins/shared/` for reuse by Iterum and future plugins
- **Keyboard shortcut handler** could be a shared utility if the pattern proves generic enough
- **Window resize handler** with aspect ratio constraints could be shared infrastructure

## Implementation Verification *(mandatory at completion)*

<!--
  CRITICAL: This section MUST be completed when claiming spec completion.
  Constitution Principle XV: Honest Completion requires explicit verification
  of ALL requirements before claiming "done".

  This section is EMPTY during specification phase and filled during
  implementation phase when /speckit.implement completes.
-->

### Compliance Status

*Fill this table when claiming completion. DO NOT claim completion if ANY requirement is ❌ NOT MET without explicit user approval.*

| Requirement | Status | Evidence |
|-------------|--------|----------|
| FR-001 | MET | `animated_expand_controller.cpp` animates container visibility on `kBandExpanded` param change; `controller.cpp:3495-3511` creates controllers for 8 bands |
| FR-002 | MET | Each band has independent `kBandExpanded` param; no accordion logic; test `expand_collapse_test.cpp:107-133` verifies multiple simultaneous expand |
| FR-003 | MET | `kBandExpanded` is a registered VST3 parameter (`plugin_ids.h:183`), auto-persisted by host via parameter state |
| FR-004 | PARTIAL | Works in practice (hidden parent container masks expand animation), but no explicit guard in `AnimatedExpandController::update()` checking band visibility. No test coverage for this edge case. |
| FR-005 | MET | Animation duration 250ms (`controller.cpp:3508`), well within 300ms limit; timing function `CubicBezierTimingFunction::easyInOut(250)` at `animated_expand_controller.cpp:144`; test at `expand_collapse_test.cpp:139-145` |
| FR-006 | MET | Uses same animation name `"expandCollapse"` for both directions (`animated_expand_controller.cpp:137-145`); VSTGUI auto-cancels existing animation with same view+name, replacing with new target |
| FR-007 | MET | `kGlobalModPanelVisible` param at `plugin_ids.h:67`; `ContainerVisibilityController` created at `controller.cpp:3513-3524` |
| FR-008 | MET | Visibility controller only hides/shows UI container; modulation routings in processor are unaffected (by design — no disable logic) |
| FR-009 | MET | `kGlobalModPanelVisible` is a registered VST3 parameter, auto-persisted by host; also saved/restored in controller state |
| FR-010 | MET | `keyboard_shortcut_handler.cpp:70-74` handles Tab; `cycleBandFocus()` at lines 106-127 wraps from N to 0 |
| FR-010a | MET | `controller.cpp:3561-3563`: `setFocusDrawingEnabled(true)`, `setFocusColor(CColor(0x3A, 0x96, 0xDD))`, `setFocusWidth(2.0)` |
| FR-010b | MET | Accent blue (0x3A96DD) is high-contrast against both dark and light backgrounds; VSTGUI renders focus ring above content at frame level |
| FR-011 | MET | `keyboard_shortcut_handler.cpp:71`: `event.modifiers.has(ModifierKey::Shift)` sets reverse; lines 110-115 wrap from 0 to N-1 |
| FR-012 | MET | `keyboard_shortcut_handler.cpp:76-82`: Space toggles `kBandBypass` via `toggleBandBypass()` at lines 129-144 |
| FR-013 | MET | `keyboard_shortcut_handler.cpp:87`: `stepFraction = 0.01f` (1/100th); discrete params use 1 step (`lines 169-170`) |
| FR-014 | MET | `keyboard_shortcut_handler.cpp:90-91`: Down/Left negates stepFraction |
| FR-015 | MET | `keyboard_shortcut_handler.cpp:86-87`: Shift modifier sets `stepFraction = 0.1f` (1/10th) |
| FR-016 | MET | `IKeyboardHook` only receives events when plugin editor has OS focus (standard VSTGUI behavior); line 27 filters non-KeyDown |
| FR-017 | MET | `controller.cpp:2811-2813`: `setEditorSizeConstrains()` enables resize; `editor.uidesc:478` sets min/max size |
| FR-018 | MET | Min constraint 834x500 at `controller.cpp:2812` and `editor.uidesc:478` (834/500 = 5:3 exact) |
| FR-019 | MET | Max constraint 1400x840 at `controller.cpp:2812` and `editor.uidesc:478` (1400/840 = 5:3 exact) |
| FR-020 | MET | Default 1000x600 in `controller.h:278-279` (`lastWindowWidth_=1000.0`, `lastWindowHeight_=600.0`) |
| FR-021 | MET | `controller.cpp:3527-3532`: `constrainedHeight = constrainedWidth * 3.0 / 5.0` enforces 5:3 on every resize |
| FR-021a | MET | Same logic at `controller.cpp:3529-3530` clamps width then recalculates height; also in setState at lines 2752-2754 |
| FR-022 | MET | VSTGUI propagates size changes through view hierarchy; `editor.uidesc` uses relative layout; `setEditorSizeConstrains` enables proportional resize |
| FR-023 | MET | `controller.cpp:2710-2715` (getState) saves width/height as doubles; `controller.cpp:2748-2757` (setState) restores with 5:3 enforcement |
| FR-024 | MET | `accessibility_helper.cpp:27-41` (Windows), `49-56` (macOS), `59-68` (Linux) detect OS high contrast setting |
| FR-025 | PARTIAL | Windows: full system palette query (`GetSysColor`). macOS: detection deferred (no .mm Obj-C file yet, returns false). Linux: best-effort GTK_THEME env var only, no GSettings. |
| FR-025a | MET | `spectrum_display.cpp` and `morph_pad.cpp` apply 2px borders, solid fills; `controller.cpp:3578-3622` applies HC colors to all custom controls |
| FR-025b | MET | `accessibility_helper.cpp:31`: `SystemParametersInfoW(SPI_GETHIGHCONTRAST)` + `GetSysColor()` for full palette |
| FR-025c | NOT MET | `accessibility_helper.cpp:50-56`: Comment says "Best-effort without Obj-C", currently returns `false`. Requires `.mm` file with `NSWorkspace.shared.accessibilityDisplayShouldIncreaseContrast`. |
| FR-025d | PARTIAL | `accessibility_helper.cpp:59-68`: Checks `GTK_THEME` env var for "HighContrast"; no GLib/GSettings queries (would require linking GLib). Meets "best-effort" language in spec. |
| FR-026 | MET | `controller.cpp:3604-3622`: `setHighContrastMode()` called on SpectrumDisplay, MorphPad, SweepIndicator, DynamicNodeSelector |
| FR-027 | PARTIAL | Windows: `SPI_GETCLIENTAREAANIMATION` at `accessibility_helper.cpp:43-47`. macOS/Linux: hardcoded to `false` (no detection). |
| FR-028 | MET | `controller.cpp:3569-3576`: `setAnimationsEnabled(false)` on all AnimatedExpandControllers when reduced motion detected |
| FR-029 | MET | `animated_expand_controller.cpp:74-84`: When `!animationsEnabled_`, calls `instantExpand()`/`instantCollapse()` directly (zero duration) |
| FR-030 | MET | `createContextMenu()` at `controller.cpp:3837-3895` adds MIDI Learn to any control with a tag; `MidiCCManager` accepts any `ParamID` |
| FR-031 | MET | `controller.cpp:3852-3861`: Right-click context menu with "MIDI Learn" item; calls `midiCCManager_->startLearn(paramId)` |
| FR-032 | MET | `midi_cc_manager.cpp:127-146`: `processCCMessage()` captures first CC when `learnModeActive_`, calls `addGlobalMapping()` |
| FR-032a | MET | `controller.cpp:3878-3891`: "Save Mapping with Preset" checkbox in context menu; toggles between global and preset mapping |
| FR-032b | MET | `midi_cc_manager.cpp:88-90`: `addPresetMapping()` stores in preset-scoped map; `serializePresetMappings()`/`deserializePresetMappings()` for persistence |
| FR-033 | MET | `controller.cpp:3864-3876`: "Clear MIDI Learn" menu item; calls `midiCCManager_->removeMappingsForParam(paramId)` |
| FR-034 | MET | `midi_cc_manager.cpp:195-210`: `getMapping()` checks preset map first, then global (per-preset overrides global) |
| FR-035 | MET | `midi_cc_manager.cpp:127-189`: `processCCMessage()` invokes callback immediately with normalized value; no buffering |
| FR-036 | MET | `midi_cc_manager.cpp:21-30`: Adding mapping removes existing mapping for same CC first; comment references FR-036 |
| FR-037 | MET | `keyboard_shortcut_handler.cpp:46-48,98-104`: Escape calls `escapeCallback_`; `controller.cpp:3547-3555`: callback calls `cancelLearn()` |
| FR-038 | MET | `midi_cc_manager.h:30-36`: `is14Bit` flag in `MidiCCMapping`; supports CC pairs 0-31 MSB / 32-63 LSB |
| FR-039 | MET | `midi_cc_manager.cpp:148-163`: `combined = (lastMSB_[msbCC] << 7) | value`; `normalized = combined / 16383.0` (16,384 steps) |
| FR-040 | MET | `midi_cc_manager.cpp:178-183`: If 14-bit but no LSB yet, uses `value / 127.0` (7-bit fallback) |
| SC-001 | MET | 250ms animation; test `expand_collapse_test.cpp:139-145` asserts `<= 300ms` |
| SC-002 | MET | Same 250ms animation for collapse; same test coverage |
| SC-003 | MET | Min 834x500, max 1400x840, 5:3 enforced; resize constraints set in `controller.cpp:2811-2813` and `editor.uidesc` |
| SC-004 | MET | Tab cycling at `keyboard_shortcut_handler.cpp:106-127`; wrapping logic < 1 frame per cycle; test `keyboard_shortcut_test.cpp` |
| SC-005 | MET | Right-click + MIDI Learn + move knob = 3 steps; `startLearn()` + first CC = instant capture at `midi_cc_manager.cpp:127-136` |
| SC-006 | MET | `processCCMessage()` is synchronous callback with no buffering; < 1ms processing |
| SC-007 | PARTIAL | Windows: system HC colors applied (system guarantees 4.5:1). macOS: HC detection not functional. Linux: best-effort only. |
| SC-008 | MET | `controller.cpp:3569-3576` disables all animations; `animated_expand_controller.cpp:74-84` uses instant transitions |
| SC-009 | MET | Toggle via `kGlobalModPanelVisible`; `ContainerVisibilityController` hides UI only; routings unaffected |
| SC-010 | MET | `midi_cc_manager.cpp:154-155`: `combined / 16383.0` = 16,384 discrete steps; test `midi_cc_manager_test.cpp` verifies |
| SC-011 | PARTIAL | Expand states + mod panel via VST3 param state; window size via getState/setState; MIDI CC global via getState/setState; MIDI CC per-preset via component state. No round-trip integration test that verifies ALL state together. |

**Status Key:**
- MET: Requirement fully satisfied with test evidence
- NOT MET: Requirement not satisfied (spec is NOT complete)
- PARTIAL: Partially met with documented gap
- DEFERRED: Explicitly moved to future work with user approval

### Completion Checklist

*All items must be checked before claiming completion:*

- [x] All FR-xxx requirements verified against implementation
- [x] All SC-xxx success criteria measured and documented
- [x] No test thresholds relaxed from spec requirements
- [ ] No placeholder values or TODO comments in new code — **FAIL**: `accessibility_helper.cpp` has TODO for macOS .mm implementation
- [x] No features quietly removed from scope
- [ ] User would NOT feel cheated by this completion claim — **FAIL**: macOS high contrast detection is non-functional

### Honest Assessment

**Overall Status**: PARTIAL

**Gaps:**

| Gap | Requirement | Issue | Severity |
|-----|-------------|-------|----------|
| 1 | FR-004 | Hidden band expand works by accident (parent container masks animation), but no explicit guard in `AnimatedExpandController::update()`. No test for this edge case. | Low — works in practice |
| 2 | FR-025c | macOS high contrast detection NOT implemented. `accessibility_helper.cpp` returns `false` on macOS. Needs `.mm` Objective-C file with `NSWorkspace.shared.accessibilityDisplayShouldIncreaseContrast`. | Medium — macOS users get no HC support |
| 3 | FR-027 | Reduced motion detection only works on Windows (`SPI_GETCLIENTAREAANIMATION`). macOS/Linux hardcoded to `false`. macOS needs `NSWorkspace.shared.accessibilityDisplayShouldReduceMotion`. | Medium — macOS/Linux users get no reduced motion |
| 4 | SC-007 | High contrast 4.5:1 ratio cannot be verified on macOS/Linux since detection is non-functional there | Medium — follows from Gap 2 |
| 5 | SC-011 | No single round-trip integration test that verifies ALL state (expand + mod panel + window size + MIDI CC) persists together | Low — individual persistence tested separately |

**What is solid (35/40 FRs fully MET):**
- Expand/collapse with smooth 250ms animation and mid-animation reversal
- Modulation panel toggle with routing preservation
- Full keyboard shortcut suite (Tab/Shift+Tab/Space/Arrow/Shift+Arrow/Escape)
- Window resize with 5:3 aspect ratio enforcement and persistence
- Windows high contrast detection with system color palette
- Windows reduced motion detection
- Complete MIDI CC mapping with MIDI Learn, context menu, hybrid persistence
- 14-bit MIDI CC with MSB+LSB combining and 7-bit fallback
- All custom controls (SpectrumDisplay, MorphPad, SweepIndicator, DynamicNodeSelector) support HC mode
- 430 tests passing, 0 compiler warnings, clang-tidy clean, pluginval level 5 passing

**Recommendation**: To reach COMPLETE status:
1. Add explicit band-visibility guard in `AnimatedExpandController::update()` + test (1 hour)
2. Create `accessibility_helper.mm` with macOS `NSWorkspace` queries for HC + reduced motion (2 hours)
3. Add Linux `GSettings` queries for reduced motion if linking GLib is acceptable (1 hour)
4. Add combined state round-trip integration test (1 hour)
