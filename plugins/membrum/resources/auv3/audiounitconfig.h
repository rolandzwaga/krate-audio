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

// 00220032 == two configs:
//   0022 = (0 in, 2 out) instrument main stereo only
//   0032 = (0 in, 32 out) instrument with 16 stereo buses
#define kSupportedNumChannels     00220032

#define kAUcomponentFlags         0
#define kAUcomponentFlagsMask     0
#define kAUapplicationDelegateClassName AppDelegate
