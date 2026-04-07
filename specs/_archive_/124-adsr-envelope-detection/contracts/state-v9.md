# State Serialization v9 Contract

## Write Order (appended after v8 data)

```
// --- ADSR Envelope (v9) ---
streamer.writeFloat(adsrAttackMs_);       // 720
streamer.writeFloat(adsrDecayMs_);        // 721
streamer.writeFloat(adsrSustainLevel_);   // 722
streamer.writeFloat(adsrReleaseMs_);      // 723
streamer.writeFloat(adsrAmount_);         // 724
streamer.writeFloat(adsrTimeScale_);      // 725
streamer.writeFloat(adsrAttackCurve_);    // 726
streamer.writeFloat(adsrDecayCurve_);     // 727
streamer.writeFloat(adsrReleaseCurve_);   // 728

// Per-slot ADSR data (8 slots, 9 floats each = 72 floats)
for (int i = 0; i < 8; ++i) {
    streamer.writeFloat(memorySlots_[i].adsrAttackMs);
    streamer.writeFloat(memorySlots_[i].adsrDecayMs);
    streamer.writeFloat(memorySlots_[i].adsrSustainLevel);
    streamer.writeFloat(memorySlots_[i].adsrReleaseMs);
    streamer.writeFloat(memorySlots_[i].adsrAmount);
    streamer.writeFloat(memorySlots_[i].adsrTimeScale);
    streamer.writeFloat(memorySlots_[i].adsrAttackCurve);
    streamer.writeFloat(memorySlots_[i].adsrDecayCurve);
    streamer.writeFloat(memorySlots_[i].adsrReleaseCurve);
}
```

## Read Order (backward compatible)

```
if (version >= 9) {
    // Global ADSR parameters
    float val;
    if (streamer.readFloat(val)) adsrAttackMs_.store(val);
    if (streamer.readFloat(val)) adsrDecayMs_.store(val);
    if (streamer.readFloat(val)) adsrSustainLevel_.store(val);
    if (streamer.readFloat(val)) adsrReleaseMs_.store(val);
    if (streamer.readFloat(val)) adsrAmount_.store(val);
    if (streamer.readFloat(val)) adsrTimeScale_.store(val);
    if (streamer.readFloat(val)) adsrAttackCurve_.store(val);
    if (streamer.readFloat(val)) adsrDecayCurve_.store(val);
    if (streamer.readFloat(val)) adsrReleaseCurve_.store(val);

    // Per-slot ADSR data
    for (int i = 0; i < 8; ++i) {
        if (streamer.readFloat(val)) memorySlots_[i].adsrAttackMs = val;
        if (streamer.readFloat(val)) memorySlots_[i].adsrDecayMs = val;
        if (streamer.readFloat(val)) memorySlots_[i].adsrSustainLevel = val;
        if (streamer.readFloat(val)) memorySlots_[i].adsrReleaseMs = val;
        if (streamer.readFloat(val)) memorySlots_[i].adsrAmount = val;
        if (streamer.readFloat(val)) memorySlots_[i].adsrTimeScale = val;
        if (streamer.readFloat(val)) memorySlots_[i].adsrAttackCurve = val;
        if (streamer.readFloat(val)) memorySlots_[i].adsrDecayCurve = val;
        if (streamer.readFloat(val)) memorySlots_[i].adsrReleaseCurve = val;
    }
}
// v1-v8: defaults apply automatically from atomic/struct initializers
// adsrAmount_ defaults to 0.0f --> bit-exact bypass (FR-009)
// curve amounts default to 0.0f --> linear (FR-020)
```
