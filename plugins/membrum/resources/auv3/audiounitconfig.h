// Membrum AUv3 Audio Unit Configuration
// Copyright (c) 2026 Krate Audio. All rights reserved.

// Effect (aufx), Generator (augn), Instrument (aumu), and Music Effect (aufm)
#define kAUcomponentType          'aumu'
#define kAUcomponentType1         aumu

// A subtype code (unique ID), exactly 4 alphanumeric characters
#define kAUcomponentSubType       'Mbrm'
#define kAUcomponentSubType1      Mbrm

// A manufacturer code, exactly 4 alphanumeric characters
#define kAUcomponentManufacturer  'KrAt'
#define kAUcomponentManufacturer1 KrAt

#define kAUcomponentDescription   AUv3WrapperExtension
#define kAUcomponentName          Krate Audio: Membrum
#define kAUcomponentTag           Synthesizer

// 0.1.0 = (0 << 16) | (1 << 8) | 0 = 256
#define kAUcomponentVersion       256

// 0022 == one config: (0 in, 2 out) instrument only, no sidechain
#define kSupportedNumChannels     0022

#define kAUcomponentFlags         0
#define kAUcomponentFlagsMask     0
#define kAUapplicationDelegateClassName AppDelegate
