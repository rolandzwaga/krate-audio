# Disrumpo - UI Specifications

**Related Documents:**
- [specs-overview.md](specs-overview.md) - Core requirements specification
- [plans-overview.md](plans-overview.md) - System architecture and development roadmap
- [tasks-overview.md](tasks-overview.md) - Task breakdown and milestones
- [dsp-details.md](dsp-details.md) - DSP implementation details
- [custom-controls.md](custom-controls.md) - Custom VSTGUI control specifications

---

## 1. Window Dimensions

| Config | Width | Height |
|--------|-------|--------|
| Minimum | 800 | 500 |
| Default | 1000 | 600 |
| Maximum | 1400 | 900 |

---

## 2. Progressive Disclosure Levels

| Level | Name | Content |
|-------|------|---------|
| 1 | Essential | Spectrum, band types, basic controls |
| 2 | Standard | Morph controls, sweep, per-band detail |
| 3 | Expert | Modulation matrix, advanced params |

### Level 1: Essential View
```
┌────────────────────────────────────────────────────────────┐
│  Disrumpo                               [≡ Menu] [? Help]   │
├────────────────────────────────────────────────────────────┤
│  ┌──────────────────────────────────────────────────────┐ │
│  │              SPECTRUM DISPLAY                         │ │
│  │  ▓▓▓▓▓▓▓▓▓│▓▓▓▓▓▓▓▓▓▓▓▓│▓▓▓▓▓▓▓▓▓│▓▓▓▓▓▓▓▓▓▓▓▓▓▓   │ │
│  │   20Hz  200Hz      2kHz       8kHz        20kHz      │ │
│  │        [Band1]   [Band2]   [Band3]     [Band4]       │ │
│  └──────────────────────────────────────────────────────┘ │
│                                                            │
│  ┌─────────────┬─────────────┬─────────────┬─────────────┐│
│  │   BAND 1    │   BAND 2    │   BAND 3    │   BAND 4    ││
│  │  < 200 Hz   │ 200 - 2kHz  │  2k - 8kHz  │   > 8kHz    ││
│  ├─────────────┼─────────────┼─────────────┼─────────────┤│
│  │Type [Tube▼] │Type [Fuzz▼] │Type [Fold▼] │Type[Crush▼] ││
│  │Drive (◯)    │Drive (●)    │Drive (◯)    │Drive (◯)    ││
│  │Mix   (◯)    │Mix   (◯)    │Mix   (●)    │Mix   (◯)    ││
│  │[S] [B] [M]  │[S] [B] [M]  │[S] [B] [M]  │[S] [B] [M]  ││
│  └─────────────┴─────────────┴─────────────┴─────────────┘│
│                                                            │
│  ┌─────────────────────────┬──────────────────────────────┐│
│  │        GLOBAL           │           SWEEP              ││
│  │ Input(◯) Output(◯) Mix(●)│ [●]Enable Freq(◯) Width(◯) ││
│  └─────────────────────────┴──────────────────────────────┘│
│                                                            │
│  Preset: [Init            ▼]  [<] [>] [Save]  [▼ Expand]  │
└────────────────────────────────────────────────────────────┘
```

### Level 2: Standard View (Band Expanded)
```
┌─ BAND 2: 200Hz - 2kHz ───────────────────────────────────┐
│                                                           │
│  ┌─ MORPH SPACE ─────────────┐  ┌─ NODES ───────────────┐│
│  │       ● Fuzz              │  │ [+] Add Node          ││
│  │        ╲                  │  │                       ││
│  │         ╲                 │  │ ● Fuzz (A)            ││
│  │   ● ─────●                │  │   Type [Fuzz      ▼]  ││
│  │  Tube   Fold              │  │   Drive(●) Bias(◯)    ││
│  │         ○ ← cursor        │  │                       ││
│  │                           │  │ ● Tube (B)            ││
│  │  Mode: [2D Planar ▼]      │  │   Type [Tube      ▼]  ││
│  └───────────────────────────┘  │   Drive(◯) Bias(◯)    ││
│                                 │                       ││
│  X: [═══════●════] 0.72        │ ● Fold (C)            ││
│     Link: [Sweep Freq    ▼]    │   Type [Sine Fold ▼]  ││
│                                 │   Drive(◯) Folds(●)   ││
│  Y: [═══●════════] 0.31        └───────────────────────┘│
│     Link: [None          ▼]                              │
│                                                           │
│  OUTPUT: Gain(◯) Pan(◯) [S][B][M]                        │
└───────────────────────────────────────────────────────────┘
```

### Level 3: Modulation Panel
```
┌── MODULATION ────────────────────────────────────────────┐
│  ┌─ SOURCES ─────────────┐  ┌─ ROUTING MATRIX ─────────┐│
│  │ LFO 1                 │  │ Source    Dest      Amt  ││
│  │ ├─ Rate  (●) 0.5 Hz   │  │ LFO 1  → Sweep Freq [+70%]│
│  │ ├─ Shape [Sine    ▼]  │  │ LFO 2  → B2 Morph X [-30%]│
│  │ └─ Phase (◯) 0°       │  │ Env    → Glbl Drive [+50%]│
│  │                       │  │ Macro1 → B1 Morph Y [+80%]│
│  │ LFO 2                 │  │                          ││
│  │ ├─ Rate  (◯) 1/4      │  │ [+ Add Routing]          ││
│  │ ├─ Shape [Triangle▼]  │  └──────────────────────────┘│
│  │ └─ Sync  [Tempo   ▼]  │                              │
│  │                       │  ┌─ MACROS ─────────────────┐│
│  │ Envelope Follower     │  │ Macro1(◯) Macro2(◯)      ││
│  │ ├─ Attack  (◯) 10ms   │  │ Macro3(◯) Macro4(◯)      ││
│  │ └─ Release (●) 150ms  │  └──────────────────────────┘│
│  └───────────────────────┘                              │
└──────────────────────────────────────────────────────────┘
```

