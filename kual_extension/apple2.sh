#!/bin/sh
# Apple IIe Emulator for Kindle — KUAL Extension
# Uses kterm for on-screen keyboard input
cd "$(dirname "$0")"

# Find the first .do or .dsk disk image in the disks/ folder
DISK1=""
for f in disks/*.do disks/*.dsk disks/*.nib disks/*.woz; do
    if [ -f "$f" ]; then
        DISK1="$f"
        break
    fi
done

if [ -z "$DISK1" ]; then
    eips 5 20 "No disk image found in disks/ folder"
    eips 5 22 "Place .do or .dsk files there"
    sleep 3
    exit 1
fi

# Suspend Kindle UI but keep X11 running for kterm
lipc-set-prop com.lab126.powerd preventScreenSaver 1 2>/dev/null
lipc-set-prop com.lab126.pillow disableEnablePillow disable 2>/dev/null
killall -STOP cvm 2>/dev/null

eips -f -c 2>/dev/null
sleep 1

# Launch via kterm (provides the on-screen keyboard)
/mnt/us/extensions/kterm/bin/kterm \
  -e "/mnt/us/extensions/Apple2/apple2 /mnt/us/extensions/Apple2/$DISK1"

# Restore Kindle UI
killall -CONT cvm 2>/dev/null
lipc-set-prop com.lab126.pillow disableEnablePillow enable 2>/dev/null
lipc-set-prop com.lab126.powerd preventScreenSaver 0 2>/dev/null
eips -f -c 2>/dev/null
