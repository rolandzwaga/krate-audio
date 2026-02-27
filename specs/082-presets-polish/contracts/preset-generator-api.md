# Contract: Ruinae Preset Generator API

## Tool: `ruinae_preset_generator`

### Usage
```
ruinae_preset_generator [output_dir]
```

Default output directory: `plugins/ruinae/resources/presets`

### Output Structure
```
plugins/ruinae/resources/presets/
  Arp Classic/
    Basic_Up_1-16.vstpreset
    Down_1-8.vstpreset
    UpDown_1-8T.vstpreset
  Arp Acid/
    Acid_Line_303.vstpreset
    Acid_Stab.vstpreset
  Arp Euclidean/
    Tresillo_E3-8.vstpreset
    Bossa_E5-16.vstpreset
    Samba_E7-16.vstpreset
  Arp Polymetric/
    3x5x7_Evolving.vstpreset
    4x5_Shifting.vstpreset
  Arp Generative/
    Spice_Evolver.vstpreset
    Chaos_Garden.vstpreset
  Arp Performance/
    Fill_Cascade.vstpreset
    Probability_Waves.vstpreset
```

### Binary Format (.vstpreset)

```
Offset  Size  Content
0       4     "VST3" magic
4       4     Version (1, LE uint32)
8       32    Class ID ASCII hex (Ruinae processor FUID)
40      8     Chunk list offset (LE int64)
48      N     Component state data (binary blob)
48+N    4     "List" magic
48+N+4  4     Chunk count (1, LE uint32)
48+N+8  4     "Comp" chunk ID
48+N+12 8     Component data offset (48, LE int64)
48+N+20 8     Component data size (N, LE int64)
```

### Component State Data Format

Must match `Processor::getState()` byte-for-byte. Serialization order:
1. State version (int32 = 1)
2. Global params
3. Osc A params
4. Osc B params
5. Mixer params
6. Filter params
7. Distortion params
8. Trance Gate params
9. Amp Env params
10. Filter Env params
11. Mod Env params
12. LFO 1 params
13. LFO 2 params
14. Chaos Mod params
15. Mod Matrix params
16. Global Filter params
17. Delay params
18. Reverb params
19. Mono Mode params
20. Voice routes (16 x {i8, i8, f32, i8, f32, i8, i8, i8})
21. FX enable flags (2 x i8)
22. Phaser params + enable (i8)
23. LFO 1 Extended params
24. LFO 2 Extended params
25. Macro params
26. Rungler params
27. Settings params
28. Env Follower params
29. Sample Hold params
30. Random params
31. Pitch Follower params
32. Transient params
33. Harmonizer params + enable (i8)
34. Arpeggiator params

### Arp Params Serialization (matching `saveArpParams()`)

```
# Base params (11 values)
int32  enabled (0 or 1)
int32  mode (0-9)
int32  octaveRange (1-4)
int32  octaveMode (0-1)
int32  tempoSync (0 or 1)
int32  noteValue (0-20)
float  freeRate (0.5-50.0)
float  gateLength (1.0-200.0)
float  swing (0.0-75.0)
int32  latchMode (0-2)
int32  retrigger (0-2)

# Velocity lane (33 values)
int32  velocityLaneLength (1-32)
float  velocityLaneSteps[32] (0.0-1.0)

# Gate lane (33 values)
int32  gateLaneLength (1-32)
float  gateLaneSteps[32] (0.01-2.0)

# Pitch lane (33 values)
int32  pitchLaneLength (1-32)
int32  pitchLaneSteps[32] (-24 to +24)

# Modifier lane (35 values)
int32  modifierLaneLength (1-32)
int32  modifierLaneSteps[32] (0-255 bitmask)
int32  accentVelocity (0-127)
float  slideTime (0.0-500.0)

# Ratchet lane (33 values)
int32  ratchetLaneLength (1-32)
int32  ratchetLaneSteps[32] (1-4)

# Euclidean (4 values)
int32  euclideanEnabled (0 or 1)
int32  euclideanHits (0-32)
int32  euclideanSteps (2-32)
int32  euclideanRotation (0-31)

# Condition lane (34 values)
int32  conditionLaneLength (1-32)
int32  conditionLaneSteps[32] (0-17)
int32  fillToggle (0 or 1)

# Spice/Humanize (2 values)
float  spice (0.0-1.0)
float  humanize (0.0-1.0)

# Ratchet swing (1 value)
float  ratchetSwing (50.0-75.0)
```