---

## 3. Color Scheme

| Element | Hex |
|---------|-----|
| Background Primary | #1A1A1E |
| Background Secondary | #252529 |
| Accent Primary | #FF6B35 |
| Accent Secondary | #4ECDC4 |
| Text Primary | #FFFFFF |
| Text Secondary | #8888AA |
| Band 1-4 | #FF6B35, #4ECDC4, #95E86B, #C792EA |
| Band 5-8 | #FFCB6B, #FF5370, #89DDFF, #F78C6C |

---

## 4. Interaction Specs

- **Knobs**: Vertical drag, Shift+drag for fine, double-click reset, Ctrl+click direct entry
- **Spectrum**: Drag dividers for crossovers, click band to select, scroll to zoom
- **Morph Pad**: Drag cursor, Alt+drag node to reposition, Shift for fine

---

## 5. Per-Distortion Type UI Specifications

Each distortion type has a dedicated parameter view. To maintain predictability, **shared parameters always appear in fixed positions**, while type-specific parameters fill dedicated zones.

### Standard Parameter Panel Layout

```
┌─────────────────────────────────────────────────────────────────┐
│  DISTORTION: [Type Selector Dropdown                        ▼]  │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  ┌─ COMMON ZONE (Fixed Position) ─────────────────────────────┐│
│  │                                                             ││
│  │   DRIVE          MIX            TONE                        ││
│  │    (●)           (●)            (●)                         ││
│  │   0-10          0-100%       200-8000Hz                     ││
│  │                                                             ││
│  └─────────────────────────────────────────────────────────────┘│
│                                                                 │
│  ┌─ TYPE-SPECIFIC ZONE (Changes per type) ────────────────────┐│
│  │                                                             ││
│  │   [Parameters specific to selected distortion type]         ││
│  │                                                             ││
│  │   Row 1: Primary parameters (up to 4 controls)              ││
│  │   Row 2: Secondary parameters (up to 4 controls)            ││
│  │   Row 3: Advanced/Mode selectors (if needed)                ││
│  │                                                             ││
│  └─────────────────────────────────────────────────────────────┘│
│                                                                 │
│  ┌─ OUTPUT ZONE (Fixed Position) ─────────────────────────────┐│
│  │   GAIN           PAN          [S] [B] [M]                   ││
│  │    (●)           (●)          Solo Bypass Mute              ││
│  └─────────────────────────────────────────────────────────────┘│
└─────────────────────────────────────────────────────────────────┘
```

### Shared Parameters (All Types)

| Parameter | Position | Control | Range | Default |
|-----------|----------|---------|-------|---------|
| Drive | Common Row, Slot 1 | Knob | 0.0 - 10.0 | 1.0 |
| Mix | Common Row, Slot 2 | Knob | 0% - 100% | 100% |
| Tone | Common Row, Slot 3 | Knob | 200 Hz - 8 kHz | 4 kHz |
| Gain | Output Row, Slot 1 | Knob | -24 dB - +24 dB | 0 dB |
| Pan | Output Row, Slot 2 | Knob | -100% L - +100% R | 0% (Center) |

---

## 6. SATURATION TYPES (D01-D06)

### D01: Soft Clip
**Category:** Saturation | **Character:** Warm, gentle compression

```
┌─ TYPE-SPECIFIC ────────────────────────────────────────────────┐
│                                                                 │
│   CURVE            KNEE                                         │
│    (●)             (●)                                          │
│  0.0-1.0         0.0-1.0                                        │
│  (harder→softer) (sharp→smooth)                                 │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

| Parameter | Control | Range | Default | Description |
|-----------|---------|-------|---------|-------------|
| Curve | Knob | 0.0 - 1.0 | 0.5 | Clipping curve shape (0=harder, 1=softer) |
| Knee | Knob | 0.0 - 1.0 | 0.3 | Transition smoothness into saturation |

---

### D02: Hard Clip
**Category:** Saturation | **Character:** Aggressive, digital edge

```
┌─ TYPE-SPECIFIC ────────────────────────────────────────────────┐
│                                                                 │
│   THRESHOLD        CEILING                                      │
│    (●)              (●)                                         │
│  -24dB-0dB       -6dB-0dB                                       │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

| Parameter | Control | Range | Default | Description |
|-----------|---------|-------|---------|-------------|
| Threshold | Knob | -24 dB - 0 dB | -6 dB | Level where clipping begins |
| Ceiling | Knob | -6 dB - 0 dB | 0 dB | Maximum output level |

---

### D03: Tube
**Category:** Saturation | **Character:** Warm, harmonic richness

```
┌─ TYPE-SPECIFIC ────────────────────────────────────────────────┐
│                                                                 │
│   BIAS             SAG             STAGE                        │
│    (●)             (●)            [1▼]                          │
│  -1.0-+1.0       0.0-1.0         1-4                            │
│  (even↔odd)     (compression)                                   │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

| Parameter | Control | Range | Default | Description |
|-----------|---------|-------|---------|-------------|
| Bias | Knob | -1.0 - +1.0 | 0.0 | Even/odd harmonic balance |
| Sag | Knob | 0.0 - 1.0 | 0.2 | Power supply sag simulation |
| Stage | Dropdown | 1 - 4 | 1 | Number of tube stages |

---

### D04: Tape
**Category:** Saturation | **Character:** Warm, compressed, vintage

```
┌─ TYPE-SPECIFIC ────────────────────────────────────────────────┐
│                                                                 │
│   BIAS             SAG            SPEED          MODEL          │
│    (●)             (●)             (●)          [Hysteresis▼]   │
│  -1.0-+1.0       0.0-1.0        7.5-30ips      Simple/Hyst     │
│                                                                 │
│   HF ROLL          FLUTTER                                      │
│    (●)              (●)                                         │
│  1kHz-20kHz       0.0-1.0                                       │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

