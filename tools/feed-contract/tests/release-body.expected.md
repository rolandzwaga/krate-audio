### Added

- Krate Manager updates itself.

## Install

- **Windows (x64):** download `Krate-Manager-0.1.0-Windows-x64-setup.exe` and run it. It installs for the current user.
- **macOS 15 or later (Apple silicon and Intel):** install `Krate-Manager-0.1.0-macOS.pkg` from Terminal
  (provisional: see the Krate Manager first-launch notes when they are published):

  ```sh
  curl -fLO https://github.com/o/r/releases/download/manager/v0.1.0/Krate-Manager-0.1.0-macOS.pkg && sudo installer -pkg Krate-Manager-0.1.0-macOS.pkg -target /
  ```

  It installs `Krate Manager.app` into `/Applications`.
- **Linux (x86_64):** download `Krate-Manager-0.1.0-Linux-x86_64.AppImage`, make it executable (`chmod +x`) and run it,
  or install `Krate-Manager-0.1.0-Linux-amd64.deb` with `sudo apt install ./Krate-Manager-0.1.0-Linux-amd64.deb`.

`Krate-Manager-0.1.0-macOS-universal.app.tar.gz`, the `.sig` files and `latest.json` are what an installed Krate Manager
downloads to update itself; installing does not need them.
