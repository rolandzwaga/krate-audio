// Ruinae AUv3 Audio Unit Configuration
// Copyright (c) 2025-2026 Krate Audio. All rights reserved.

// Effect (aufx), Generator (augn), Instrument (aumu), and Music Effect (aufm)
#define kAUcomponentType          'aumu'
#define kAUcomponentType1         aumu

// A subtype code (unique ID), exactly 4 alphanumeric characters
#define kAUcomponentSubType       'Ruin'
#define kAUcomponentSubType1      Ruin

// A manufacturer code, exactly 4 alphanumeric characters
#define kAUcomponentManufacturer  'KrAt'
#define kAUcomponentManufacturer1 KrAt

#define kAUcomponentDescription   AUv3WrapperExtension
#define kAUcomponentName          Krate Audio: Ruinae
#define kAUcomponentTag           Synthesizer

// 0.9.0 = (0 << 16) | (9 << 8) | 0 = 2304
#define kAUcomponentVersion       2304

// 02 == no inputs, stereo out (instrument)
#define kSupportedNumChannels     02

// No audio preview file for instruments
// #define kAudioFileName          "drumLoop"
// #define kAudioFileFormat        "wav"

#define kAUcomponentFlags         0
#define kAUcomponentFlagsMask     0
#define kAUapplicationDelegateClassName AppDelegate