| Parameter | Control | Range | Default | Description |
|-----------|---------|-------|---------|-------------|
| Bias | Knob | -1.0 - +1.0 | 0.0 | Tape bias adjustment |
| Sag | Knob | 0.0 - 1.0 | 0.3 | Compression behavior |
| Speed | Knob | 7.5 - 30 ips | 15 ips | Tape speed (affects HF response) |
| Model | Dropdown | Simple, Hysteresis | Hysteresis | Saturation model complexity |
| HF Roll | Knob | 1 kHz - 20 kHz | 12 kHz | High frequency rolloff point |
| Flutter | Knob | 0.0 - 1.0 | 0.0 | Tape flutter/wow amount |

---

### D05: Fuzz
**Category:** Saturation | **Character:** Aggressive, buzzy, vintage

```
┌─ TYPE-SPECIFIC ────────────────────────────────────────────────┐
│                                                                 │
│   BIAS             GATE           TRANSISTOR                    │
│    (●)             (●)           [Germanium▼]                   │
│  -1.0-+1.0       0.0-1.0        Ge/Si                          │
│                                                                 │
│   OCTAVE           SUSTAIN                                      │
│    (●)              (●)                                         │
│  0.0-1.0          0.0-1.0                                       │
│  (octave up)     (compression)                                  │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

| Parameter | Control | Range | Default | Description |
|-----------|---------|-------|---------|-------------|
| Bias | Knob | -1.0 - +1.0 | 0.0 | Transistor bias point |
| Gate | Knob | 0.0 - 1.0 | 0.0 | Noise gate threshold |
| Transistor | Dropdown | Germanium, Silicon | Germanium | Transistor type character |
| Octave | Knob | 0.0 - 1.0 | 0.0 | Octave-up effect blend |
| Sustain | Knob | 0.0 - 1.0 | 0.5 | Compression/sustain amount |

---

### D06: Asymmetric Fuzz
**Category:** Saturation | **Character:** Complex harmonics, uneven clipping

```
┌─ TYPE-SPECIFIC ────────────────────────────────────────────────┐
│                                                                 │
│   BIAS             ASYMMETRY      TRANSISTOR                    │
│    (●)              (●)          [Silicon▼]                     │
│  -1.0-+1.0        0.0-1.0        Ge/Si                         │
│  (DC offset)     (pos/neg ratio)                                │
│                                                                 │
│   GATE             SUSTAIN        BODY                          │
│    (●)              (●)           (●)                           │
│  0.0-1.0          0.0-1.0       0.0-1.0                         │
│                                 (low-mid bump)                  │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

| Parameter | Control | Range | Default | Description |
|-----------|---------|-------|---------|-------------|
| Bias | Knob | -1.0 - +1.0 | 0.2 | DC offset for asymmetry |
| Asymmetry | Knob | 0.0 - 1.0 | 0.6 | Positive/negative clipping ratio |
| Transistor | Dropdown | Germanium, Silicon | Silicon | Transistor type |
| Gate | Knob | 0.0 - 1.0 | 0.0 | Noise gate threshold |
| Sustain | Knob | 0.0 - 1.0 | 0.5 | Compression amount |
| Body | Knob | 0.0 - 1.0 | 0.3 | Low-mid frequency emphasis |

---

## 7. WAVEFOLD TYPES (D07-D09)

### D07: Sine Fold
**Category:** Wavefold | **Character:** Smooth, bell-like harmonics

```
┌─ TYPE-SPECIFIC ────────────────────────────────────────────────┐
│                                                                 │
│   FOLDS            SYMMETRY       SHAPE                         │
│    (●)              (●)           (●)                           │
│   1-8             0.0-1.0       0.0-1.0                         │
│  (fold count)    (balanced)    (sine→tri)                       │
│                                                                 │
│   BIAS             SMOOTH                                       │
│    (●)              (●)                                         │
│  -1.0-+1.0        0.0-1.0                                       │
│                  (AA filter)                                    │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

| Parameter | Control | Range | Default | Description |
|-----------|---------|-------|---------|-------------|
| Folds | Knob | 1 - 8 | 2 | Number of wavefolding stages |
| Symmetry | Knob | 0.0 - 1.0 | 0.5 | Folding symmetry |
| Shape | Knob | 0.0 - 1.0 | 0.0 | Fold shape (sine to triangle) |
| Bias | Knob | -1.0 - +1.0 | 0.0 | DC offset before folding |
| Smooth | Knob | 0.0 - 1.0 | 0.3 | Anti-aliasing filter amount |

---

### D08: Triangle Fold
**Category:** Wavefold | **Character:** Bright, edgy harmonics

```
┌─ TYPE-SPECIFIC ────────────────────────────────────────────────┐
│                                                                 │
│   FOLDS            SYMMETRY       ANGLE                         │
│    (●)              (●)           (●)                           │
│   1-8             0.0-1.0       0.0-1.0                         │
│                                (sharp→soft)                     │
│                                                                 │
│   BIAS             SMOOTH                                       │
│    (●)              (●)                                         │
│  -1.0-+1.0        0.0-1.0                                       │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

