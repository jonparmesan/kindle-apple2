#!/bin/sh
# Generates menu.json from disk images in the disks/ folder.
# Run this after adding new disk images, or on the Kindle via kterm.
cd "$(dirname "$0")"

cat > menu.json << 'HEADER'
{
    "items": [
HEADER

PRIORITY=0
FIRST=1

for f in disks/*.do disks/*.dsk disks/*.nib disks/*.woz; do
    [ ! -f "$f" ] && continue

    # Extract game name from filename (strip path and extension)
    NAME=$(basename "$f" | sed 's/\.[^.]*$//' | sed 's/_/ /g')

    if [ $FIRST -eq 0 ]; then
        echo "," >> menu.json
    fi
    FIRST=0

    cat >> menu.json << ENTRY
        {
            "name": "Apple IIe: $NAME",
            "priority": $PRIORITY,
            "action": "./apple2.sh",
            "params": "$f",
            "exitmenu": true,
            "status": false
        }
ENTRY
    PRIORITY=$((PRIORITY + 1))
done

cat >> menu.json << 'FOOTER'
    ]
}
FOOTER

echo "Generated menu.json with $PRIORITY game(s)"
