# kindle-apple2

Apple IIe emulator for Kindle e-ink devices, packaged as a KUAL extension.

Runs Apple II software directly on your Kindle's e-ink display. The emulator renders the Apple II's 280x192 HIRES graphics to the Kindle framebuffer, scaled to fill the screen. Keyboard input is provided by [kterm](https://github.com/bfabiszewski/kterm)'s on-screen keyboard.

## Screenshots

*Coming soon*

## Requirements

- Jailbroken Kindle (tested on Kindle Paperwhite, firmware 5.12.x)
- [KUAL](https://www.mobileread.com/forums/showthread.php?t=203326) installed
- [kterm](https://github.com/bfabiszewski/kterm) installed (for on-screen keyboard)
- Apple II disk images in `.do`, `.dsk`, `.nib`, or `.woz` format (not included)

## Installation

1. Download the latest release from the [Releases](../../releases) page
2. Connect your Kindle via USB
3. Copy the `Apple2` folder to `/mnt/us/extensions/` on your Kindle
4. Place your Apple II disk images in the `disks/` subfolder
5. Eject the Kindle and open KUAL
6. Tap "Apple IIe Emulator" to launch

The emulator will boot the first disk image it finds in the `disks/` folder.

## Adding Multiple Games

Place multiple disk images in the `disks/` folder. Then run the menu generator to create a KUAL entry for each game:

```bash
# On the Kindle (via kterm or SSH):
cd /mnt/us/extensions/Apple2
sh gen_menu.sh
```

This scans `disks/` and generates a `menu.json` with a separate KUAL menu entry for each disk image. Next time you open KUAL, you'll see all your games listed under "Apple IIe".

You can also edit `menu.json` manually to customize game names.

## Controls

The on-screen keyboard (provided by kterm) gives you a full QWERTY layout with arrow keys, shift, and ctrl.

- **ESC** — Shows "Exit to Kindle?" prompt
  - **Y** — Quit and return to Kindle
  - **N** — Resume emulation
- **Ctrl-C** — Immediate exit

## Building from Source

### Requirements

- [Zig](https://ziglang.org/) (for cross-compilation, no Docker needed)
- macOS or Linux build host

### Build

```bash
# Static ARM binary for Kindle
make -f Makefile.static

# Package as KUAL extension
mkdir -p kual_extension/disks
cp kindle-apple2-static kual_extension/apple2
```

### macOS test build (headless, no display)

```bash
make -f Makefile.test
./build-test/bin/mii_test your_disk_image.do 200
# Outputs screen_output.pgm
```

## How It Works

This project strips the [MII Apple IIe emulator](https://github.com/buserror/mii_emu) (MIT license) down to its core and adds a Kindle-specific display backend:

- **CPU**: 65C02 emulation running at native speed
- **Video**: Reads Apple II HIRES video RAM directly and renders to `/dev/fb0` with proportional scaling
- **Input**: Reads from stdin in raw mode (kterm translates touch keyboard to terminal input)
- **Disk**: Supports `.do` (DOS order), `.dsk`, `.nib`, and `.woz` disk image formats
- **Display**: Monochrome rendering — Apple II's 1-bit HIRES graphics are a perfect match for e-ink

The entire binary is ~1.4MB statically linked with zero runtime dependencies.

## Compatibility

- Kindle Paperwhite (758x1024) — tested and working
- Other Kindle models — should work (dynamic resolution detection), untested
- The emulator auto-detects screen resolution and scales the Apple II display to fit

## Credits

- [MII Apple IIe Emulator](https://github.com/buserror/mii_emu) by Michel Pollet (MIT license) — core emulation
- [kindle-touch-doom](https://github.com/HimbeersaftLP/kindle-touch-doom) — Kindle framebuffer patterns
- [kterm](https://github.com/bfabiszewski/kterm) — on-screen keyboard
- [geekmaster](https://www.mobileread.com/forums/showthread.php?t=177455) — Kindle framebuffer access code (MIT license)

## License

MIT — see individual source files for copyright notices.