| Parameter | Control | Range | Default | Description |
|-----------|---------|-------|---------|-------------|
| Folds | Knob | 1 - 8 | 2 | Number of wavefolding stages |
| Symmetry | Knob | 0.0 - 1.0 | 0.5 | Folding symmetry |
| Angle | Knob | 0.0 - 1.0 | 0.5 | Triangle angle (sharp to soft) |
| Bias | Knob | -1.0 - +1.0 | 0.0 | DC offset before folding |
| Smooth | Knob | 0.0 - 1.0 | 0.3 | Anti-aliasing filter amount |

---

### D09: Serge Fold
**Category:** Wavefold | **Character:** Complex, musical, West Coast

```
┌─ TYPE-SPECIFIC ────────────────────────────────────────────────┐
│                                                                 │
│   FOLDS            SYMMETRY       MODEL                         │
│    (●)              (●)          [Lockhart▼]                    │
│   1-8             0.0-1.0       Lockhart/Buchla                 │
│                                                                 │
│   BIAS             SHAPE          SMOOTH                        │
│    (●)              (●)           (●)                           │
│  -1.0-+1.0        0.0-1.0       0.0-1.0                         │
│                  (character)                                    │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

| Parameter | Control | Range | Default | Description |
|-----------|---------|-------|---------|-------------|
| Folds | Knob | 1 - 8 | 3 | Number of wavefolding stages |
| Symmetry | Knob | 0.0 - 1.0 | 0.5 | Folding symmetry |
| Model | Dropdown | Lockhart, Buchla259 | Lockhart | Wavefolder circuit model |
| Bias | Knob | -1.0 - +1.0 | 0.0 | DC offset before folding |
| Shape | Knob | 0.0 - 1.0 | 0.5 | Fold shape character |
| Smooth | Knob | 0.0 - 1.0 | 0.3 | Anti-aliasing amount |

---

## 8. RECTIFY TYPES (D10-D11)

### D10: Full Rectify
**Category:** Rectify | **Character:** Octave up, bright, aggressive

```
┌─ TYPE-SPECIFIC ────────────────────────────────────────────────┐
│                                                                 │
│   SMOOTH           DC BLOCK                                     │
│    (●)              [✓]                                         │
│  0.0-1.0          On/Off                                        │
│  (AA filter)                                                    │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

| Parameter | Control | Range | Default | Description |
|-----------|---------|-------|---------|-------------|
| Smooth | Knob | 0.0 - 1.0 | 0.2 | Smoothing/anti-aliasing |
| DC Block | Toggle | On/Off | On | Remove DC offset from output |

---

### D11: Half Rectify
**Category:** Rectify | **Character:** Gated, sputtery, aggressive

```
┌─ TYPE-SPECIFIC ────────────────────────────────────────────────┐
│                                                                 │
│   THRESHOLD        SMOOTH         DC BLOCK                      │
│    (●)              (●)            [✓]                          │
│  -1.0-0.0         0.0-1.0        On/Off                         │
│  (cut point)                                                    │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

| Parameter | Control | Range | Default | Description |
|-----------|---------|-------|---------|-------------|
| Threshold | Knob | -1.0 - 0.0 | 0.0 | Rectification threshold point |
| Smooth | Knob | 0.0 - 1.0 | 0.2 | Smoothing/anti-aliasing |
| DC Block | Toggle | On/Off | On | Remove DC offset from output |

---

## 9. DIGITAL TYPES (D12-D14, D18-D19)

### D12: Bitcrush
**Category:** Digital | **Character:** Retro, gritty, lo-fi

```
┌─ TYPE-SPECIFIC ────────────────────────────────────────────────┐
│                                                                 │
│   BIT DEPTH        DITHER         MODE                          │
│    (●)              (●)          [Truncate▼]                    │
│   1-16            0.0-1.0       Truncate/Round/Noise            │
│                                                                 │
│   JITTER                                                        │
│    (●)                                                          │
│  0.0-1.0                                                        │
│  (bit instability)                                              │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

| Parameter | Control | Range | Default | Description |
|-----------|---------|-------|---------|-------------|
| Bit Depth | Knob | 1.0 - 16.0 | 8.0 | Output bit depth (continuous) |
| Dither | Knob | 0.0 - 1.0 | 0.0 | Dither noise amount |
| Mode | Dropdown | Truncate, Round, Noise | Truncate | Quantization method |
| Jitter | Knob | 0.0 - 1.0 | 0.0 | Bit depth instability |

---

### D13: Sample Reduce
**Category:** Digital | **Character:** Aliased, crunchy, retro

```
┌─ TYPE-SPECIFIC ────────────────────────────────────────────────┐
│                                                                 │
│   RATE RATIO       JITTER         MODE                          │
│    (●)              (●)          [Hold▼]                        │
│   1-32            0.0-1.0       Hold/Interpolate                │
│  (÷ factor)      (rate wobble)                                  │
│                                                                 │
│   SMOOTH                                                        │
│    (●)                                                          │
│  0.0-1.0                                                        │
│  (output filter)                                                │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

| Parameter | Control | Range | Default | Description |
|-----------|---------|-------|---------|-------------|
| Rate Ratio | Knob | 1.0 - 32.0 | 4.0 | Sample rate division factor |
| Jitter | Knob | 0.0 - 1.0 | 0.0 | Sample timing instability |
| Mode | Dropdown | Hold, Interpolate | Hold | Sample reconstruction mode |
| Smooth | Knob | 0.0 - 1.0 | 0.0 | Output lowpass filter |

---

### D14: Quantize
**Category:** Digital | **Character:** Stepped, robotic, glitchy

```
┌─ TYPE-SPECIFIC ────────────────────────────────────────────────┐
│                                                                 │
│   LEVELS           DITHER         SMOOTH                        │
│    (●)              (●)           (●)                           │
│   2-256           0.0-1.0       0.0-1.0                         │
│  (step count)                   (slew limit)                    │
│                                                                 │
│   OFFSET                                                        │
│    (●)                                                          │
│  -0.5-+0.5                                                      │
│  (level shift)                                                  │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

