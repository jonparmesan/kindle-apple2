# kindle-apple2

An Apple IIe emulator that runs directly on Kindle e-ink devices. Play classic Apple II games on your Kindle — the emulator renders Apple II graphics in monochrome, which works well on e-ink displays.

Packaged as a [KUAL](https://www.mobileread.com/forums/showthread.php?t=203326) extension. Uses [kterm](https://github.com/bfabiszewski/kterm) for on-screen keyboard input.

## What You Need

Before installing, make sure you have:

1. **A Kindle with KUAL support** — Tested on Kindle Paperwhite. Other models should work but are untested.
2. **KUAL** — The Kindle Unified Application Launcher. This is how you'll launch the emulator. Install guide: [MobileRead thread](https://www.mobileread.com/forums/showthread.php?t=203326).
3. **kterm** — A terminal emulator for Kindle that provides the on-screen keyboard. Download from [GitHub](https://github.com/bfabiszewski/kterm). Copy the `kterm` folder to `/mnt/us/extensions/` on your Kindle.
4. **Apple II disk images** — The emulator does not include any games. You'll need to provide your own disk images in `.do`, `.dsk`, `.nib`, `.woz`, or `.po` format. Many public domain Apple II programs are available online.

## Installation

### Step 1: Download

Download the latest release from the [Releases](../../releases) page. The release contains:

```
Apple2/
├── apple2          # The emulator binary (ARM, statically linked)
├── apple2.sh       # Launch script
├── config.xml      # KUAL extension config
├── menu.json       # KUAL menu entries
├── gen_menu.sh     # Menu generator script
└── disks/          # Put your disk images here
    └── README.txt
```

### Step 2: Copy to Kindle

1. Connect your Kindle to your computer via USB
2. Copy the entire `Apple2` folder to `/mnt/us/extensions/` on the Kindle
   - On macOS, this appears as `/Volumes/Kindle/extensions/`
   - On Windows, this appears as the Kindle drive under `extensions/`
3. Place your Apple II `.do`/`.dsk`/`.woz` disk images into the `disks/` subfolder

### Step 3: Set up the game menu

Edit `menu.json` in the `Apple2` folder on your computer (while the Kindle is connected via USB). This file controls what appears in KUAL. Add one entry per game:

```json
{
    "items": [
        {
            "name": "Games",
            "items": [
                {"name": "Oregon Trail", "priority": 0, "action": "./apple2.sh", "params": "disks/Oregon_Trail.do", "exitmenu": true, "status": false},
                {"name": "Taipan", "priority": 1, "action": "./apple2.sh", "params": "disks/Taipan.dsk", "exitmenu": true, "status": false}
            ]
        }
    ]
}
```

- The outer `"name": "Games"` creates a subfolder in KUAL
- Each inner entry is a game — `"name"` is what shows in the menu, `"params"` is the disk image path
- Increment `"priority"` for each entry to control the sort order

Alternatively, if you have kterm or SSH access to the Kindle, you can auto-generate the menu:

```bash
cd /mnt/us/extensions/Apple2
sh gen_menu.sh
```

### Step 4: Launch

1. Safely eject the Kindle from your computer
2. On the Kindle, open **KUAL**
3. Tap **Games** > select a game
4. Wait 10-30 seconds for the Apple II to boot from the virtual floppy disk
5. Play using kterm's on-screen keyboard

## Controls

The emulator uses **kterm's on-screen keyboard** which appears at the bottom of the screen. This gives you a full QWERTY keyboard with arrow keys, shift, ctrl, and all special characters.

### Key mappings

| kterm key | Apple II function |
|-----------|------------------|
| Letter keys | Types that letter (sent as uppercase to Apple II) |
| Number keys | Types that number |
| Arrow keys | Cursor movement (varies by game) |
| Enter/Return | Confirm selection, advance text |
| Space | Space bar |
| ESC | **Shows exit dialog** (see below) |
| Ctrl+C | Immediate exit to Kindle |

### Exiting the emulator

Press **ESC** on kterm's keyboard. The emulator pauses and shows:

```
Exit to Kindle?  Y / N
```

- Press **Y** to quit and return to the Kindle home screen
- Press **N** or **ESC** again to resume playing

You can also press **Ctrl+C** for an immediate exit without the prompt.

### Game-specific controls

Most Apple II games display their controls on screen.

## Supported Disk Formats

| Format | Extension | Description |
|--------|-----------|-------------|
| DOS order | `.do`, `.dsk` | Standard Apple II DOS 3.3 disk images (140KB) |
| ProDOS order | `.po` | ProDOS-ordered disk images (140KB) |
| Nibble | `.nib` | Raw nibblized disk format |
| WOZ | `.woz` | Modern preservation format (flux-level accuracy) |

Most Apple II disk images found online are in `.do` or `.dsk` format.

## Troubleshooting

### The game shows a blank screen

Apple II games boot from floppy disk, which takes time. **Wait 15-30 seconds** after launching. The emulator is running even if the screen appears blank — the Apple II is reading the virtual disk.

### The game still doesn't load after waiting

Check the log file at `/mnt/us/extensions/Apple2/apple2.log` (accessible via USB). Look for:
- `PC=$C27F` in the log = the disk controller is stuck in a read loop. The disk image may be corrupted or in an unsupported format.
- `CRASH signal` = the emulator crashed. Please open an issue with the log contents.

### The emulator exits immediately

This usually means the disk image file wasn't found. Check that:
- The file path in `menu.json` matches the actual filename in `disks/`
- The file extension is one of `.do`, `.dsk`, `.nib`, `.woz`, `.po`

### kterm keyboard doesn't appear

Make sure kterm is installed at `/mnt/us/extensions/kterm/`. The emulator launches through kterm to get the on-screen keyboard.

### Some terminal text visible between game and keyboard

This is a known cosmetic issue. The emulator renders the game on the top portion of the screen, and kterm's keyboard renders at the bottom. A thin strip of kterm's terminal may be visible in between. It doesn't affect gameplay.

### How to check logs

Connect your Kindle via USB and read:
```
/mnt/us/extensions/Apple2/apple2.log
```
Each game launch is logged with a timestamp, the disk image loaded, and periodic status updates (CPU program counter, frame count, video mode).

## How It Works

### Architecture

```
┌──────────────────────────────────────────┐
│              KUAL Extension              │
│  apple2.sh launches kterm with emulator  │
├──────────────────────────────────────────┤
│         kindle-apple2 binary             │
├────────────┬─────────────┬───────────────┤
│  MII Core  │  Kindle FB  │  stdin Input  │
│  (65C02    │  (/dev/fb0) │  (from kterm) │
│   CPU,     │  scales to  │  raw terminal │
│   video,   │  fill screen│  keypresses   │
│   disk)    │  e-ink upd  │              │
└────────────┴─────────────┴───────────────┘
```

### Emulation

The core is a stripped-down version of the [MII Apple IIe emulator](https://github.com/buserror/mii_emu) by Michel Pollet (MIT license). MII was chosen for its:

- **MIT license** — permissive, no GPL concerns
- **Pure C99** — simple cross-compilation with zig
- **Modular architecture** — CPU, video, and disk I/O are cleanly separated
- **Accurate 65C02 emulation** — runs at native speed with cycle-accurate timers

We stripped MII down to ~14K LOC by removing the OpenGL/X11 UI, audio, mouse card, serial card, SmartPort, Mockingboard, and debug shell. What remains is the pure emulation core.

### Display rendering

The Apple II HIRES screen is 280x192 pixels. While the original hardware could produce colors via NTSC artifact coloring, the emulator renders in monochrome mode for e-ink. The emulator:

1. Reads the Apple II's HIRES video RAM directly (at memory address $2000 or $4000)
2. Decodes the Apple II's interleaved scanline addressing
3. Scales each pixel proportionally to fill the Kindle's screen width
4. Writes pixel values directly to the Kindle's framebuffer (`/dev/fb0`) via `mmap`
5. Triggers an e-ink display update via ioctl

The monochrome rendering maps each Apple II pixel to black (on) or white (off), which displays cleanly on e-ink without dithering. Games that relied heavily on color may lose some visual information, but most Apple II software was designed to be usable on monochrome monitors.

The emulator detects the Kindle's screen resolution at startup (`FBIOGET_VSCREENINFO`) and computes the scaling dynamically, so it works on any Kindle model.

### Keyboard input

The emulator reads keypresses from **stdin** in raw terminal mode. It's launched as a subprocess of kterm, which provides the on-screen keyboard. When you tap a key on kterm's keyboard:

1. kterm generates the corresponding terminal character
2. The emulator reads it from stdin
3. Arrow keys arrive as ANSI escape sequences (`ESC [ A/B/C/D`)
4. Letters are converted to uppercase (Apple II convention)
5. The keypress is fed to the emulated Apple II via `mii_keypress()`

### Launch sequence

When you tap a game in KUAL, here's what happens:

1. `apple2.sh` runs and suspends the Kindle UI framework (`lipc-set-prop`)
2. The script launches kterm with the emulator as its child process
3. The emulator opens `/dev/fb0`, detects screen resolution, and clears the terminal
4. The emulator initializes the Apple IIe (CPU, memory, ROM, Disk II controller)
5. The disk image is loaded into the virtual Disk II drive
6. The Apple IIe boots from the virtual floppy (cold reset)
7. The main loop runs: execute CPU instructions → render HIRES to framebuffer → read keyboard → repeat
8. On exit (ESC+Y or Ctrl+C), the emulator closes the framebuffer
9. The launch script restores the Kindle UI framework

### Cross-compilation

The binary is cross-compiled for ARM Linux using [Zig](https://ziglang.org/)'s built-in C cross-compiler:

```
zig cc -target arm-linux-musleabi -static
```

This produces a fully **statically linked** ARM binary with musl libc — no runtime dependencies on the Kindle's system libraries. The binary is ~1.4MB.

No Docker, no SDK, no toolchain setup. Just `zig` and `make`.

## Building from Source

### Prerequisites

- [Zig](https://ziglang.org/) 0.15+ (install via `brew install zig` on macOS)
- macOS or Linux

### Build the Kindle binary

```bash
git clone https://github.com/jonparmesan/kindle-apple2.git
cd kindle-apple2
make -f Makefile.static
```

Output: `kindle-apple2-static` (ARM ELF binary, ~1.4MB)

### Deploy to Kindle

```bash
mkdir -p kual_extension/disks
cp kindle-apple2-static kual_extension/apple2
# Copy kual_extension/ contents to /mnt/us/extensions/Apple2/ on Kindle
```

### Test on macOS (headless)

`Makefile.test` builds a headless version for macOS that can boot a disk image and dump the Apple II screen to a PGM file. This requires writing a test harness (`mii_test_main.c`) — see the source code for the Kindle main loop as a reference.

## Compatibility

| Device | Status |
|--------|--------|
| Kindle Paperwhite (758x1024) | Tested, working |
| Other Kindle models | Untested, should work (dynamic resolution detection) |

The emulator auto-detects screen resolution at startup and scales accordingly.

## Project Structure

```
kindle-apple2/
├── Makefile.static          # Cross-compile for Kindle ARM
├── Makefile.test            # macOS headless test build
├── README.md
├── kual_extension/          # KUAL extension files
│   ├── apple2.sh            # Launch script
│   ├── config.xml           # KUAL extension metadata
│   ├── gen_menu.sh          # Auto-generate menu from disk images
│   ├── menu.json            # KUAL menu (list of games)
│   └── disks/               # Disk images go here
├── src/
│   ├── mii_kindle_fb.c/h    # Kindle framebuffer backend
│   ├── mii_kindle_input.c/h # Keyboard input (stdin from kterm)
│   ├── mii_kindle_main.c    # Main loop
│   ├── mii.c/h              # MII emulator core
│   ├── mii_65c02.c/h        # 65C02 CPU emulation
│   ├── mii_video.c/h        # Apple II video rendering
│   ├── mii_bank.c/h         # Memory bank management
│   ├── drivers/             # Disk II controller, no-slot clock
│   ├── format/              # Disk image format support (DSK/NIB/WOZ)
│   └── roms/                # Apple IIe ROM data (compiled in)
```

## Credits

- [MII Apple IIe Emulator](https://github.com/buserror/mii_emu) by Michel Pollet — core emulation (MIT license)
- [kindle-touch-doom](https://github.com/HimbeersaftLP/kindle-touch-doom) by HimbeersaftLP — Kindle framebuffer patterns and e-ink update code
- [kterm](https://github.com/bfabiszewski/kterm) by Bartek Fabiszewski — on-screen keyboard
- [geekmaster](https://www.mobileread.com/forums/showthread.php?t=177455) — original Kindle framebuffer access code (MIT license)

## License

MIT — see individual source files for copyright notices. The MII emulator core is MIT licensed by Michel Pollet. The Kindle framebuffer code is MIT licensed by geekmaster.
