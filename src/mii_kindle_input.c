/*
 * mii_kindle_input.c - Keyboard input via stdin only (kterm provides keyboard)
 * No touch input — all keys come from kterm's on-screen keyboard via stdin.
 */
#include "mii_kindle_input.h"
#include "mii_kindle_fb.h"
#include "mii_slot.h"

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <dirent.h>

static struct termios orig_termios;
static int term_setup = 0;
static int quit_requested = 0;
static int exit_prompt_showing = 0;

/* Disk swap menu state */
static int disk_menu_showing = 0;
static int disk_menu_drive = 0;        /* target drive: 0 or 1 */
static char disk_dir[256] = {0};       /* parent directory of disk images */
static char disk_files[40][128];       /* basenames of found disk images */
static int disk_file_count = 0;
static int disk_menu_page = 0;
static char current_disk_name[128] = {0}; /* currently loaded disk basename */

void
kindle_input_set_disk_dir(const char *dir)
{
	snprintf(disk_dir, sizeof(disk_dir), "%s", dir);
	fprintf(stderr, "kindle_input: disk dir set to '%s'\n", disk_dir);
}

void
kindle_input_set_current_disk(const char *basename)
{
	snprintf(current_disk_name, sizeof(current_disk_name), "%s", basename);
}

/* Check if filename has a supported disk image extension */
static int
is_disk_image(const char *name)
{
	const char *dot = strrchr(name, '.');
	if (!dot) return 0;
	return (strcasecmp(dot, ".do") == 0 ||
		strcasecmp(dot, ".dsk") == 0 ||
		strcasecmp(dot, ".nib") == 0 ||
		strcasecmp(dot, ".woz") == 0 ||
		strcasecmp(dot, ".po") == 0);
}

/* Scan disk_dir for disk images, store in disk_files[] */
static void
scan_disk_dir(void)
{
	DIR *d = opendir(disk_dir);
	disk_file_count = 0;
	if (!d) return;

	struct dirent *ent;
	while ((ent = readdir(d)) != NULL && disk_file_count < 40) {
		if (ent->d_name[0] == '.') continue;
		if (!is_disk_image(ent->d_name)) continue;
		snprintf(disk_files[disk_file_count], 128, "%s", ent->d_name);
		disk_file_count++;
	}
	closedir(d);

	/* Simple insertion sort alphabetically */
	for (int i = 1; i < disk_file_count; i++) {
		char tmp[128];
		memcpy(tmp, disk_files[i], 128);
		int j = i - 1;
		while (j >= 0 && strcasecmp(disk_files[j], tmp) > 0) {
			memcpy(disk_files[j + 1], disk_files[j], 128);
			j--;
		}
		memcpy(disk_files[j + 1], tmp, 128);
	}
}

/* Format a display name: strip extension, replace _/- with spaces */
static void
format_display_name(const char *filename, char *out, int out_size)
{
	snprintf(out, out_size, "%s", filename);
	/* Strip extension */
	char *dot = strrchr(out, '.');
	if (dot) *dot = '\0';
	/* Replace underscores and hyphens with spaces */
	for (char *p = out; *p; p++) {
		if (*p == '_' || *p == '-') *p = ' ';
	}
}