| Parameter | Control | Range | Default | Description |
|-----------|---------|-------|---------|-------------|
| Levels | Knob | 2 - 256 | 16 | Number of quantization levels |
| Dither | Knob | 0.0 - 1.0 | 0.0 | Dither noise before quantization |
| Smooth | Knob | 0.0 - 1.0 | 0.0 | Output slew rate limiting |
| Offset | Knob | -0.5 - +0.5 | 0.0 | Quantization grid offset |

---

### D18: Aliasing
**Category:** Digital | **Character:** Metallic, harsh, inharmonic

```
┌─ TYPE-SPECIFIC ────────────────────────────────────────────────┐
│                                                                 │
│   DOWNSAMPLE       FREQ SHIFT     PRE-FILTER                    │
│    (●)              (●)            [✓]                          │
│   2-32          -1000-+1000Hz    On/Off                         │
│  (÷ factor)     (before DS)     (minimal AA)                    │
│                                                                 │
│   FEEDBACK         RESONANCE                                    │
│    (●)              (●)                                         │
│  0.0-0.9          0.0-1.0                                       │
│  (recirculate)   (at nyquist)                                   │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

| Parameter | Control | Range | Default | Description |
|-----------|---------|-------|---------|-------------|
| Downsample | Knob | 2 - 32 | 4 | Downsample factor (no AA) |
| Freq Shift | Knob | -1000 - +1000 Hz | 0 Hz | Frequency shift before downsample |
| Pre-Filter | Toggle | On/Off | Off | Minimal anti-alias filter |
| Feedback | Knob | 0.0 - 0.9 | 0.0 | Aliased signal feedback |
| Resonance | Knob | 0.0 - 1.0 | 0.0 | Resonance at folded frequencies |

---

### D19: Bitwise Mangler
**Category:** Digital | **Character:** Extreme, glitchy, unpredictable

```
┌─ TYPE-SPECIFIC ────────────────────────────────────────────────┐
│                                                                 │
│   OPERATION        INTENSITY      PATTERN                       │
│  [XorPattern▼]      (●)           (●)                           │
│  XOR/Rotate/       0.0-1.0      0x0000-0xFFFF                   │
│  Shuffle/Wrap                   (hex display)                   │
│                                                                 │
│   BIT RANGE        SMOOTH                                       │
│   [●━━━━━━●]        (●)                                         │
│   Low    High     0.0-1.0                                       │
│   (affected bits) (output filter)                               │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

| Parameter | Control | Range | Default | Description |
|-----------|---------|-------|---------|-------------|
| Operation | Dropdown | XorPattern, XorPrevious, BitRotate, OverflowWrap | XorPattern | Bitwise operation type |
| Intensity | Knob | 0.0 - 1.0 | 0.5 | Effect intensity/blend |
| Pattern | Knob | 0x0000 - 0xFFFF | 0xAAAA | XOR pattern (hex display) |
| Bit Range | Range Slider | 0-15, 0-15 | 0-15 | Affected bit range |
| Smooth | Knob | 0.0 - 1.0 | 0.1 | Output lowpass filter |

---

## 10. DYNAMIC TYPE (D15)

### D15: Temporal
**Category:** Dynamic | **Character:** Breathing, responsive, expressive

```
┌─ TYPE-SPECIFIC ────────────────────────────────────────────────┐
│                                                                 │
│   MODE             SENSITIVITY    CURVE                         │
│  [EnvFollow▼]       (●)          [Soft▼]                        │
│  Follow/Inverse/   0.0-1.0      Soft/Hard/Exp                   │
│  Derivative/Hyst                                                │
│                                                                 │
│   ATTACK           RELEASE        DEPTH                         │
│    (●)              (●)           (●)                           │
│  1-100ms         10-500ms       0.0-1.0                         │
│                                (drive mod)                      │
│                                                                 │
│   LOOKAHEAD        HOLD                                         │
│    (●)              (●)                                         │
│  0-10ms          0-100ms                                        │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

| Parameter | Control | Range | Default | Description |
|-----------|---------|-------|---------|-------------|
| Mode | Dropdown | EnvelopeFollow, InverseEnvelope, Derivative, Hysteresis | EnvelopeFollow | Detection mode |
| Sensitivity | Knob | 0.0 - 1.0 | 0.5 | Input level sensitivity |
| Curve | Dropdown | Soft, Hard, Exponential | Soft | Saturation curve type |
| Attack | Knob | 1 - 100 ms | 10 ms | Envelope attack time |
| Release | Knob | 10 - 500 ms | 100 ms | Envelope release time |
| Depth | Knob | 0.0 - 1.0 | 0.5 | Drive modulation depth |
| Lookahead | Knob | 0 - 10 ms | 0 ms | Detection lookahead |
| Hold | Knob | 0 - 100 ms | 0 ms | Envelope hold time |

---

## 11. HYBRID TYPES (D16-D17, D26)

### D16: Ring Saturation
**Category:** Hybrid | **Character:** Metallic, bell-like, inharmonic

```
┌─ TYPE-SPECIFIC ────────────────────────────────────────────────┐
│                                                                 │
│   MOD DEPTH        STAGES         CURVE                         │
│    (●)             [2▼]          [Tanh▼]                        │
│  0.0-1.0          1-4           Tanh/Hard/Fold                  │
│  (ring amount)                                                  │
│                                                                 │
│   CARRIER          BIAS                                         │
│  [Self▼]           (●)                                          │
│  Self/Fixed/      -1.0-+1.0                                     │
│  Envelope                                                       │
│                                                                 │
│   CARRIER FREQ     (if Fixed)                                   │
│    (●)                                                          │
│  20-2000Hz                                                      │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

