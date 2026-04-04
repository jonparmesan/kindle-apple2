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
#include <sys/stat.h>

#include "mii.h"
#include "mii_video.h"
#include "mii_sw.h"
#include "mii_slot.h"
#include "mii_kindle_fb.h"
#include "mii_kindle_input.h"
#include "mii_kindle_save.h"
#include "mii_kindle_disks.h"
#include "mii_floppy.h"

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
	int force_fresh = 0;

	/* Parse arguments: optional flags, then disk paths */
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--mono") == 0) {
			force_mono = 1;
		} else if (strcmp(argv[i], "--fresh") == 0) {
			force_fresh = 1;
		} else if (!disk1_path) {
			disk1_path = argv[i];
		} else if (!disk2_path) {
			disk2_path = argv[i];
		}
	}
	if (!disk1_path) {
		fprintf(stderr,
			"kindle-apple2 - Apple IIe emulator for Kindle\n"
			"Usage: %s [--mono] [--fresh] <disk1.do> [disk2.do]\n"
			"\n"
			"  --mono    Force monochrome rendering (no grayscale dithering)\n"
			"  --fresh   Ignore saved state, cold boot from disk\n",
			argv[0]);
		return 1;
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

	/* --- Initialize Kindle framebuffer first (so we can show errors) --- */
	int fb_ok = (kindle_fb_init() == 0);
	if (fb_ok) {
		fprintf(stderr, "kindle-apple2: framebuffer ok (game_h=%d)\n",
			kindle_fb_get_game_h());
		kindle_fb_draw_splash(disk1_path, force_mono);
	} else {
		fprintf(stderr, "Failed to init framebuffer\n");
		return 1;
	}

	/* --- Initialize emulator --- */
	mii_t mii = {};
	uint32_t flags = MII_INIT_SILENT | MII_INIT_NSC;

	mii_init(&mii);

	/* Install Disk II in slot 6 */
	if (mii_slot_drv_register(&mii, 6, "disk2") < 0) {
		fprintf(stderr, "Failed to register disk2 driver\n");
		kindle_fb_draw_error("Disk II driver failed");
		sleep(5);
		kindle_fb_close();
		return 1;
	}

	mii_prepare(&mii, flags);

	/* Load disk images */
	if (mii_slot_command(&mii, 6, MII_SLOT_DRIVE_LOAD + 0, (void*)disk1_path) < 0) {
		fprintf(stderr, "Failed to load disk 1: %s\n", disk1_path);
		kindle_fb_draw_error("Failed to load disk image");
		sleep(5);
		kindle_fb_close();
		mii_dispose(&mii);
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

	/* Force monochrome only if --mono flag is set.
	 * Otherwise, use color rendering with grayscale dithering. */
	if (force_mono)
		mii.video.monochrome = 1;

	/* Try to load saved state (unless --fresh) */
	char save_path[512];
	int have_save_path = (kindle_save_path_for_disk(
		disk1_path, save_path, sizeof(save_path)) != NULL);
	if (have_save_path) {
		if (!force_fresh && kindle_load_state(&mii, save_path) == 0) {
			/*
			 * mii_reset() set cpu_state.reset=1 which causes the CPU
			 * to fetch the reset vector instead of continuing from the
			 * saved PC. Clear it and tell the CPU to start fetching at
			 * the restored PC.
			 */
			mii.cpu_state.raw = 0;
			mii.cpu_state.addr = mii.cpu.PC;
			mii.cpu_state.sync = 1;
			fprintf(stderr, "kindle-apple2: restored from save state\n");
		}
	}

	/* Scan available disk images for runtime switching */
	kindle_disks_scan();

	/* Initialize keyboard input (stdin from kterm) */
	kindle_input_init();

	fprintf(stderr, "kindle-apple2: running (mode=%s)\n",
		force_mono ? "mono" : "dithered");

	/* --- Main loop --- */
	int last_update_ms = 0;
	int update_interval_ms = 150; /* ~6-7 fps for e-ink */
	uint32_t last_frame_seed = 0; /* track video changes */
	char last_status[64] = "";

	while (running && mii.state == MII_RUNNING) {
		/* Check for keyboard input */
		if (kindle_input_poll(&mii))
			break; /* quit requested */

		/* If exit prompt is showing, pause emulation and rendering */
		if (kindle_input_is_paused()) {
			usleep(50000); /* 50ms */
			continue;
		}

		/* Handle disk swap request (Ctrl-D) */
		if (kindle_input_disk_swap_requested() && kindle_disks_count() > 0) {
			int drive = 0;
			int sel = kindle_disks_show_overlay(&drive);
			if (sel >= 0) {
				if (kindle_disks_load(&mii, drive, sel) == 0) {
					char msg[64];
					snprintf(msg, sizeof(msg), "D%d: %s",
						drive + 1, kindle_disks_name(sel));
					kindle_fb_draw_status(msg);
					kindle_fb_update();
				} else {
					kindle_fb_draw_error("Failed to load disk");
					kindle_fb_update();
					sleep(2);
				}
			}
			/* Force full redraw after overlay */
			last_frame_seed = 0;
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

			/* Update status bar if content changed */
			{
				mii_floppy_t *f = NULL;
				mii_slot_command(&mii, 6,
					MII_SLOT_D2_GET_FLOPPY + 0, (void*)&f);
				int motor = (f && f->motor);
				char status[64];
				snprintf(status, sizeof(status), "%s  %s",
					motor ? "[DISK]" : "      ",
					force_mono ? "MONO" : "GRAY");
				if (strcmp(status, last_status) != 0) {
					kindle_fb_draw_status(status);
					strncpy(last_status, status, sizeof(last_status) - 1);
				}
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

	/* Save state if requested */
	if (kindle_input_save_requested() && have_save_path) {
		/* Ensure saves directory exists */
		mkdir("/mnt/us/extensions/Apple2/saves", 0755);
		if (kindle_save_state(&mii, save_path) == 0) {
			kindle_fb_draw_status("State saved!");
			kindle_fb_update();
			usleep(500000); /* show message briefly */
		} else {
			kindle_fb_draw_error("Save failed!");
			kindle_fb_update();
			sleep(3);
		}
	}

	/* --- Cleanup --- */
	kindle_input_close();
	kindle_fb_close();
	mii_dispose(&mii);

	return 0;
}
