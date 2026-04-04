/*
 * mii_kindle_disks.c - Runtime disk switching overlay for Kindle
 *
 * Scans the disks directory for Apple II disk images and provides
 * an on-screen overlay to select and swap disks at runtime.
 */
#include "mii_kindle_disks.h"
#include "mii_kindle_fb.h"
#include "mii_slot.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>

static char disk_names[KINDLE_MAX_DISKS][64];
static char disk_paths[KINDLE_MAX_DISKS][256];
static int  disk_count = 0;

static int
is_disk_ext(const char *name)
{
	const char *dot = strrchr(name, '.');
	if (!dot) return 0;
	return (strcasecmp(dot, ".do") == 0 ||
		strcasecmp(dot, ".dsk") == 0 ||
		strcasecmp(dot, ".nib") == 0 ||
		strcasecmp(dot, ".woz") == 0 ||
		strcasecmp(dot, ".po") == 0);
}

int
kindle_disks_scan(void)
{
	DIR *d = opendir(KINDLE_DISKS_DIR);
	if (!d) {
		fprintf(stderr, "disks: cannot open %s\n", KINDLE_DISKS_DIR);
		disk_count = 0;
		return 0;
	}

	disk_count = 0;
	memset(disk_names, 0, sizeof(disk_names));
	memset(disk_paths, 0, sizeof(disk_paths));

	struct dirent *ent;
	while ((ent = readdir(d)) != NULL && disk_count < KINDLE_MAX_DISKS) {
		if (ent->d_name[0] == '.') continue;
		if (!is_disk_ext(ent->d_name)) continue;

		snprintf(disk_names[disk_count], sizeof(disk_names[0]),
			"%s", ent->d_name);
		snprintf(disk_paths[disk_count], sizeof(disk_paths[0]),
			"%s/%s", KINDLE_DISKS_DIR, ent->d_name);
		disk_count++;
	}
	closedir(d);

	if (disk_count >= KINDLE_MAX_DISKS)
		fprintf(stderr, "disks: warning: list truncated at %d\n", KINDLE_MAX_DISKS);

	/* Sort alphabetically (bubble sort, max 32 entries) */
	for (int i = 0; i < disk_count - 1; i++) {
		for (int j = i + 1; j < disk_count; j++) {
			if (strcasecmp(disk_names[i], disk_names[j]) > 0) {
				char tmp_name[64];
				char tmp_path[256];
				memcpy(tmp_name, disk_names[i], sizeof(tmp_name));
				memcpy(disk_names[i], disk_names[j], sizeof(tmp_name));
				memcpy(disk_names[j], tmp_name, sizeof(tmp_name));
				memcpy(tmp_path, disk_paths[i], sizeof(tmp_path));
				memcpy(disk_paths[i], disk_paths[j], sizeof(tmp_path));
				memcpy(disk_paths[j], tmp_path, sizeof(tmp_path));
			}
		}
	}

	fprintf(stderr, "disks: found %d disk images\n", disk_count);
	return disk_count;
}

int kindle_disks_count(void) { return disk_count; }
const char *kindle_disks_name(int i) { return (i >= 0 && i < disk_count) ? disk_names[i] : NULL; }
const char *kindle_disks_path(int i) { return (i >= 0 && i < disk_count) ? disk_paths[i] : NULL; }

