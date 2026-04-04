/*
 * mii_kindle_save.c - Save/restore emulator state for Kindle
 *
 * Saves CPU registers, soft switch state, RAM contents, video state,
 * and Disk II controller + floppy state to a binary file.
 *
 * Save format assumes little-endian byte order (ARM Kindle).
 */
#include "mii_kindle_save.h"
#include "mii_video.h"
#include "mii_bank.h"
#include "mii_slot.h"
#include "drivers/mii_disk2.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>

#define SAVE_MAGIC		0x4D494953	/* "MIIS" */
#define SAVE_VERSION	2

/*
 * Save file layout (little-endian, ARM Kindle only):
 *
 *   Header:  magic(4) version(4) flags(4) reserved(4)
 *   CPU:     A(1) X(1) Y(1) S(1) PC(2) P(1) IR(1) IRQ(1) cycle(1) total_cycle(8)
 *   State:   sw_state(4) mem_dirty(4) mem[256](256 bytes)
 *   Video:   base_addr(2) monochrome(1) color_mode(1) frame_seed(4)
 *   Banks:   For each bank 0..MII_BANK_COUNT-1:
 *              bank_size_pages(2) bank_data(size*256 bytes)
 *   Disk II: has_disk2(1)
 *            If has_disk2: selected(1) write_register(1) head(1)
 *              lss_state(1) lss_mode(1) data_register(1)
 *            For each drive 0..1:
 *              motor(1) qtrack(1) bit_position(4) stepper(1) write_protected(1)
 *              For each track 0..34:
 *                dirty(1) bit_count(4) track_data(MII_FLOPPY_MAX_TRACK_SIZE)
 *              [v2+] seed_dirty(4) seed_saved(4)
 *
 * Version history:
 *   v1: Initial format
 *   v2: Added seed_dirty/seed_saved per floppy drive (dirty-track detection)
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

static int
write_u8(FILE *f, uint8_t v) { return write_all(f, &v, 1); }

static int
read_u8(FILE *f, uint8_t *v) { return read_all(f, v, 1); }

int
kindle_save_state(mii_t *mii, const char *save_path)
{
	/* Write to a temp file and rename on success to avoid
	 * leaving a corrupt .sav if we crash or run out of space. */
	char tmp_path[520];
	snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", save_path);

	FILE *f = fopen(tmp_path, "wb");
	if (!f) {
		fprintf(stderr, "save: cannot open %s: %s\n", tmp_path, strerror(errno));
		return -1;
	}

	/* Header */
	save_header_t hdr = {
		.magic = SAVE_MAGIC,
		.version = SAVE_VERSION,
	};
	if (write_all(f, &hdr, sizeof(hdr)) < 0) goto fail;

	/* CPU state — serialize P as a single byte via MII_GET_P for portability */
	if (write_all(f, &mii->cpu.A, 1) < 0) goto fail;
	if (write_all(f, &mii->cpu.X, 1) < 0) goto fail;
	if (write_all(f, &mii->cpu.Y, 1) < 0) goto fail;
	if (write_all(f, &mii->cpu.S, 1) < 0) goto fail;
	if (write_all(f, &mii->cpu.PC, 2) < 0) goto fail;
	{
		uint8_t p;
		MII_GET_P(&mii->cpu, p);
		if (write_u8(f, p) < 0) goto fail;
	}
	if (write_all(f, &mii->cpu.IR, 1) < 0) goto fail;
	if (write_all(f, &mii->cpu.IRQ, 1) < 0) goto fail;
	if (write_all(f, &mii->cpu.cycle, 1) < 0) goto fail;
	if (write_all(f, &mii->cpu.total_cycle, 8) < 0) goto fail;

	/* Soft switches and memory map */
	if (write_all(f, &mii->sw_state, 4) < 0) goto fail;
	if (write_all(f, &mii->mem_dirty, 4) < 0) goto fail;
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

	/* Disk II state — extract bitfields into plain uint8_t to avoid UB */
	mii_card_disk2_t *d2 = NULL;
	if (mii->slot[5].drv_priv)
		d2 = (mii_card_disk2_t *)mii->slot[5].drv_priv;

	uint8_t has_disk2 = (d2 != NULL);
	if (write_u8(f, has_disk2) < 0) goto fail;

	if (d2) {
		if (write_u8(f, d2->selected) < 0) goto fail;
		if (write_u8(f, d2->write_register) < 0) goto fail;
		if (write_u8(f, d2->head) < 0) goto fail;
		if (write_u8(f, d2->lss_state) < 0) goto fail;
		if (write_u8(f, d2->lss_mode) < 0) goto fail;
		if (write_u8(f, d2->data_register) < 0) goto fail;

		for (int drv = 0; drv < 2; drv++) {
			mii_floppy_t *fl = &d2->floppy[drv];
			if (write_u8(f, fl->motor) < 0) goto fail;
			if (write_u8(f, fl->qtrack) < 0) goto fail;
			if (write_all(f, &fl->bit_position, 4) < 0) goto fail;
			if (write_u8(f, fl->stepper) < 0) goto fail;
			if (write_u8(f, fl->write_protected) < 0) goto fail;

			for (int t = 0; t < MII_FLOPPY_TRACK_COUNT; t++) {
				if (write_u8(f, fl->tracks[t].dirty) < 0) goto fail;
				if (write_all(f, &fl->tracks[t].bit_count, 4) < 0) goto fail;
				if (write_all(f, fl->track_data[t],
						MII_FLOPPY_MAX_TRACK_SIZE) < 0) goto fail;
			}

			/* v2: dirty-tracking seeds */
			if (write_all(f, &fl->seed_dirty, 4) < 0) goto fail;
			if (write_all(f, &fl->seed_saved, 4) < 0) goto fail;
		}
	}

	fclose(f);

	/* Atomic replace: rename temp file over the real save path */
	if (rename(tmp_path, save_path) < 0) {
		fprintf(stderr, "save: rename %s -> %s failed: %s\n",
			tmp_path, save_path, strerror(errno));
		unlink(tmp_path);
		return -1;
	}

	fprintf(stderr, "save: state saved to %s\n", save_path);
	return 0;

