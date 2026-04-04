/*
 * mii_kindle_main.c - Apple IIe emulator for Kindle e-ink
 *
 * Main loop: boots Apple IIe, loads disk image, renders HIRES to
 * e-ink framebuffer, maps touchscreen to keyboard input.
 *
 * Usage: apple2 <disk_image.do> [disk_image_side_b.do]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/time.h>

#include "mii.h"
#include "mii_video.h"
#include "mii_sw.h"
#include "mii_slot.h"
#include "mii_kindle_fb.h"
#include "mii_kindle_input.h"

static volatile int running = 1;
static int g_frame_count = 0;

static void
sighandler(int sig)
{
	fprintf(stderr, "kindle-apple2: signal %d, frame %d\n", sig, g_frame_count);
	fflush(stderr);
	running = 0;
}

static void
crashhandler(int sig)
{
	fprintf(stderr, "kindle-apple2: CRASH signal %d, frame %d\n", sig, g_frame_count);
	fflush(stderr);
	_exit(1);
}

static int
getmsec(void)
{
	static int ts = 0;
	struct timeval tv;
	gettimeofday(&tv, NULL);
	int tc = tv.tv_usec / 1000 + 1000 * (0xFFFFF & tv.tv_sec);
	if (ts == 0) ts = tc;
	return tc - ts;
}

int
main(int argc, const char *argv[])
{
	const char *disk1_path = NULL;
	const char *disk2_path = NULL;
	int force_mono = 0;

	/* Parse arguments: optional --mono flag, then disk paths */
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--mono") == 0) {
			force_mono = 1;
		} else if (!disk1_path) {
			disk1_path = argv[i];
		} else if (!disk2_path) {
			disk2_path = argv[i];
		}
	}
	if (!disk1_path) {
		fprintf(stderr,
			"kindle-apple2 - Apple IIe emulator for Kindle\n"
			"Usage: %s [--mono] <disk1.do> [disk2.do]\n"
			"\n"
			"  --mono    Force monochrome rendering (no grayscale dithering)\n",
			argv[0]);
		return 1;
	}

	/* Extract disk directory for runtime disk swapping (Ctrl-D menu) */
	char disk_dir[256];
	strncpy(disk_dir, disk1_path, sizeof(disk_dir) - 1);
	disk_dir[sizeof(disk_dir) - 1] = '\0';
	char *last_slash = strrchr(disk_dir, '/');
	if (last_slash) *last_slash = '\0';
	else strcpy(disk_dir, ".");
	kindle_input_set_disk_dir(disk_dir);

	/* Auto-detect paired disk (Side_A → Side_B, Disk_1 → Disk_2, etc.) */
	static char auto_disk2[512];
	if (!disk2_path) {
		const char *base = last_slash ? last_slash + 1 : disk1_path;
		static const char *patterns[][2] = {
			{"Side_A", "Side_B"}, {"Side_B", "Side_A"},
			{"side_a", "side_b"}, {"side_b", "side_a"},
			{"Disk_1", "Disk_2"}, {"Disk_2", "Disk_1"},
			{"disk_1", "disk_2"}, {"disk_2", "disk_1"},
			{"_A.",    "_B."},    {"_B.",    "_A."},
			{"_1.",    "_2."},    {"_2.",    "_1."},
			{NULL, NULL}
		};
		for (int i = 0; patterns[i][0]; i++) {
			const char *found = strstr(base, patterns[i][0]);
			if (found) {
				int prefix_len = (int)(found - base);
				int pat_len = strlen(patterns[i][0]);
				snprintf(auto_disk2, sizeof(auto_disk2), "%s/%.*s%s%s",
					disk_dir, prefix_len, base,
					patterns[i][1], found + pat_len);
				if (access(auto_disk2, R_OK) == 0) {
					disk2_path = auto_disk2;
				}
				break;
			}
		}
	}

	signal(SIGINT, sighandler);
	signal(SIGTERM, sighandler);
	signal(SIGSEGV, crashhandler);
	signal(SIGBUS, crashhandler);
	signal(SIGABRT, crashhandler);
	signal(SIGFPE, crashhandler);

	/* Open log file — APPEND so script-level logs aren't overwritten */
	FILE *logf = fopen("/mnt/us/extensions/Apple2/apple2.log", "a");
	if (logf) {
		dup2(fileno(logf), STDERR_FILENO);
		fclose(logf);
	}
	/* Make stderr line-buffered so logs appear even on crash */
	setvbuf(stderr, NULL, _IOLBF, 0);

	fprintf(stderr, "kindle-apple2: starting (pid %d)\n", getpid());
	fprintf(stderr, "  Disk 1: %s\n", disk1_path);
	if (disk2_path)
		fprintf(stderr, "  Disk 2: %s\n", disk2_path);

	/* --- Initialize emulator --- */
	mii_t mii = {};
	uint32_t flags = MII_INIT_SILENT | MII_INIT_NSC;

	mii_init(&mii);

	/* Install Disk II in slot 6 */
	if (mii_slot_drv_register(&mii, 6, "disk2") < 0) {
		fprintf(stderr, "Failed to register disk2 driver\n");
		return 1;
	}

	mii_prepare(&mii, flags);

	/* Load disk images */
	if (mii_slot_command(&mii, 6, MII_SLOT_DRIVE_LOAD + 0, (void*)disk1_path) < 0) {
		fprintf(stderr, "Failed to load disk 1: %s\n", disk1_path);
		return 1;
	}
	/* Set current disk name for the swap menu header */
	{
		const char *bn = strrchr(disk1_path, '/');
		kindle_input_set_current_disk(bn ? bn + 1 : disk1_path);
	}
	if (disk2_path) {
		if (mii_slot_command(&mii, 6, MII_SLOT_DRIVE_LOAD + 1, (void*)disk2_path) < 0) {
			fprintf(stderr, "Failed to load disk 2: %s\n", disk2_path);
			/* non-fatal, continue with just disk 1 */
		}
	}

	mii_reset(&mii, true);
	mii.state = MII_RUNNING;

	/* Force monochrome only if --mono flag is set.
	 * Otherwise, use color rendering with grayscale dithering. */
	if (force_mono)
		mii.video.monochrome = 1;

	/* --- Initialize Kindle framebuffer --- */
	if (kindle_fb_init() < 0) {
		fprintf(stderr, "Failed to init framebuffer\n");
		mii_dispose(&mii);
		return 1;
	}

	fprintf(stderr, "kindle-apple2: framebuffer ok (game_h=%d)\n",
		kindle_fb_get_game_h());

	/* Initialize keyboard input (stdin from kterm) */
	kindle_input_init();

	/* Show startup hint if multi-disk game detected */
	if (disk2_path) {
		write(STDOUT_FILENO,
			"\n  Disk 2 loaded. Press Ctrl-D to swap disks.\n", 46);
		fprintf(stderr, "kindle-apple2: auto-paired disk 2: %s\n",
			disk2_path);
	}

	fprintf(stderr, "kindle-apple2: running (mode=%s)\n",
		force_mono ? "mono" : "dithered");

	/* --- Main loop --- */
	int last_update_ms = 0;
	int update_interval_ms = 150; /* ~6-7 fps for e-ink */
	uint32_t last_frame_seed = 0; /* track video changes */

	while (running && mii.state == MII_RUNNING) {
		/* Check for keyboard input */
		if (kindle_input_poll(&mii))
			break; /* quit requested */

		/* If exit prompt is showing, pause emulation and rendering */
		if (kindle_input_is_paused()) {
			usleep(50000); /* 50ms */
			continue;
		}

		/* Run a batch of Apple II instructions (~100ms of emulated time) */
		mii_run(&mii);

		/* Render to e-ink at limited framerate */
		int now = getmsec();
		if (now - last_update_ms >= update_interval_ms) {
			/* Skip rendering if video hasn't changed */
			if (mii.video.frame_seed == last_frame_seed) {
				usleep(1000);
				continue;
			}
			last_frame_seed = mii.video.frame_seed;
			last_update_ms = now;

			if (force_mono) {
				/* Monochrome path: read VRAM directly (original behavior) */
				uint16_t base = mii.video.base_addr;
				if (base != 0x2000 && base != 0x4000)
					base = 0x2000;
				const uint8_t *vram = mii.bank[0].mem;
				kindle_fb_render_hires(vram, base);
			} else {
				/* Dithered path: read from MII's color pixel buffer */
				kindle_fb_render_pixels(mii.video.pixels);
			}

			/* Trigger e-ink update */
			kindle_fb_update();

			g_frame_count++;

			/* Log status every 50 frames (~7.5 sec) */
			if (g_frame_count % 50 == 0) {
				fprintf(stderr, "kindle-apple2: frame %d, PC=$%04X, base=$%04X, state=%d, mode=%s\n",
					g_frame_count, mii.cpu.PC, mii.video.base_addr,
					mii.state, force_mono ? "mono" : "dithered");
			}
		}

		/* Small sleep to prevent burning CPU when not rendering */
		usleep(1000); /* 1ms */
	}

	fprintf(stderr, "kindle-apple2: shutting down (%d frames, PC=$%04X, state=%d)\n",
		g_frame_count, mii.cpu.PC, mii.state);

	/* --- Cleanup --- */
	kindle_input_close();
	kindle_fb_close();
	mii_dispose(&mii);

	return 0;
}
