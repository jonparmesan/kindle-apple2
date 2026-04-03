#!/bin/sh
# Apple IIe Emulator for Kindle — KUAL Extension
cd "$(dirname "$0")"

LOG="/mnt/us/extensions/Apple2/apple2.log"

# Log everything
echo "=== $(date) ===" >> "$LOG"
echo "Args: $*" >> "$LOG"

# Parse arguments: extract flags (--mono) and disk path
FLAGS=""
DISK1=""
for arg in "$@"; do
    case "$arg" in
        --*) FLAGS="$FLAGS $arg" ;;
        *)   [ -z "$DISK1" ] && DISK1="$arg" ;;
    esac
done

if [ -z "$DISK1" ]; then
    for f in disks/*.do disks/*.dsk disks/*.nib disks/*.woz disks/*.po; do
        if [ -f "$f" ]; then
            DISK1="$f"
            break
        fi
    done
fi

if [ -z "$DISK1" ] || [ ! -f "$DISK1" ]; then
    echo "ERROR: No disk image found: $1" >> "$LOG"
    eips 5 20 "No disk image found"
    sleep 3
    exit 1
fi

echo "Loading: $DISK1 (flags:$FLAGS)" >> "$LOG"

# Suspend Kindle UI
lipc-set-prop com.lab126.powerd preventScreenSaver 1 2>/dev/null
lipc-set-prop com.lab126.pillow disableEnablePillow disable 2>/dev/null
killall -STOP cvm 2>/dev/null

eips -f -c 2>/dev/null
sleep 1

# Launch via kterm — redirect ALL output to log
FULL_PATH="/mnt/us/extensions/Apple2/$DISK1"
/mnt/us/extensions/kterm/bin/kterm \
  -e "/mnt/us/extensions/Apple2/apple2$FLAGS $FULL_PATH" \
  >> "$LOG" 2>&1

echo "Exit code: $?" >> "$LOG"
echo "=== done ===" >> "$LOG"

# Restore Kindle UI
killall -CONT cvm 2>/dev/null
lipc-set-prop com.lab126.pillow disableEnablePillow enable 2>/dev/null
lipc-set-prop com.lab126.powerd preventScreenSaver 0 2>/dev/null
eips -f -c 2>/dev/null
