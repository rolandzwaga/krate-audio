# Iterum Project Overview

This is a monorepo of Krate Audio VST3 plugins (KrateDSP shared library + iterum,
disrumpo, ruinae, innexus, gradus, membrum). The canonical, always-current
description lives in-repo — do not maintain a second copy here (it drifts).

- **Architecture, rules, build/test commands:** `CLAUDE.md` (root) and the per-area
  `CLAUDE.md` leaf files under `dsp/` and `plugins/*/`.
- **Generated maps:** `specs/_architecture_/` — `repo-map.json`, `symbols.json`,
  layer/plugin reference docs, and `gotchas.md` (cross-cutting failure modes).
- **Cross-cutting gotchas index:** `specs/_architecture_/gotchas.md`.
