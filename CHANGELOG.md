# Changelog

All notable changes to kindle-apple2 will be documented in this file.

## [0.2.1.0] - 2026-04-04

### Added
- Runtime disk swapping via Ctrl-D menu: browse and load any disk image from the disks/ directory while playing
- Auto-pairing for multi-disk games: automatically detects Side A/Side B pairs (and similar naming patterns) and pre-loads both into Drive 1 and Drive 2
- Startup hint when multi-disk game detected: "Disk 2 loaded. Press Ctrl-D to swap disks."
- Drive selection (Tab key) to swap either Drive 1 or Drive 2 independently
- Pagination for large disk collections (>9 images)
- Error handling for failed disk loads and path overflow protection

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
