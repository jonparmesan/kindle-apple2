/*
 * mii_kindle_disks.h - Runtime disk switching overlay for Kindle
 */
#pragma once

#include "mii.h"

#define KINDLE_DISKS_DIR "/mnt/us/extensions/Apple2/disks"
#define KINDLE_MAX_DISKS 32

/*
 * Scan the disks directory and build a list of available disk images.
 * Returns number of disks found.
 */
int kindle_disks_scan(void);

/* Get the number of disks found by the last scan */
int kindle_disks_count(void);

/* Get the filename of disk at index (relative path within disks dir) */
const char *kindle_disks_name(int index);

/* Get the full path of disk at index */
const char *kindle_disks_path(int index);

/*
 * Show the disk selection overlay. Returns the selected disk index,
 * or -1 if cancelled. drive is set to 0 or 1 for D1/D2.
 * This blocks until the user makes a selection.
 */
int kindle_disks_show_overlay(int *drive);

/*
 * Load a disk image into the specified drive (0 or 1).
 * Returns 0 on success, -1 on failure.
 */
int kindle_disks_load(mii_t *mii, int drive, int disk_index);
