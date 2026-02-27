// Iterum AUv3 Audio Unit Configuration
// Copyright (c) 2025-2026 Krate Audio. All rights reserved.

// Effect (aufx), Generator (augn), Instrument (aumu), and Music Effect (aufm)
#define kAUcomponentType          'aufx'
#define kAUcomponentType1         aufx

// A subtype code (unique ID), exactly 4 alphanumeric characters
#define kAUcomponentSubType       'Itrm'
#define kAUcomponentSubType1      Itrm

// A manufacturer code, exactly 4 alphanumeric characters
#define kAUcomponentManufacturer  'KrAt'
#define kAUcomponentManufacturer1 KrAt

#define kAUcomponentDescription   AUv3WrapperExtension
#define kAUcomponentName          Krate Audio: Iterum
#define kAUcomponentTag           Effects

// 0.14.1 = (0 << 16) | (14 << 8) | 1 = 3585
#define kAUcomponentVersion       3585

// 1122 == config1: [mono in, mono out], config2: [stereo in, stereo out]
#define kSupportedNumChannels     1122

#define kAudioFileName            "drumLoop"
#define kAudioFileFormat          "wav"

#define kAUcomponentFlags         0
#define kAUcomponentFlagsMask     0
#define kAUapplicationDelegateClassName AppDelegate