| Parameter | Control | Range | Default | Description |
|-----------|---------|-------|---------|-------------|
| Mod Depth | Knob | 0.0 - 1.0 | 0.5 | Ring modulation depth |
| Stages | Dropdown | 1 - 4 | 2 | Saturation stages |
| Curve | Dropdown | Tanh, Hard, Fold | Tanh | Saturation curve type |
| Carrier | Dropdown | Self, Fixed, Envelope | Self | Modulation carrier source |
| Bias | Knob | -1.0 - +1.0 | 0.0 | DC bias for asymmetry |
| Carrier Freq | Knob | 20 - 2000 Hz | 440 Hz | Fixed carrier frequency |

---

### D17: Feedback Distortion
**Category:** Hybrid | **Character:** Singing, sustaining, screaming

```
┌─ TYPE-SPECIFIC ────────────────────────────────────────────────┐
│                                                                 │
│   FEEDBACK         DELAY          CURVE                         │
│    (●)              (●)          [Tanh▼]                        │
│  0.0-1.5         1-100ms        Tanh/Hard/Tube                  │
│  (⚠️ >1 = unstable)                                             │
│                                                                 │
│   FILTER           FILTER FREQ    STAGES                        │
│  [Lowpass▼]         (●)          [1▼]                           │
│  LP/HP/BP/None   200-8000Hz      1-4                            │
│                                                                 │
│   LIMITER          LIMIT THRESH                                 │
│    [✓]              (●)                                         │
│  On/Off          -12dB-0dB                                      │
│  (safety)                                                       │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

| Parameter | Control | Range | Default | Description |
|-----------|---------|-------|---------|-------------|
| Feedback | Knob | 0.0 - 1.5 | 0.5 | Feedback amount (>1 = self-oscillation) |
| Delay | Knob | 1 - 100 ms | 10 ms | Feedback delay time |
| Curve | Dropdown | Tanh, Hard, Tube | Tanh | Saturation in feedback loop |
| Filter | Dropdown | Lowpass, Highpass, Bandpass, None | Lowpass | Filter type in loop |
| Filter Freq | Knob | 200 - 8000 Hz | 2000 Hz | Filter cutoff frequency |
| Stages | Dropdown | 1 - 4 | 1 | Saturation stages per loop |
| Limiter | Toggle | On/Off | On | Safety limiter enable |
| Limit Thresh | Knob | -12 dB - 0 dB | -3 dB | Limiter threshold |

---

### D26: Allpass Resonant
**Category:** Hybrid | **Character:** Pitched, resonant, plucky

```
┌─ TYPE-SPECIFIC ────────────────────────────────────────────────┐
│                                                                 │
│   TOPOLOGY         FREQUENCY      FEEDBACK                      │
│  [SingleAP▼]        (●)           (●)                           │
│  Single/Chain/   20-2000Hz      0.0-0.99                        │
│  Karplus/Matrix                                                 │
│                                                                 │
│   DECAY            CURVE          STAGES                        │
│    (●)            [Tanh▼]        [2▼]                           │
│  0.01-10s        Tanh/Hard/      1-8                            │
│                  Fold            (if Chain)                     │
│                                                                 │
│   PITCH TRACK      DAMPING                                      │
│    [✓]              (●)                                         │
│  On/Off          0.0-1.0                                        │
│  (follow input)  (HF loss)                                      │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

| Parameter | Control | Range | Default | Description |
|-----------|---------|-------|---------|-------------|
| Topology | Dropdown | SingleAllpass, AllpassChain, KarplusStrong, FeedbackMatrix | SingleAllpass | Resonator topology |
| Frequency | Knob | 20 - 2000 Hz | 440 Hz | Resonant frequency |
| Feedback | Knob | 0.0 - 0.99 | 0.7 | Allpass feedback amount |
| Decay | Knob | 0.01 - 10 s | 1 s | Resonance decay time |
| Curve | Dropdown | Tanh, Hard, Fold | Tanh | Saturation curve type |
| Stages | Dropdown | 1 - 8 | 2 | Allpass stages (Chain mode) |
| Pitch Track | Toggle | On/Off | Off | Track input pitch |
| Damping | Knob | 0.0 - 1.0 | 0.3 | High frequency damping |

---

## 12. EXPERIMENTAL TYPES (D20-D25)

### D20: Chaos
**Category:** Experimental | **Character:** Evolving, organic, unpredictable