static void
draw_disk_overlay(int selected, int scroll, int drive, int visible)
{
	int sw = kindle_fb_get_xres();
	int gh = kindle_fb_get_game_h();
	int scale = kindle_fb_get_scale();
	int fs = (scale >= 4) ? 2 : 1;
	int line_h = fs * 9;

	/* Clear game area */
	kindle_fb_rect(0, 0, sw, gh, 0xFF);

	/* Title */
	int ts = (scale >= 4) ? 3 : 2;
	const char *title = "Select Disk";
	int tw = 11 * 6 * ts;
	kindle_fb_draw_text((sw - tw) / 2, 5, title, 0x00, ts);

	/* Drive indicator */
	char drv_str[16];
	snprintf(drv_str, sizeof(drv_str), "Drive: D%d", drive + 1);
	int dw = (int)strlen(drv_str) * 6 * fs;
	kindle_fb_draw_text((sw - dw) / 2, 5 + ts * 10, drv_str, 0x60, fs);

	/* List items */
	int list_y = 5 + ts * 10 + fs * 12;
	for (int i = 0; i < visible && (scroll + i) < disk_count; i++) {
		int idx = scroll + i;
		int y = list_y + i * line_h;
		uint8_t color = 0x30;

		if (idx == selected) {
			/* Highlight selected item */
			kindle_fb_rect(10, y - 1, sw - 20, line_h, 0x00);
			color = 0xFF;
		}

		/* Item number and name */
		char line[80];
		snprintf(line, sizeof(line), "%2d. %s", idx + 1, disk_names[idx]);
		int max_chars = (sw - 40) / (6 * fs);
		if ((int)strlen(line) > max_chars)
			line[max_chars] = '\0';
		kindle_fb_draw_text(15, y, line, color, fs);
	}

	/* Footer */
	const char *footer = "Up/Dn=Select Enter=Load D=Drive ESC=Cancel";
	int fw = (int)strlen(footer) * 6 * fs;
	if (fw > sw - 10) {
		footer = "Arrows/Enter/D/ESC";
		fw = (int)strlen(footer) * 6 * fs;
	}
	kindle_fb_draw_text((sw - fw) / 2, gh - fs * 10, footer, 0x80, fs);

	kindle_fb_update();
}

int
kindle_disks_show_overlay(int *drive)
{
	if (disk_count == 0) return -1;

	int selected = 0;
	int scroll = 0;
	int drv = 0;  /* D1 by default */

	int scale = kindle_fb_get_scale();
	int fs = (scale >= 4) ? 2 : 1;
	int gh = kindle_fb_get_game_h();
	int ts = (scale >= 4) ? 3 : 2;
	int list_y = 5 + ts * 10 + fs * 12;
	int line_h = fs * 9;
	int visible = (gh - list_y - fs * 12) / line_h;
	if (visible > disk_count) visible = disk_count;
	if (visible < 1) visible = 1;

	/* Set stdin to blocking for the overlay */
	int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
	fcntl(STDIN_FILENO, F_SETFL, flags & ~O_NONBLOCK);

	draw_disk_overlay(selected, scroll, drv, visible);

	while (1) {
		unsigned char buf[8];
		int n = read(STDIN_FILENO, buf, sizeof(buf));
		if (n <= 0) {
			/* EOF or error — stdin closed, bail out */
			fcntl(STDIN_FILENO, F_SETFL, flags);
			*drive = drv;
			return -1;
		}

		for (int i = 0; i < n; i++) {
			unsigned char ch = buf[i];
			int redraw = 0;

			if (ch == 0x1B && i + 2 < n && buf[i+1] == '[') {
				/* Arrow keys */
				switch (buf[i+2]) {
				case 'A': /* Up */
					if (selected > 0) { selected--; redraw = 1; }
					break;
				case 'B': /* Down */
					if (selected < disk_count - 1) { selected++; redraw = 1; }
					break;
				}
				i += 2;
			} else if (ch == 0x1B) {
				fcntl(STDIN_FILENO, F_SETFL, flags);
				*drive = drv;
				return -1;
			} else if (ch == 0x03) {
				fcntl(STDIN_FILENO, F_SETFL, flags);
				*drive = drv;
				return -1;
			} else if (ch == 0x0D || ch == 0x0A) {
				fcntl(STDIN_FILENO, F_SETFL, flags);
				*drive = drv;
				return selected;
			} else if (ch == 'd' || ch == 'D') {
				drv = 1 - drv;
				redraw = 1;
			} else if (ch >= '1' && ch <= '9') {
				int idx = ch - '1';
				if (idx < disk_count) {
					fcntl(STDIN_FILENO, F_SETFL, flags);
					*drive = drv;
					return idx;
				}
			}

			/* Adjust scroll */
			if (selected < scroll) scroll = selected;
			if (selected >= scroll + visible) scroll = selected - visible + 1;

			if (redraw)
				draw_disk_overlay(selected, scroll, drv, visible);
		}
	}
}

int
kindle_disks_load(mii_t *mii, int drive, int disk_index)
{
	if (disk_index < 0 || disk_index >= disk_count) return -1;

	const char *path = disk_paths[disk_index];
	fprintf(stderr, "disks: loading %s into D%d\n", path, drive + 1);

	if (mii_slot_command(mii, 6, MII_SLOT_DRIVE_LOAD + drive, (void*)path) < 0) {
		fprintf(stderr, "disks: failed to load %s\n", path);
		return -1;
	}

	return 0;
}
