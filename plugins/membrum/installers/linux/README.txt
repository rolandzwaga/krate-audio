================================================================================
                       MEMBRUM VST3 DRUM MACHINE PLUGIN
                           Installation Guide
================================================================================

Thank you for downloading Membrum, a synthesised drum machine plugin
by Krate Audio.


INSTALLATION
------------

User Installation (Recommended):
   Copy the plugin to your personal VST3 folder:

   cp -r Membrum.vst3 ~/.vst3/

   This makes the plugin available only to your user account.


System-Wide Installation:
   For all users on the system, copy to the system VST3 folder:

   sudo cp -r Membrum.vst3 /usr/lib/vst3/

   This requires administrator (root) privileges.


FACTORY PRESETS
---------------

Membrum ships with 20 factory kit presets organised into four
subcategories: Acoustic, Electronic, Percussive, and Unnatural.
The presets are included in the presets/ folder within this archive.

User Installation (Recommended):
   Copy presets to your personal kit-preset folder:

   mkdir -p ~/Documents/Krate\ Audio/Membrum/Kits
   cp -r presets/* ~/Documents/Krate\ Audio/Membrum/Kits/

System-Wide Installation:
   For all users on the system:

   sudo mkdir -p /usr/share/krate-audio/membrum/Kits
   sudo cp -r presets/* /usr/share/krate-audio/membrum/Kits/


VERIFICATION
------------

After installation, verify the plugin is recognized:

1. Open your DAW (Digital Audio Workstation)
2. Rescan VST3 plugins (if your DAW doesn't auto-detect)
3. Look for "Membrum" in your plugin list under Instruments


USAGE
-----

Membrum is a 32-pad synthesised drum machine that registers as an
Instrument plugin. It accepts MIDI input on notes 36-67 (C1-G3) and
outputs:

1. Main stereo audio output
2. Up to 15 auxiliary stereo bus outputs for separate DAW processing
   of individual pads (route per pad via the Output Bus selector)

To get started:

- Insert Membrum on an instrument track
- Open the Kit Browser (right column) and pick a factory kit
- Trigger pads with a MIDI keyboard or by clicking pads in the grid
- Click any pad to select it and edit its parameters in the centre
  column

The Acoustic / Extended toggle in the top-right of the selected-pad
panel switches between the focused natural-drum subset and the full
extended parameter set.


SUPPORTED DAWs
--------------

Membrum has been tested with the following Linux DAWs:

- Bitwig Studio
- Ardour
- REAPER (with VST3 support enabled)
- Renoise
- Qtractor
- Carla (VST host)

Any DAW that supports VST3 instruments should work.


TROUBLESHOOTING
---------------

Plugin not appearing in DAW:
- Ensure the .vst3 folder was copied completely (including all subfolders)
- Check that your DAW is configured to scan ~/.vst3/ or /usr/lib/vst3/
- Try rescanning plugins manually in your DAW's settings

Factory presets not appearing in the Kit Browser:
- Verify presets were copied to ~/Documents/Krate Audio/Membrum/Kits/
- The directory must contain four subdirectories: Acoustic, Electronic,
  Percussive, Unnatural - the browser scans for these by name
- Restart your DAW after installing presets

Permission errors:
- For user installation: ensure ~/.vst3/ exists (mkdir -p ~/.vst3/)
- For system installation: use sudo as shown above


SUPPORT
-------

For bug reports, feature requests, or questions:
https://github.com/rolandzwaga/krate-audio/issues


LICENSE
-------

Copyright (c) 2026 Krate Audio

================================================================================
