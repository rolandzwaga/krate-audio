// Copyright (c) 2025-2026 Krate Audio. All rights reserved.
// AUv3 wrapper view controller — effect variant (play button + audio file loading)
// Adapted from VST3 SDK again_auv3 example

#import "ViewController.h"
#import <CoreAudioKit/AUViewController.h>
#import "public.sdk/source/vst/auv3wrapper/Shared/AUv3AudioEngine.h"
#import "public.sdk/source/vst/auv3wrapper/Shared/AUv3Wrapper.h"

@class AUv3WrapperViewController;

@interface ViewController ()
{
    // Button for playback
    IBOutlet NSButton* playButton;

    AUv3AudioEngine* audioEngine;

    // Container for the custom view.
    AUv3WrapperViewController* auV3ViewController;
}

@property IBOutlet NSView *containerView;
-(IBAction)togglePlay:(id)sender;
-(void)handleMenuSelection:(id)sender;

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

    if (desc.componentType == 'aufx' || desc.componentType == 'aumf')
        [self addFileMenuEntry];

    [AUAudioUnit registerSubclass: AUv3Wrapper.class asComponentDescription:desc name:@"Local AUv3" version: UINT32_MAX];

    audioEngine = [[AUv3AudioEngine alloc] initWithComponentType:desc.componentType];

    [audioEngine loadAudioUnitWithComponentDescription:desc completion:^{
        auV3ViewController.audioUnit = (AUv3Wrapper*)audioEngine.currentAudioUnit;

        NSString* fileName = @kAudioFileName;
        NSString* fileFormat = @kAudioFileFormat;
        NSURL* fileURL = [[NSBundle mainBundle] URLForResource:fileName withExtension:fileFormat];
        NSError* error = [audioEngine loadAudioFile:fileURL];
        if (error)
        {
            NSLog (@"Error setting up audio or midi file: %@", [error description]);
        }
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

    NSArray *constraints = [NSLayoutConstraint constraintsWithVisualFormat: @"H:|-[view]-|" options:0 metrics:nil views:NSDictionaryOfVariableBindings(view)];
    [_containerView addConstraints: constraints];
    constraints = [NSLayoutConstraint constraintsWithVisualFormat: @"V:|-[view]-|" options:0 metrics:nil views:NSDictionaryOfVariableBindings(view)];
    [_containerView addConstraints: constraints];
}

//------------------------------------------------------------------------
-(void)addFileMenuEntry
{
    NSApplication *app = [NSApplication sharedApplication];
    NSMenu *fileMenu = [[app.mainMenu itemWithTag:123] submenu];
    NSMenuItem *openFileItem = [[NSMenuItem alloc] initWithTitle:@"Load file..."
                                                          action:@selector(handleMenuSelection:)
                                                   keyEquivalent:@"O"];
    [fileMenu insertItem:openFileItem atIndex:0];
}

//------------------------------------------------------------------------
-(void)handleMenuSelection:(NSMenuItem *)sender
{
    NSOpenPanel* openPanel = [NSOpenPanel openPanel];
    openPanel.title = @"Choose an audio file";
    openPanel.showsResizeIndicator = YES;
    openPanel.canChooseFiles = YES;
    openPanel.allowsMultipleSelection = NO;
    openPanel.canChooseDirectories = NO;
    openPanel.canCreateDirectories = YES;
    openPanel.allowedFileTypes = @[@"aac", @"aif", @"aiff", @"caf", @"m4a", @"mp3", @"wav"];

    if ( [openPanel runModal] == NSModalResponseOK )
    {
        NSArray* urls = [openPanel URLs];
        for(int i = 0; i < [urls count]; i++ )
        {
            NSError* error = [audioEngine loadAudioFile:[urls objectAtIndex:i]];
            if (error != nil)
            {
                NSAlert *alert = [[NSAlert alloc] init];
                [alert setMessageText:@"Error loading file"];
                [alert setInformativeText:@"Something went wrong loading the audio file. Please make sure to select the correct format and try again."];
                [alert addButtonWithTitle:@"Ok"];
                [alert runModal];
            }
        }
    }
}

//------------------------------------------------------------------------
-(IBAction)togglePlay:(id)sender
{
    BOOL isPlaying = [audioEngine startStop];
    [playButton setTitle: isPlaying ? @"Stop" : @"Play"];
}

#pragma mark <NSWindowDelegate>
//------------------------------------------------------------------------
- (void)windowWillClose:(NSNotification *)notification
{
    auV3ViewController = nil;
}
@end
