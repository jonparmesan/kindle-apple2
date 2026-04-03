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
	if (argc < 2) {
		fprintf(stderr,
			"kindle-apple2 - Apple IIe emulator for Kindle\n"
			"Usage: %s <disk1.do> [disk2.do]\n"
			"\n"
			"Loads an Apple II disk image and runs the emulator.\n"
			"Touch the on-screen keyboard to type.\n"
			"Press ESC to quit.\n",
			argv[0]);
		return 1;
	}

	const char *disk1_path = argv[1];
	const char *disk2_path = argc > 2 ? argv[2] : NULL;

	signal(SIGINT, sighandler);
	signal(SIGTERM, sighandler);
	signal(SIGSEGV, crashhandler);
	signal(SIGBUS, crashhandler);
	signal(SIGABRT, crashhandler);
	signal(SIGFPE, crashhandler);

	/* Open log file directly */
	FILE *logf = fopen("/mnt/us/extensions/Apple2/apple2.log", "w");
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
	if (disk2_path) {
		if (mii_slot_command(&mii, 6, MII_SLOT_DRIVE_LOAD + 1, (void*)disk2_path) < 0) {
			fprintf(stderr, "Failed to load disk 2: %s\n", disk2_path);
			/* non-fatal, continue with just disk 1 */
		}
	}

	mii_reset(&mii, true);
	mii.state = MII_RUNNING;

	/* Force monochrome mode for e-ink */
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

	fprintf(stderr, "kindle-apple2: running\n");

	/* --- Main loop --- */
	int last_update_ms = 0;
	int update_interval_ms = 150; /* ~6-7 fps for e-ink */

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
			last_update_ms = now;

			/* Get the active HIRES page base address from video state */
			uint16_t base = mii.video.base_addr;
			if (base != 0x2000 && base != 0x4000)
				base = 0x2000; /* default to page 1 */

			/* Read VRAM directly from the main bank memory */
			const uint8_t *vram = mii.bank[0].mem; /* MII_BANK_MAIN */

			/* Render the HIRES screen */
			kindle_fb_render_hires(vram, base);

			/* Trigger e-ink update (game area only — don't touch keyboard below) */
			kindle_fb_update();

			g_frame_count++;

			/* Log status every 50 frames (~7.5 sec) */
			if (g_frame_count % 50 == 0) {
				fprintf(stderr, "kindle-apple2: frame %d, PC=$%04X, base=$%04X, state=%d\n",
					g_frame_count, mii.cpu.PC, base, mii.state);
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
