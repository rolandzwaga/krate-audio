# Krate Manager

Krate Manager shows which Krate Audio plug-ins are installed on this computer, whether updates exist, and installs,
updates and removes them. It also updates itself. The plug-ins and the manager are free and open source: there are no
accounts and no license keys, and no build is code-signed with a paid certificate, which is why the first launch needs
the steps below.

Maintainers: the release procedure is in [RELEASING.md](RELEASING.md).

## First launch

### Windows

Krate Manager is not code-signed, so when you run its Windows setup, Windows shows "Windows protected your PC": click
"More info", then "Run anyway". The plug-in installers Krate Manager downloads itself do not show this warning.

Windows 11 Smart App Control in enforcement mode blocks unsigned programs, Krate Manager and the plug-in installers
included, so it is not supported: Krate Manager runs there only after Smart App Control is turned off.

A `%LOCALAPPDATA%` folder redirected outside the Windows user profiles folder is not supported: Krate Manager then
refuses every install, update and removal that needs administrator rights, with the error `E_HELPER_JOB_LOCATION`.

### macOS

Written from the recorded checks; see the release notes until then.

### Linux

On Linux, Krate Manager comes as an AppImage, which updates itself, and as a `.deb` package, which does not: to update a
`.deb` install, download the new `.deb` from the release page and install it.

On Linux, Krate Manager installs plug-ins to `~/.vst3` unless you choose another folder in Settings, and factory presets
to `~/Documents/Krate Audio`, one folder per plug-in.

REAPER on Linux may not scan `~/.vst3`: add `~/.vst3` to REAPER's VST path list in Preferences > Plug-ins > VST > Edit
path list, then rescan.

If the Krate Manager window is blank or flickers (seen with NVIDIA graphics), turn on "Work around a blank or flickering
window (NVIDIA)" in Settings: it sets `WEBKIT_DISABLE_DMABUF_RENDERER=1` the next time Krate Manager starts, and nothing
else. The toggle does not set the other fixes, which you apply yourself: the `nvidia_drm.modeset=1` kernel parameter
(NVIDIA drivers older than 545) and starting Krate Manager from a terminal with `__NV_DISABLE_EXPLICIT_SYNC=1`, both
worth trying first because they keep hardware acceleration, and, as a last resort, starting it from a terminal with
`WEBKIT_DISABLE_COMPOSITING_MODE=1`.

If the window is already blank, so Settings cannot be reached, start Krate Manager once from a terminal with
`WEBKIT_DISABLE_DMABUF_RENDERER=1` in front of its command, then turn the toggle on.

### Every system

After Krate Manager installs or updates a plug-in, restart your DAW or rescan its plug-ins so that it finds the new
version.

## Supported systems

Windows 10 22H2 and Windows 11 x64; macOS 15 and later (Apple silicon and Intel); Linux x86_64 at the glibc of the
`ubuntu:22.04` build.
