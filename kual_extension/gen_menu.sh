#!/bin/sh
# Generates menu.json from disk images in the disks/ folder.
# Creates a "Games" subfolder in KUAL with one entry per disk image.
# Run this after adding new disk images.
cd "$(dirname "$0")"

# Build the items list
ITEMS=""
PRIORITY=0

for f in disks/*.do disks/*.dsk disks/*.nib disks/*.woz disks/*.po; do
    [ ! -f "$f" ] && continue

    # Extract game name from filename (strip path and extension, replace _ with space)
    NAME=$(basename "$f" | sed 's/\.[^.]*$//' | sed 's/_/ /g')

    if [ $PRIORITY -gt 0 ]; then
        ITEMS="$ITEMS,"
    fi

    ITEMS="$ITEMS
                {\"name\": \"$NAME\", \"priority\": $PRIORITY, \"action\": \"./apple2.sh\", \"params\": \"$f\", \"exitmenu\": true, \"status\": false}"
    PRIORITY=$((PRIORITY + 1))
done

if [ $PRIORITY -eq 0 ]; then
    echo "No disk images found in disks/"
    exit 1
fi

cat > menu.json << EOF
{
    "items": [
        {
            "name": "Games",
            "items": [$ITEMS
            ]
        }
    ]
}
EOF

echo "Generated menu.json with $PRIORITY game(s)"
