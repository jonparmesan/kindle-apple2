# Changelog

All notable changes to kindle-apple2 will be documented in this file.

## [0.2.0.0] - 2026-04-03

### Added
- Grayscale dithering for color games: Apple II colors now render as distinguishable gray patterns on e-ink using ordered Bayer dithering
- `--mono` flag to force monochrome rendering for games that look better in pure black/white
- Frame skip optimization: renderer skips e-ink updates when the video hasn't changed

### Changed
- Default rendering mode is now dithered grayscale (was forced monochrome)
- Renderer reads from the emulator's color pixel buffer instead of raw VRAM
- Launch script now properly forwards flags (like `--mono`) to the binary

## [0.1.0.0] - 2026-04-03

### Added
- Initial release: Apple IIe emulator for Kindle e-ink
- Multi-game support with auto-generated KUAL menu
- HIRES monochrome rendering scaled to fill screen
- On-screen keyboard input via kterm
- Exit prompt (ESC to pause, Y/N to quit)
