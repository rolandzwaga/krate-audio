================================================================================
                       GRADUS VST3 ARPEGGIATOR PLUGIN
                           Installation Guide
================================================================================

Thank you for downloading Gradus, a standalone step arpeggiator
plugin by Krate Audio.


INSTALLATION
------------

User Installation (Recommended):
   Copy the plugin to your personal VST3 folder:

   cp -r Gradus.vst3 ~/.vst3/

   This makes the plugin available only to your user account.


System-Wide Installation:
   For all users on the system, copy to the system VST3 folder:

   sudo cp -r Gradus.vst3 /usr/lib/vst3/

   This requires administrator (root) privileges.


FACTORY PRESETS
---------------

Factory presets are included in the presets/ folder within this archive.

User Installation (Recommended):
   Copy presets to your personal presets folder:

   mkdir -p ~/Documents/Krate\ Audio/Gradus
   cp -r presets/* ~/Documents/Krate\ Audio/Gradus/

System-Wide Installation:
   For all users on the system:

   sudo mkdir -p /usr/share/krate-audio/gradus
   sudo cp -r presets/* /usr/share/krate-audio/gradus/


VERIFICATION
------------

After installation, verify the plugin is recognized:

1. Open your DAW (Digital Audio Workstation)
2. Rescan VST3 plugins (if your DAW doesn't auto-detect)
3. Look for "Gradus" in your plugin list under Instruments


USAGE
-----

Gradus is an arpeggiator that registers as an Instrument plugin.
It accepts MIDI input, arpeggiates the held notes, and outputs:

1. MIDI events (arpeggiated notes) via the MIDI output bus
2. Audio from the built-in audition synth (for previewing patterns)

To drive an external synth:
- Insert Gradus on an instrument track
- Route its MIDI output to your target synthesizer
  (method varies by DAW - consult your DAW's documentation)

The built-in audition synth can be toggled on/off in the bottom
bar of the plugin UI.


SUPPORTED DAWs
--------------

Gradus has been tested with the following Linux DAWs:

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
