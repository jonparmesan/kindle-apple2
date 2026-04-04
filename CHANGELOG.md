# Changelog

All notable changes to kindle-apple2 will be documented in this file.

## [0.3.0.0] - 2026-04-03

### Added
- **Boot splash screen**: Shows game name and "Booting from disk..." immediately on launch, eliminating the blank screen wait
- **On-screen error messages**: Disk load failures now display on the e-ink screen instead of silently failing
- **Save state support**: Save & resume games via the exit menu (ESC → S). States auto-load on next launch. Use `--fresh` to force cold boot
- **Runtime disk switching**: Press Ctrl+D to swap disk images without restarting — enables multi-disk games (Ultima, Wizardry, etc.)
- **Status bar**: Shows disk activity indicator and rendering mode between the game area and keyboard
- **CI/CD pipeline**: GitHub Actions workflow for automated ARM cross-compilation

### Changed
- Exit dialog now renders text directly on the framebuffer instead of via terminal output
- Exit menu expanded with Save & Exit option (S key)
- Framebuffer initialized before emulator so errors can be displayed visually

### Fixed
- Exit dialog text appeared as terminal text instead of in the on-screen dialog box
- Cosmetic terminal strip between game and keyboard now used for the status bar
- Stack corruption in status bar floppy motor check (D2_GET_FLOPPY writes two pointers)
- Save file validation: corrupt saves no longer cause out-of-bounds access or division by zero
- Disk overlay now uses select() instead of blocking read, preventing hang on signal
- Added fsync before rename when saving state for FAT32 power-loss safety
- Save format bumped to v3 with complete Disk II controller state for write-safe restores

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
