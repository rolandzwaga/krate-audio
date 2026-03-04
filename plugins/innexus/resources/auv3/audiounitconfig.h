// Innexus AUv3 Audio Unit Configuration
// Copyright (c) 2025-2026 Krate Audio. All rights reserved.

// Effect (aufx), Generator (augn), Instrument (aumu), and Music Effect (aufm)
#define kAUcomponentType          'aumu'
#define kAUcomponentType1         aumu

// A subtype code (unique ID), exactly 4 alphanumeric characters
#define kAUcomponentSubType       'Innx'
#define kAUcomponentSubType1      Innx

// A manufacturer code, exactly 4 alphanumeric characters
#define kAUcomponentManufacturer  'KrAt'
#define kAUcomponentManufacturer1 KrAt

#define kAUcomponentDescription   AUv3WrapperExtension
#define kAUcomponentName          Krate Audio: Innexus
#define kAUcomponentTag           Synthesizer

// 0.1.0 = (0 << 16) | (1 << 8) | 0 = 256
#define kAUcomponentVersion       256

// 0222 == two configs: (0 in, 2 out) instrument + (2 in, 2 out) with sidechain
#define kSupportedNumChannels     0222

// No audio preview file for instruments
// #define kAudioFileName          "drumLoop"
// #define kAudioFileFormat        "wav"

#define kAUcomponentFlags         0
#define kAUcomponentFlagsMask     0
#define kAUapplicationDelegateClassName AppDelegate
