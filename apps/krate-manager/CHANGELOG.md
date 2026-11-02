# Changelog

All notable changes to Krate Manager will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.1.0] - Unreleased

### Added

- Scaffold: Tauri 2 app, platform abstraction, settings model and the manager CI.
- Update all: one confirmation lists every offered plug-in update, then downloads them (three at a time) and installs
  them in one run, behind at most one administrator prompt on Windows and macOS.
- Linux settings screen: choose the plug-in folder (or go back to the default `~/.vst3`) and turn on a graphics
  workaround for a blank or flickering window, which takes effect at the next start.
- Remove for a plug-in that Krate Manager installed on Linux but that the plug-in list no longer names.
