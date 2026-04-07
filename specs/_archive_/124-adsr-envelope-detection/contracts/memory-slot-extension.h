// Contract: MemorySlot extension for ADSR data
// Location: dsp/include/krate/dsp/processors/harmonic_snapshot.h
// Extended fields added to struct MemorySlot

struct MemorySlot {
    HarmonicSnapshot snapshot{};
    bool occupied = false;

    // ADSR Envelope parameters (Spec 124)
    float adsrAttackMs = 10.0f;        ///< Attack time (ms)
    float adsrDecayMs = 100.0f;        ///< Decay time (ms)
    float adsrSustainLevel = 1.0f;     ///< Sustain level [0, 1]
    float adsrReleaseMs = 100.0f;      ///< Release time (ms)
    float adsrAmount = 0.0f;           ///< Envelope amount [0, 1]
    float adsrTimeScale = 1.0f;        ///< Time scale [0.25, 4.0]
    float adsrAttackCurve = 0.0f;      ///< Attack curve [-1, +1]
    float adsrDecayCurve = 0.0f;       ///< Decay curve [-1, +1]
    float adsrReleaseCurve = 0.0f;     ///< Release curve [-1, +1]
};

// Interpolation helpers (for morph/evolution)
// Time params: geometric mean  -->  exp((1-t)*log(a) + t*log(b))
// Level/curve params: linear   -->  (1-t)*a + t*b
