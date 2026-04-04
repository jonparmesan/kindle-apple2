/*
 * mii_kindle_save.h - Save/restore emulator state for Kindle
 */
#pragma once

#include "mii.h"

#define KINDLE_SAVES_DIR "/mnt/us/extensions/Apple2/saves"

/*
 * Save complete emulator state to a file.
 * Returns 0 on success, -1 on failure.
 */
int kindle_save_state(mii_t *mii, const char *save_path);

/*
 * Restore emulator state from a file.
 * The emulator must already be initialized (mii_init + mii_slot_drv_register
 * + mii_prepare + disk loaded + mii_reset called).
 * This overwrites CPU, memory, video, and disk state from the save file.
 * Returns 0 on success, -1 on failure (file not found or version mismatch).
 */
int kindle_load_state(mii_t *mii, const char *save_path);

/*
 * Build the default save path for a given disk image path.
 * Writes to buf (max buf_size bytes). Returns buf on success, NULL on failure.
 * Format: /mnt/us/extensions/Apple2/saves/<basename_without_ext>.sav
 */
char *kindle_save_path_for_disk(const char *disk_path, char *buf, int buf_size);