```
┌─ TYPE-SPECIFIC ────────────────────────────────────────────────┐
│                                                                 │
│   ATTRACTOR        SPEED          AMOUNT                        │
│  [Lorenz▼]          (●)           (●)                           │
│  Lorenz/Rossler/  0.1-10.0      0.0-1.0                         │
│  Chua/Henon       (evolution)   (chaos blend)                   │
│                                                                 │
│   COUPLING         X-DRIVE        Y-DRIVE                       │
│    (●)              (●)           (●)                           │
│  0.0-1.0          0.0-1.0       0.0-1.0                         │
│  (input→attractor) (X axis)    (Y axis)                         │
│                                                                 │
│   SMOOTH           SEED                                         │
│    (●)            [Reseed]                                      │
│  0.0-1.0          Button                                        │
│  (interpolation)                                                │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

| Parameter | Control | Range | Default | Description |
|-----------|---------|-------|---------|-------------|
| Attractor | Dropdown | Lorenz, Rossler, Chua, Henon | Lorenz | Chaotic attractor model |
| Speed | Knob | 0.1 - 10.0 | 1.0 | Attractor evolution speed |
| Amount | Knob | 0.0 - 1.0 | 0.5 | Chaos effect blend |
| Coupling | Knob | 0.0 - 1.0 | 0.3 | Input signal → attractor coupling |
| X-Drive | Knob | 0.0 - 1.0 | 0.5 | X-axis drive influence |
| Y-Drive | Knob | 0.0 - 1.0 | 0.5 | Y-axis drive influence |
| Smooth | Knob | 0.0 - 1.0 | 0.2 | Output interpolation smoothing |
| Seed | Button | — | — | Randomize attractor initial state |

---

### D21: Formant
**Category:** Experimental | **Character:** Vocal, talking, alien

```
┌─ TYPE-SPECIFIC ────────────────────────────────────────────────┐
│                                                                 │
│   VOWEL            SHIFT          CURVE                         │
│  [A-E Morph]        (●)          [Soft▼]                        │
│   (●)           -12-+12 st      Soft/Hard/Fold                  │
│  A-E-I-O-U      (formant shift)                                 │
│                                                                 │
│   RESONANCE        BANDWIDTH      FORMANTS                      │
│    (●)              (●)          [3▼]                           │
│  0.0-1.0          0.5-2.0        2-5                            │
│  (peak height)   (Q width)      (formant count)                 │
│                                                                 │
│   GENDER           BLEND                                        │
│    (●)              (●)                                         │
│  -1.0-+1.0        0.0-1.0                                       │
│  (masc↔fem)      (formant↔dry)                                  │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

| Parameter | Control | Range | Default | Description |
|-----------|---------|-------|---------|-------------|
| Vowel | Knob | A-E-I-O-U (0-1) | 0.0 (A) | Vowel formant selection |
| Shift | Knob | -12 - +12 st | 0 st | Formant frequency shift |
| Curve | Dropdown | Soft, Hard, Fold | Soft | Distortion curve type |
| Resonance | Knob | 0.0 - 1.0 | 0.5 | Formant peak resonance |
| Bandwidth | Knob | 0.5 - 2.0 | 1.0 | Formant bandwidth multiplier |
| Formants | Dropdown | 2 - 5 | 3 | Number of formant peaks |
| Gender | Knob | -1.0 - +1.0 | 0.0 | Formant spacing (masculine↔feminine) |
| Blend | Knob | 0.0 - 1.0 | 0.7 | Formant processing blend |

---

### D22: Granular
**Category:** Experimental | **Character:** Textured, pointillist, fragmented

```
┌─ TYPE-SPECIFIC ────────────────────────────────────────────────┐
│                                                                 │
│   GRAIN SIZE       DENSITY        PITCH VAR                     │
│    (●)              (●)           (●)                           │
│  5-100ms         1-50 g/s       -12-+12 st                      │
│                  (grains/sec)   (random range)                  │
│                                                                 │
│   DRIVE VAR        POSITION       CURVE                         │
│    (●)              (●)          [Tanh▼]                        │
│  0.0-1.0          0.0-1.0       Tanh/Hard/Fold                  │
│  (per-grain)     (buffer pos)                                   │
│                                                                 │
│   ENVELOPE         SPREAD         FREEZE                        │
│  [Hanning▼]         (●)           [✓]                           │
│  Hann/Tri/        0.0-1.0       On/Off                          │
│  Rect/Tukey      (stereo)                                       │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

| Parameter | Control | Range | Default | Description |
|-----------|---------|-------|---------|-------------|
| Grain Size | Knob | 5 - 100 ms | 50 ms | Individual grain duration |
| Density | Knob | 1 - 50 g/s | 10 g/s | Grains per second |
| Pitch Var | Knob | -12 - +12 st | 0 st | Random pitch variation range |
| Drive Var | Knob | 0.0 - 1.0 | 0.3 | Per-grain drive randomization |
| Position | Knob | 0.0 - 1.0 | 0.5 | Grain position in buffer |
| Curve | Dropdown | Tanh, Hard, Fold | Tanh | Per-grain distortion curve |
| Envelope | Dropdown | Hanning, Triangle, Rectangle, Tukey | Hanning | Grain envelope shape |
| Spread | Knob | 0.0 - 1.0 | 0.3 | Stereo spread of grains |
| Freeze | Toggle | On/Off | Off | Freeze buffer (loop grains) |

---

### D23: Spectral
**Category:** Experimental | **Character:** Surgical, impossible, crystalline

```
┌─ TYPE-SPECIFIC ────────────────────────────────────────────────┐
│                                                                 │
│   MODE             FFT SIZE       CURVE                         │
│  [PerBin▼]        [2048▼]        [Soft▼]                        │
│  PerBin/Mag/     512-4096       Soft/Hard/Fold                  │
│  Selective/Crush                                                │
│                                                                 │
│   TILT             THRESH         MAG BITS                      │
│    (●)              (●)           (●)                           │
│  -1.0-+1.0      -60dB-0dB        1-16                           │
│  (low↔high)     (gate)          (for Crush)                     │
│                                                                 │
│   FREQ RANGE       PHASE MODE                                   │
│   [●━━━━━━●]      [Preserve▼]                                   │
│   Low    High    Preserve/Zero/                                 │
│   (affected Hz)  Random                                         │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