static void
redraw_disk_menu(void)
{
	/* Draw a white box over the game area */
	int sw = kindle_fb_get_xres();
	int gh = kindle_fb_get_game_h();
	kindle_fb_rect(0, 0, sw, gh, 0xFF);
	kindle_fb_rect(0, 0, sw, 3, 0x00);
	kindle_fb_rect(0, gh - 3, sw, 3, 0x00);
	kindle_fb_rect(0, 0, 3, gh, 0x00);
	kindle_fb_rect(sw - 3, 0, 3, gh, 0x00);
	kindle_fb_update();

	/* Write menu to terminal */
	write(STDOUT_FILENO, "\033[2J\033[H", 7);

	char line[256];
	int len;

	if (current_disk_name[0]) {
		len = snprintf(line, sizeof(line),
			"\n  SWAP DISK (Drive %d)  [%s]\n\n",
			disk_menu_drive + 1, current_disk_name);
	} else {
		len = snprintf(line, sizeof(line),
			"\n  SWAP DISK (Drive %d)\n\n",
			disk_menu_drive + 1);
	}
	write(STDOUT_FILENO, line, len);

	if (disk_file_count == 0) {
		const char *msg = "  No disk images found.\n\n  ESC: Cancel\n";
		write(STDOUT_FILENO, msg, strlen(msg));
		return;
	}

	int start = disk_menu_page * 9;
	int end = start + 9;
	if (end > disk_file_count) end = disk_file_count;
	int total_pages = (disk_file_count + 8) / 9;

	for (int i = start; i < end; i++) {
		char display[96];
		format_display_name(disk_files[i], display, sizeof(display));
		len = snprintf(line, sizeof(line), "  %d) %s\n", (i - start) + 1, display);
		write(STDOUT_FILENO, line, len);
	}

	write(STDOUT_FILENO, "\n", 1);

	if (total_pages > 1) {
		len = snprintf(line, sizeof(line),
			"  Page %d/%d  Tab:Drive  >:Next  <:Prev  ESC:Cancel\n",
			disk_menu_page + 1, total_pages);
	} else {
		len = snprintf(line, sizeof(line),
			"  Tab: Drive %d  ESC: Cancel\n",
			(disk_menu_drive == 0) ? 2 : 1);
	}
	write(STDOUT_FILENO, line, len);
}

static void
show_disk_menu(void)
{
	scan_disk_dir();
	disk_menu_page = 0;
	disk_menu_drive = 0;
	disk_menu_showing = 1;
	redraw_disk_menu();
}

static void
hide_disk_menu(void)
{
	write(STDOUT_FILENO, "\033[2J\033[H", 7);
	disk_menu_showing = 0;
}

static void
handle_disk_menu_key(mii_t *mii, unsigned char ch)
{
	/* ESC → cancel */
	if (ch == 0x1B) {
		hide_disk_menu();
		return;
	}

	/* Tab → toggle target drive */
	if (ch == 0x09) {
		disk_menu_drive = !disk_menu_drive;
		redraw_disk_menu(); /* redraw without resetting page */
		return;
	}

	/* > or . → next page */
	if ((ch == '>' || ch == '.') && disk_file_count > 0) {
		int total_pages = (disk_file_count + 8) / 9;
		if (disk_menu_page < total_pages - 1) {
			disk_menu_page++;
			redraw_disk_menu();
		}
		return;
	}

	/* < or , → previous page */
	if ((ch == '<' || ch == ',') && disk_menu_page > 0) {
		disk_menu_page--;
		redraw_disk_menu();
		return;
	}

	/* 1-9 → select disk */
	if (ch >= '1' && ch <= '9') {
		int idx = disk_menu_page * 9 + (ch - '1');
		if (idx >= disk_file_count) return; /* invalid selection */

		char path[512];
		int written = snprintf(path, sizeof(path), "%s/%s",
			disk_dir, disk_files[idx]);
		if (written >= (int)sizeof(path)) {
			/* Path truncated, don't load */
			const char *msg = "\n  Error: path too long\n";
			write(STDOUT_FILENO, msg, strlen(msg));
			return;
		}

		fprintf(stderr, "kindle_input: swapping drive %d → %s\n",
			disk_menu_drive, path);

		int ret = mii_slot_command(mii, 6,
			MII_SLOT_DRIVE_LOAD + disk_menu_drive, (void*)path);

		if (ret < 0) {
			const char *msg = "\n  Load failed!\n";
			write(STDOUT_FILENO, msg, strlen(msg));
			fprintf(stderr, "kindle_input: disk load failed for %s\n", path);
			/* Stay in menu so user can try another */
			return;
		}

		/* Update current disk name */
		snprintf(current_disk_name, sizeof(current_disk_name),
			"%s", disk_files[idx]);

		hide_disk_menu();
	}
}

