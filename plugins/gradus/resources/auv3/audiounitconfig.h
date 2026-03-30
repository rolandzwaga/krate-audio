// Gradus AUv3 Audio Unit Configuration
// Copyright (c) 2026 Krate Audio. All rights reserved.

// Effect (aufx), Generator (augn), Instrument (aumu), and Music Effect (aufm)
#define kAUcomponentType          'aumu'
#define kAUcomponentType1         aumu

// A subtype code (unique ID), exactly 4 alphanumeric characters
#define kAUcomponentSubType       'Grad'
#define kAUcomponentSubType1      Grad

// A manufacturer code, exactly 4 alphanumeric characters
#define kAUcomponentManufacturer  'KrAt'
#define kAUcomponentManufacturer1 KrAt

#define kAUcomponentDescription   AUv3WrapperExtension
#define kAUcomponentName          Krate Audio: Gradus
#define kAUcomponentTag           Synthesizer

// 1.0.0 = (1 << 16) | (0 << 8) | 0 = 65536
#define kAUcomponentVersion       65536

// 0022 == one config: (0 in, 2 out) instrument only, no sidechain
#define kSupportedNumChannels     0022

#define kAUcomponentFlags         0
#define kAUcomponentFlagsMask     0
#define kAUapplicationDelegateClassName AppDelegate