| Parameter | Control | Range | Default | Description |
|-----------|---------|-------|---------|-------------|
| Mode | Dropdown | PerBinSaturate, MagnitudeOnly, BinSelective, SpectralBitcrush | PerBinSaturate | Processing mode |
| FFT Size | Dropdown | 512, 1024, 2048, 4096 | 2048 | FFT analysis size |
| Curve | Dropdown | Soft, Hard, Fold | Soft | Magnitude saturation curve |
| Tilt | Knob | -1.0 - +1.0 | 0.0 | Frequency-dependent drive (low↔high) |
| Thresh | Knob | -60 dB - 0 dB | -40 dB | Bin gate threshold |
| Mag Bits | Knob | 1 - 16 | 16 | Magnitude bit depth (Crush mode) |
| Freq Range | Range Slider | 20-20000 Hz | 20-20000 Hz | Affected frequency range |
| Phase Mode | Dropdown | Preserve, Zero, Random | Preserve | Phase handling |

---

### D24: Fractal
**Category:** Experimental | **Character:** Self-similar, deep, evolving

```
┌─ TYPE-SPECIFIC ────────────────────────────────────────────────┐
│                                                                 │
│   MODE             ITERATIONS     SCALE                         │
│  [Residual▼]       [4▼]           (●)                           │
│  Residual/Multi/  1-8           0.3-0.9                         │
│  Harmonic/Cascade               (per-level)                     │
│                                                                 │
│   CURVE            FREQ DECAY     FEEDBACK                      │
│  [Tanh▼]            (●)           (●)                           │
│  Tanh/Hard/       0.0-1.0       0.0-0.9                         │
│  Fold/Mix        (HF reduction) (cross-feed)                    │
│                                                                 │
│   BLEND            DEPTH                                        │
│    (●)              (●)                                         │
│  0.0-1.0          0.0-1.0                                       │
│  (fractal mix)   (iteration depth)                              │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

| Parameter | Control | Range | Default | Description |
|-----------|---------|-------|---------|-------------|
| Mode | Dropdown | Residual, Multiband, Harmonic, Cascade, Feedback | Residual | Fractal processing mode |
| Iterations | Dropdown | 1 - 8 | 4 | Recursion depth |
| Scale | Knob | 0.3 - 0.9 | 0.5 | Per-level amplitude scaling |
| Curve | Dropdown | Tanh, Hard, Fold, Mix | Tanh | Distortion curve per level |
| Freq Decay | Knob | 0.0 - 1.0 | 0.5 | High frequency reduction per level |
| Feedback | Knob | 0.0 - 0.9 | 0.0 | Cross-level feedback |
| Blend | Knob | 0.0 - 1.0 | 0.7 | Fractal/dry mix |
| Depth | Knob | 0.0 - 1.0 | 0.5 | Effective iteration depth |

---

### D25: Stochastic
**Category:** Experimental | **Character:** Analog-like, drifting, organic

```
┌─ TYPE-SPECIFIC ────────────────────────────────────────────────┐
│                                                                 │
│   BASE CURVE       JITTER         JITTER RATE                   │
│  [Tanh▼]            (●)           (●)                           │
│  Tanh/Hard/       0.0-1.0       0.1-100Hz                       │
│  Tube/Fold       (randomness)   (variation speed)               │
│                                                                 │
│   COEFF NOISE      DRIFT          SEED                          │
│    (●)              (●)          [Reseed]                       │
│  0.0-1.0          0.0-1.0        Button                         │
│  (curve noise)   (slow wander)                                  │
│                                                                 │
│   CORRELATION      SMOOTH                                       │
│    (●)              (●)                                         │
│  0.0-1.0          0.0-1.0                                       │
│  (sample link)   (output filter)                                │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

| Parameter | Control | Range | Default | Description |
|-----------|---------|-------|---------|-------------|
| Base Curve | Dropdown | Tanh, Hard, Tube, Fold | Tanh | Base distortion curve |
| Jitter | Knob | 0.0 - 1.0 | 0.2 | Curve randomization amount |
| Jitter Rate | Knob | 0.1 - 100 Hz | 10 Hz | Jitter variation rate |
| Coeff Noise | Knob | 0.0 - 1.0 | 0.1 | Transfer function coefficient noise |
| Drift | Knob | 0.0 - 1.0 | 0.1 | Slow parameter wandering |
| Seed | Button | — | — | Randomize RNG state |
| Correlation | Knob | 0.0 - 1.0 | 0.5 | Sample-to-sample correlation |
| Smooth | Knob | 0.0 - 1.0 | 0.1 | Output smoothing filter |

---

## 13. UI Behavior During Morphing

When the morph position blends between distortion types:

1. **Same-Family Morphs** (e.g., D01↔D03, both Saturation):
   - Type-specific zone shows interpolated parameters
   - Controls that exist in both types remain visible
   - Controls unique to one type fade in/out with morph weight

2. **Cross-Family Morphs** (e.g., D03 Tube ↔ D07 Sine Fold):
   - Type-specific zone splits into two sub-panels
   - Each panel shows its type's controls at reduced size
   - Panel opacity matches morph weight
   - When weight < 10%, panel collapses entirely

3. **Multi-Node Morphs** (3-4 nodes active):
   - Dominant type (highest weight) shows full controls
   - Secondary types show as collapsed headers
   - Click header to expand temporarily
   - "Show All" toggle reveals all active type panels

---

## 14. Control State Persistence

- Parameter values persist when switching distortion types
- Returning to a previously-used type restores its last values
- "Reset to Default" button available per type-specific zone
- Per-node parameter memory stored in preset