fail:
	fprintf(stderr, "save: write error to %s: %s\n", tmp_path, strerror(errno));
	fclose(f);
	unlink(tmp_path);
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
	if (hdr.magic != SAVE_MAGIC) {
		fprintf(stderr, "save: bad magic in %s (%08x)\n",
			save_path, hdr.magic);
		fclose(f);
		return -1;
	}
	if (hdr.version > SAVE_VERSION) {
		fprintf(stderr, "save: version %d too new (max %d) in %s\n",
			hdr.version, SAVE_VERSION, save_path);
		fclose(f);
		return -1;
	}

	/* CPU state */
	if (read_all(f, &mii->cpu.A, 1) < 0) goto fail;
	if (read_all(f, &mii->cpu.X, 1) < 0) goto fail;
	if (read_all(f, &mii->cpu.Y, 1) < 0) goto fail;
	if (read_all(f, &mii->cpu.S, 1) < 0) goto fail;
	if (read_all(f, &mii->cpu.PC, 2) < 0) goto fail;
	{
		uint8_t p;
		if (read_u8(f, &p) < 0) goto fail;
		MII_SET_P(&mii->cpu, p);
	}
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
			if (fseek(f, (long)pages * 256, SEEK_CUR) != 0) goto fail;
		}
	}

	/* Disk II state */
	uint8_t has_disk2;
	if (read_u8(f, &has_disk2) < 0) goto fail;

	if (has_disk2) {
		mii_card_disk2_t *d2 = NULL;
		if (mii->slot[5].drv_priv)
			d2 = (mii_card_disk2_t *)mii->slot[5].drv_priv;

		/* Calculate size of v1 disk section to skip if no driver */
		size_t disk_ctrl_size = 6;
		size_t per_track = 1 + 4 + MII_FLOPPY_MAX_TRACK_SIZE;
		size_t per_drive_v1 = 1 + 1 + 4 + 1 + 1 +
			(MII_FLOPPY_TRACK_COUNT * per_track);
		size_t per_drive_v2 = per_drive_v1 + 4 + 4; /* + seed_dirty + seed_saved */
		size_t per_drive = (hdr.version >= 2) ? per_drive_v2 : per_drive_v1;
		size_t disk_total = disk_ctrl_size + 2 * per_drive;

		if (d2) {
			{
				uint8_t v;
				if (read_u8(f, &v) < 0) goto fail; d2->selected = v;
				if (read_u8(f, &v) < 0) goto fail; d2->write_register = v;
				if (read_u8(f, &v) < 0) goto fail; d2->head = v;
				if (read_u8(f, &v) < 0) goto fail; d2->lss_state = v;
				if (read_u8(f, &v) < 0) goto fail; d2->lss_mode = v;
				if (read_u8(f, &v) < 0) goto fail; d2->data_register = v;
			}

			for (int drv = 0; drv < 2; drv++) {
				mii_floppy_t *fl = &d2->floppy[drv];
				uint8_t v;
				if (read_u8(f, &v) < 0) goto fail; fl->motor = v;
				if (read_u8(f, &v) < 0) goto fail; fl->qtrack = v;
				if (read_all(f, &fl->bit_position, 4) < 0) goto fail;
				if (read_u8(f, &v) < 0) goto fail; fl->stepper = v;
				if (read_u8(f, &v) < 0) goto fail; fl->write_protected = v;

				for (int t = 0; t < MII_FLOPPY_TRACK_COUNT; t++) {
					if (read_u8(f, &v) < 0) goto fail;
					fl->tracks[t].dirty = v;
					if (read_all(f, &fl->tracks[t].bit_count, 4) < 0) goto fail;
					if (read_all(f, fl->track_data[t],
							MII_FLOPPY_MAX_TRACK_SIZE) < 0) goto fail;
					fl->tracks[t].virgin = 0;
				}

				/* v2: dirty-tracking seeds */
				if (hdr.version >= 2) {
					if (read_all(f, &fl->seed_dirty, 4) < 0) goto fail;
					if (read_all(f, &fl->seed_saved, 4) < 0) goto fail;
				} else {
					/* v1: no seed data, assume clean */
					fl->seed_dirty = fl->seed_saved;
				}
			}
		} else {
			/* No driver loaded — skip disk data */
			if (fseek(f, (long)disk_total, SEEK_CUR) != 0) goto fail;
		}
	}

	fclose(f);
	/* Force a video refresh */
	mii->video.frame_seed++;
	fprintf(stderr, "save: state loaded from %s (v%d)\n", save_path, hdr.version);
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
	snprintf(name, sizeof(name), "%s", base);
	char *dot = strrchr(name, '.');
	if (dot) *dot = '\0';

	/* Build path */
	int n = snprintf(buf, buf_size,
		"/mnt/us/extensions/Apple2/saves/%s.sav", name);
	if (n < 0 || n >= buf_size)
		return NULL;

	return buf;
}