int
kindle_input_init(void)
{
	struct termios raw;
	if (tcgetattr(STDIN_FILENO, &orig_termios) == 0) {
		raw = orig_termios;
		raw.c_lflag &= ~(ECHO | ICANON | ISIG);
		raw.c_iflag &= ~(IXON | ICRNL);
		raw.c_cc[VMIN] = 0;
		raw.c_cc[VTIME] = 0;
		tcsetattr(STDIN_FILENO, TCSANOW, &raw);
		term_setup = 1;
	}
	fcntl(STDIN_FILENO, F_SETFL,
		fcntl(STDIN_FILENO, F_GETFL, 0) | O_NONBLOCK);

	/* Clear kterm's terminal by sending ANSI escape codes BEFORE raw mode */
	/* This hides the shell prompt and any command text */
	write(STDOUT_FILENO, "\033[2J\033[H", 7);  /* clear screen + home cursor */

	fprintf(stderr, "kindle_input: stdin raw mode, terminal cleared\n");
	return 0;
}

void
kindle_input_draw_keyboard(void)
{
	/* kterm provides the keyboard */
}

static void
show_exit_prompt(void)
{
	/* Draw a dialog box in the center of the game area */
	int sw = kindle_fb_get_xres();
	int gh = kindle_fb_get_game_h();
	int bw = sw * 2 / 3;
	int bh = gh / 4;
	int bx = (sw - bw) / 2;
	int by = (gh - bh) / 2;

	/* White box with black border */
	kindle_fb_rect(bx, by, bw, bh, 0xFF);
	kindle_fb_rect(bx, by, bw, 3, 0x00);
	kindle_fb_rect(bx, by + bh - 3, bw, 3, 0x00);
	kindle_fb_rect(bx, by, 3, bh, 0x00);
	kindle_fb_rect(bx + bw - 3, by, 3, bh, 0x00);

	/* Write prompt text to stdout so kterm renders it in the terminal */
	/* (this appears in the terminal area, not on our framebuffer) */
	/* Instead, just use the ANSI terminal output: */
	write(STDOUT_FILENO, "\033[2J\033[H", 7);
	write(STDOUT_FILENO, "\n\n   Exit to Kindle?  Y / N\n", 28);

	kindle_fb_update();
	exit_prompt_showing = 1;
}

static void
hide_exit_prompt(void)
{
	/* Clear the terminal text */
	write(STDOUT_FILENO, "\033[2J\033[H", 7);
	exit_prompt_showing = 0;
}

int
kindle_input_poll(mii_t *mii)
{
	unsigned char buf[32];
	int n = read(STDIN_FILENO, buf, sizeof(buf));

	for (int i = 0; i < n; i++) {
		unsigned char ch = buf[i];

		/* Ctrl-C → quit immediately */
		if (ch == 0x03) { quit_requested = 1; return 1; }

		/* If exit prompt is showing, only handle Y/N */
		if (exit_prompt_showing) {
			if (ch == 'y' || ch == 'Y') {
				quit_requested = 1;
				return 1;
			}
			if (ch == 'n' || ch == 'N' || ch == 0x1B) {
				hide_exit_prompt();
				continue;
			}
			continue; /* ignore other keys while prompt is up */
		}

		/* If disk menu is showing, handle menu keys */
		if (disk_menu_showing) {
			handle_disk_menu_key(mii, ch);
			continue;
		}

		/* Ctrl-D → open disk swap menu */
		if (ch == 0x04 && disk_dir[0]) {
			show_disk_menu();
			continue;
		}

		/* Arrow key escape sequences: ESC [ A/B/C/D */
		if (ch == 0x1B && i + 2 < n && buf[i+1] == '[') {
			switch (buf[i+2]) {
			case 'A': mii_keypress(mii, 0x0B); i += 2; continue;
			case 'B': mii_keypress(mii, 0x0A); i += 2; continue;
			case 'C': mii_keypress(mii, 0x15); i += 2; continue;
			case 'D': mii_keypress(mii, 0x08); i += 2; continue;
			}
		}

		/* ESC alone → show exit prompt */
		if (ch == 0x1B) { show_exit_prompt(); continue; }

		/* Newline → carriage return */
		if (ch == 0x0A) ch = 0x0D;

		/* Lowercase → uppercase (Apple II) */
		if (ch >= 'a' && ch <= 'z') ch = ch - 'a' + 'A';

		mii_keypress(mii, ch);
	}

	return quit_requested;
}

int
kindle_input_is_paused(void)
{
	return exit_prompt_showing || disk_menu_showing;
}

void
kindle_input_close(void)
{
	if (term_setup)
		tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
}
