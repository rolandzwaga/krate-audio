# Disrumpo Reference Documents Manifest

**Purpose:** This manifest lists ALL reference documents that MUST be consulted when creating implementation specs, plans, or tasks for Disrumpo.

**Location:** `specs/Disrumpo/`

---

## Required Reference Documents

When spawning ANY speckit agent (specify, plan, tasks, clarify, analyze) for Disrumpo, the agent MUST read ALL of the following documents:

| Document | Path | Contains |
|----------|------|----------|
| **specs-overview.md** | `specs/Disrumpo/specs-overview.md` | Functional requirements (FR-xxx), success criteria, 26 distortion types, parameter ranges |
| **plans-overview.md** | `specs/Disrumpo/plans-overview.md` | System architecture diagrams, signal flow, per-band processing, layer structure |
| **tasks-overview.md** | `specs/Disrumpo/tasks-overview.md` | Task breakdown summary, condensed task IDs (T2.1-T2.4 style) |
| **roadmap.md** | `specs/Disrumpo/roadmap.md` | Detailed task IDs (T2.1-T2.9 style), milestone criteria, dependencies, 17-week timeline |
| **dsp-details.md** | `specs/Disrumpo/dsp-details.md` | Parameter ID encoding, data structures, DSP algorithms, BandState, DistortionParams |
| **ui-mockups.md** | `specs/Disrumpo/ui-mockups.md` | UI layout, panel organization, control placement, visual hierarchy |
| **custom-controls.md** | `specs/Disrumpo/custom-controls.md` | Custom VSTGUI control specs: MorphPad, FrequencyBandDisplay, BandStrip |
| **vstgui-implementation.md** | `specs/Disrumpo/vstgui-implementation.md` | VSTGUI patterns, IDependent, visibility controllers, thread safety, editor.uidesc |

---

## Implementation Spec Directory Structure

Implementation specs are written to `specs/NNN-spec-name/` (NOT to `specs/Disrumpo/`):

```
specs/
├── Disrumpo/                    # Reference documents (READ ONLY)
│   ├── MANIFEST.md              # This file
│   ├── specs-overview.md
│   ├── plans-overview.md
│   ├── tasks-overview.md
│   ├── roadmap.md
│   ├── dsp-details.md
│   ├── ui-mockups.md
│   ├── custom-controls.md
│   └── vstgui-implementation.md
│
├── 001-plugin-skeleton/         # Implementation spec 001
│   ├── spec.md
│   ├── plan.md
│   └── tasks.md
│
├── 002-band-management/         # Implementation spec 002
│   ├── spec.md
│   ├── plan.md
│   └── tasks.md
│
├── 003-distortion-integration/  # Implementation spec 003
│   ├── spec.md
│   ├── plan.md
│   └── tasks.md
│
└── ...                          # Future specs: 004, 005, etc.
```

---

## Agent Instructions

When spawning a speckit agent for Disrumpo:

1. **FIRST** tell the agent to read this MANIFEST.md
2. **THEN** tell the agent to read ALL 8 documents listed above
3. **WRITE** output to `specs/NNN-spec-name/` (not to `specs/Disrumpo/`)

Example agent prompt prefix:
```
Read specs/Disrumpo/MANIFEST.md first, then read ALL 8 reference documents listed in it.
Write output to specs/003-distortion-integration/tasks.md
Do NOT write to specs/Disrumpo/ - it is for reference only.
```

---

## Document Relevance by Phase

| Phase | Primary Documents | Secondary Documents |
|-------|-------------------|---------------------|
| **specify** | specs-overview.md, dsp-details.md | plans-overview.md, roadmap.md |
| **plan** | plans-overview.md, dsp-details.md | roadmap.md, vstgui-implementation.md |
| **tasks** | tasks-overview.md, roadmap.md | plans-overview.md |
| **clarify** | All documents | - |
| **analyze** | All documents | - |

**Note:** Even "secondary" documents may contain critical information. When in doubt, read all 8.

---

## Version History

| Version | Date | Changes |
|---------|------|---------|
| 1.0 | 2026-01-28 | Initial manifest creation |
