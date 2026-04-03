/*
 * mii_kindle_save.c - Save/restore emulator state for Kindle
 *
 * Saves CPU registers, soft switch state, RAM contents, video state,
 * and Disk II controller + floppy state to a binary file.
 */
#include "mii_kindle_save.h"
#include "mii_video.h"
#include "mii_bank.h"
#include "mii_slot.h"
#include "drivers/mii_disk2.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

#define SAVE_MAGIC		0x4D494953	/* "MIIS" */
#define SAVE_VERSION	1

/*
 * Save file layout (all little-endian):
 *   Header:  magic(4) version(4) flags(4) reserved(4)
 *   CPU:     A X Y S PC P IR IRQ cycle total_cycle (fixed size block)
 *   State:   sw_state(4) mem_dirty(4) mem[256](256 bytes)
 *   Video:   base_addr(2) monochrome(1) color_mode(1) frame_seed(4)
 *   Banks:   For each bank 0..MII_BANK_COUNT-1:
 *              bank_size_pages(2) bank_data(size*256 bytes)
 *   Disk II: selected(1)
 *            For each drive 0..1:
 *              floppy.motor(1) floppy.qtrack(1) floppy.bit_position(4)
 *              floppy.stepper(1) floppy.write_protected(1)
 *              For each track 0..34:
 *                dirty(1) bit_count(4) track_data(MII_FLOPPY_MAX_TRACK_SIZE)
 */

typedef struct {
	uint32_t magic;
	uint32_t version;
	uint32_t flags;
	uint32_t reserved;
} save_header_t;

static int
write_all(FILE *f, const void *buf, size_t len)
{
	return fwrite(buf, 1, len, f) == len ? 0 : -1;
}

static int
read_all(FILE *f, void *buf, size_t len)
{
	return fread(buf, 1, len, f) == len ? 0 : -1;
}

