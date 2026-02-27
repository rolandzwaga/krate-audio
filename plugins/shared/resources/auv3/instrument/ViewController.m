// Copyright (c) 2025-2026 Krate Audio. All rights reserved.
// AUv3 wrapper view controller — instrument variant (no play button, auto-starts engine)
// Adapted from VST3 SDK note_expression_synth_auv3 example

#import "ViewController.h"
#import <CoreAudioKit/AUViewController.h>
#import "public.sdk/source/vst/auv3wrapper/Shared/AUv3AudioEngine.h"
#import "public.sdk/source/vst/auv3wrapper/Shared/AUv3Wrapper.h"

@class AUv3WrapperViewController;

@interface ViewController ()
{
    AUv3AudioEngine* audioEngine;

    // Container for the custom view.
    AUv3WrapperViewController* auV3ViewController;
}

@property IBOutlet NSView *containerView;

@end

@implementation ViewController

//------------------------------------------------------------------------
- (void)viewDidLoad
{
    [super viewDidLoad];

    [self embedPlugInView];

    AudioComponentDescription desc;

    desc.componentType = kAUcomponentType;
    desc.componentSubType = kAUcomponentSubType;
    desc.componentManufacturer = kAUcomponentManufacturer;
    desc.componentFlags = kAUcomponentFlags;
    desc.componentFlagsMask = kAUcomponentFlagsMask;

    [AUAudioUnit registerSubclass: AUv3Wrapper.class asComponentDescription:desc name:@"Local AUv3" version: UINT32_MAX];

    audioEngine = [[AUv3AudioEngine alloc] initWithComponentType:desc.componentType];

    [audioEngine loadAudioUnitWithComponentDescription:desc completion:^{
        auV3ViewController.audioUnit = (AUv3Wrapper*)audioEngine.currentAudioUnit;
        [audioEngine startStop];
    }];
}

//------------------------------------------------------------------------
- (void)embedPlugInView
{
    NSURL *builtInPlugInURL = [[NSBundle mainBundle] builtInPlugInsURL];
    NSURL *pluginURL = [builtInPlugInURL URLByAppendingPathComponent: @"vst3plugin.appex"];
    NSBundle *appExtensionBundle = [NSBundle bundleWithURL: pluginURL];

    auV3ViewController = [[AUv3WrapperViewController alloc] initWithNibName: @"AUv3WrapperViewController" bundle: appExtensionBundle];

    NSView *view = auV3ViewController.view;
    view.frame = _containerView.bounds;

    [_containerView addSubview: view];

    view.translatesAutoresizingMaskIntoConstraints = NO;

    NSArray *constraints = [NSLayoutConstraint constraintsWithVisualFormat: @"H:|[view]|" options:0 metrics:nil views:NSDictionaryOfVariableBindings(view)];
    [_containerView addConstraints: constraints];

    constraints = [NSLayoutConstraint constraintsWithVisualFormat: @"V:|[view]|" options:0 metrics:nil views:NSDictionaryOfVariableBindings(view)];
    [_containerView addConstraints: constraints];
}

//------------------------------------------------------------------------
- (void)viewDidDisappear
{
    [audioEngine shutdown];
    audioEngine = nil;
    auV3ViewController = nil;
}

@end
