## What this changes

<!-- One or two sentences. What is different in the game after this merges? -->

## Why

<!-- The problem being solved. Link the issue: Fixes #123 -->

## How it was verified

- [ ] Builds on Linux
- [ ] Loaded a world and played it
- [ ] Placed and broke blocks
- [ ] Saved, quit, reloaded, and the world was intact
- [ ] Not runtime tested (explain why below)

## Checklist

- [ ] Shader changes updated all backend variants and the committed `.spv`
- [ ] Save format changes bumped `WorldMeta::CURRENT_VERSION` and handle old worlds
- [ ] Collision changes kept the single-pass MTV resolution intact
- [ ] New event handlers are unregistered on scene teardown