int
kindle_save_state(mii_t *mii, const char *save_path)
{
	FILE *f = fopen(save_path, "wb");
	if (!f) {
		fprintf(stderr, "save: cannot open %s: %s\n", save_path, strerror(errno));
		return -1;
	}

	/* Header */
	save_header_t hdr = {
		.magic = SAVE_MAGIC,
		.version = SAVE_VERSION,
	};
	if (write_all(f, &hdr, sizeof(hdr)) < 0) goto fail;

	/* CPU state */
	if (write_all(f, &mii->cpu.A, 1) < 0) goto fail;
	if (write_all(f, &mii->cpu.X, 1) < 0) goto fail;
	if (write_all(f, &mii->cpu.Y, 1) < 0) goto fail;
	if (write_all(f, &mii->cpu.S, 1) < 0) goto fail;
	if (write_all(f, &mii->cpu.PC, 2) < 0) goto fail;
	if (write_all(f, &mii->cpu.P, sizeof(mii->cpu.P)) < 0) goto fail;
	if (write_all(f, &mii->cpu.IR, 1) < 0) goto fail;
	if (write_all(f, &mii->cpu.IRQ, 1) < 0) goto fail;
	if (write_all(f, &mii->cpu.cycle, 1) < 0) goto fail;
	if (write_all(f, &mii->cpu.total_cycle, 8) < 0) goto fail;

	/* Soft switches and memory map */
	if (write_all(f, &mii->sw_state, 4) < 0) goto fail;
	if (write_all(f, &mii->mem_dirty, 4) < 0) goto fail;
	/* Save the mem[] page map (256 entries, 1 byte each) */
	for (int i = 0; i < 256; i++) {
		if (write_all(f, &mii->mem[i].both, 1) < 0) goto fail;
	}

	/* Video state */
	if (write_all(f, &mii->video.base_addr, 2) < 0) goto fail;
	if (write_all(f, &mii->video.monochrome, 1) < 0) goto fail;
	if (write_all(f, &mii->video.color_mode, 1) < 0) goto fail;
	if (write_all(f, &mii->video.frame_seed, 4) < 0) goto fail;

	/* Bank memory contents */
	for (int b = 0; b < MII_BANK_COUNT; b++) {
		mii_bank_t *bank = &mii->bank[b];
		uint16_t pages = bank->size;
		if (write_all(f, &pages, 2) < 0) goto fail;
		if (bank->mem && pages > 0) {
			uint32_t sz = (uint32_t)pages * 256;
			if (write_all(f, bank->mem, sz) < 0) goto fail;
		}
	}

	/* Disk II state */
	mii_card_disk2_t *d2 = NULL;
	if (mii->slot[5].drv_priv)  /* slot 6 is index 5 (0-based) */
		d2 = (mii_card_disk2_t *)mii->slot[5].drv_priv;

	uint8_t has_disk2 = (d2 != NULL);
	if (write_all(f, &has_disk2, 1) < 0) goto fail;

	if (d2) {
		if (write_all(f, &d2->selected, 1) < 0) goto fail;
		if (write_all(f, &d2->write_register, 1) < 0) goto fail;
		if (write_all(f, &d2->head, 1) < 0) goto fail;
		if (write_all(f, &d2->lss_state, 1) < 0) goto fail;
		if (write_all(f, &d2->lss_mode, 1) < 0) goto fail;
		if (write_all(f, &d2->data_register, 1) < 0) goto fail;

		for (int drv = 0; drv < 2; drv++) {
			mii_floppy_t *fl = &d2->floppy[drv];
			if (write_all(f, &fl->motor, 1) < 0) goto fail;
			if (write_all(f, &fl->qtrack, 1) < 0) goto fail;
			if (write_all(f, &fl->bit_position, 4) < 0) goto fail;
			if (write_all(f, &fl->stepper, 1) < 0) goto fail;
			if (write_all(f, &fl->write_protected, 1) < 0) goto fail;

			for (int t = 0; t < MII_FLOPPY_TRACK_COUNT; t++) {
				uint8_t dirty = fl->tracks[t].dirty;
				if (write_all(f, &dirty, 1) < 0) goto fail;
				if (write_all(f, &fl->tracks[t].bit_count, 4) < 0) goto fail;
				if (write_all(f, fl->track_data[t],
						MII_FLOPPY_MAX_TRACK_SIZE) < 0) goto fail;
			}
		}
	}

	fclose(f);
	fprintf(stderr, "save: state saved to %s\n", save_path);
	return 0;

fail:
	fprintf(stderr, "save: write error to %s: %s\n", save_path, strerror(errno));
	fclose(f);
	return -1;
}

int
kindle_load_state(mii_t *mii, const char *save_path)
{
	FILE *f = fopen(save_path, "rb");
	if (!f)
		return -1; /* no save file, not an error */

	/* Header */
	save_header_t hdr;
	if (read_all(f, &hdr, sizeof(hdr)) < 0) goto fail;
	if (hdr.magic != SAVE_MAGIC || hdr.version != SAVE_VERSION) {
		fprintf(stderr, "save: bad header in %s (magic=%08x ver=%d)\n",
			save_path, hdr.magic, hdr.version);
		fclose(f);
		return -1;
	}

	/* CPU state */
	if (read_all(f, &mii->cpu.A, 1) < 0) goto fail;
	if (read_all(f, &mii->cpu.X, 1) < 0) goto fail;
	if (read_all(f, &mii->cpu.Y, 1) < 0) goto fail;
	if (read_all(f, &mii->cpu.S, 1) < 0) goto fail;
	if (read_all(f, &mii->cpu.PC, 2) < 0) goto fail;
	if (read_all(f, &mii->cpu.P, sizeof(mii->cpu.P)) < 0) goto fail;
	if (read_all(f, &mii->cpu.IR, 1) < 0) goto fail;
	if (read_all(f, &mii->cpu.IRQ, 1) < 0) goto fail;
	if (read_all(f, &mii->cpu.cycle, 1) < 0) goto fail;
	if (read_all(f, &mii->cpu.total_cycle, 8) < 0) goto fail;

	/* Soft switches and memory map */
	if (read_all(f, &mii->sw_state, 4) < 0) goto fail;
	if (read_all(f, &mii->mem_dirty, 4) < 0) goto fail;
	for (int i = 0; i < 256; i++) {
		if (read_all(f, &mii->mem[i].both, 1) < 0) goto fail;
	}

	/* Video state */
	if (read_all(f, &mii->video.base_addr, 2) < 0) goto fail;
	if (read_all(f, &mii->video.monochrome, 1) < 0) goto fail;
	if (read_all(f, &mii->video.color_mode, 1) < 0) goto fail;
	if (read_all(f, &mii->video.frame_seed, 4) < 0) goto fail;

	/* Bank memory contents */
	for (int b = 0; b < MII_BANK_COUNT; b++) {
		mii_bank_t *bank = &mii->bank[b];
		uint16_t pages;
		if (read_all(f, &pages, 2) < 0) goto fail;
		if (bank->mem && pages > 0 && pages == bank->size) {
			uint32_t sz = (uint32_t)pages * 256;
			if (read_all(f, bank->mem, sz) < 0) goto fail;
		} else if (pages > 0) {
			/* Skip mismatched bank data */
			fseek(f, (long)pages * 256, SEEK_CUR);
		}
	}

	/* Disk II state */
	uint8_t has_disk2;
	if (read_all(f, &has_disk2, 1) < 0) goto fail;

	if (has_disk2) {
		mii_card_disk2_t *d2 = NULL;
		if (mii->slot[5].drv_priv)
			d2 = (mii_card_disk2_t *)mii->slot[5].drv_priv;

		if (d2) {
			if (read_all(f, &d2->selected, 1) < 0) goto fail;
			if (read_all(f, &d2->write_register, 1) < 0) goto fail;
			if (read_all(f, &d2->head, 1) < 0) goto fail;
			if (read_all(f, &d2->lss_state, 1) < 0) goto fail;
			if (read_all(f, &d2->lss_mode, 1) < 0) goto fail;
			if (read_all(f, &d2->data_register, 1) < 0) goto fail;

			for (int drv = 0; drv < 2; drv++) {
				mii_floppy_t *fl = &d2->floppy[drv];
				if (read_all(f, &fl->motor, 1) < 0) goto fail;
				if (read_all(f, &fl->qtrack, 1) < 0) goto fail;
				if (read_all(f, &fl->bit_position, 4) < 0) goto fail;
				if (read_all(f, &fl->stepper, 1) < 0) goto fail;
				if (read_all(f, &fl->write_protected, 1) < 0) goto fail;

				for (int t = 0; t < MII_FLOPPY_TRACK_COUNT; t++) {
					uint8_t dirty;
					if (read_all(f, &dirty, 1) < 0) goto fail;
					fl->tracks[t].dirty = dirty;
					if (read_all(f, &fl->tracks[t].bit_count, 4) < 0) goto fail;
					if (read_all(f, fl->track_data[t],
							MII_FLOPPY_MAX_TRACK_SIZE) < 0) goto fail;
					fl->tracks[t].virgin = 0;
				}
			}
		}
		/* else: skip disk data if no driver loaded */
	}

	fclose(f);
	/* Force a video refresh */
	mii->video.frame_seed++;
	fprintf(stderr, "save: state loaded from %s\n", save_path);
	return 0;

fail:
	fprintf(stderr, "save: read error from %s: %s\n", save_path, strerror(errno));
	fclose(f);
	return -1;
}

char *
kindle_save_path_for_disk(const char *disk_path, char *buf, int buf_size)
{
	/* Extract basename */
	const char *base = disk_path;
	for (const char *p = disk_path; *p; p++) {
		if (*p == '/') base = p + 1;
	}

	/* Strip extension */
	char name[256];
	strncpy(name, base, sizeof(name) - 1);
	name[sizeof(name) - 1] = '\0';
	char *dot = strrchr(name, '.');
	if (dot) *dot = '\0';

	/* Build path */
	int n = snprintf(buf, buf_size,
		"/mnt/us/extensions/Apple2/saves/%s.sav", name);
	if (n < 0 || n >= buf_size)
		return NULL;

	return buf;
}
