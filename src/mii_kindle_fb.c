/*
 * mii_kindle_fb.c - Kindle e-ink framebuffer backend for MII
 *
 * Dynamically detects screen resolution and scales Apple II output to fit.
 * Based on geekmaster's kindle video player (MIT license).
 *
 * Copyright (C) 2012 geekmaster (original gmplay, MIT license)
 */
#include "mii_kindle_fb.h"

#include <fcntl.h>
#include <linux/fb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

typedef unsigned char u8;
typedef unsigned int u32;

/* E-ink update ioctl codes */
#define EU3  0x46dd
#define EU50 0x4040462e
#define EU51 0x4048462e

struct update_area_t {
	int x1, y1, x2, y2, which_fx;
	u8 *buffer;
};
struct mxcfb_rect {
	u32 top, left, width, height;
};
struct mxcfb_alt_buffer_data {
	u32 phys_addr, width, height;
	struct mxcfb_rect alt_update_region;
};
struct mxcfb_update_data {
	struct mxcfb_rect update_region;
	u32 waveform_mode, update_mode, update_marker;
	int temp;
	unsigned int flags;
	struct mxcfb_alt_buffer_data alt_buffer_data;
};
struct mxcfb_update_data51 {
	struct mxcfb_rect update_region;
	u32 waveform_mode, update_mode, update_marker;
	u32 hist_bw_waveform_mode, hist_gray_waveform_mode;
	int temp;
	unsigned int flags;
	struct mxcfb_alt_buffer_data alt_buffer_data;
};

/* Framebuffer state */
static u8 *fb0 = NULL;
static int fdFB = 0;
static u32 fb_stride = 0;
static u32 fb_xres = 0;
static u32 fb_yres = 0;
static int eupcode = 0;
static void *eupdata = NULL;
static struct update_area_t ua;
static struct mxcfb_update_data ur;
static struct mxcfb_update_data51 ur51;

/* Computed layout (set during init) */
static int scale = 2;       /* integer scale factor (for reference) */
static int game_w = 560;    /* actual game width in screen pixels */
static int game_h = 384;    /* actual game height in screen pixels */
static int game_x = 0;      /* horizontal offset */
static int game_y = 0;      /* vertical offset */

int kindle_fb_get_xres(void)  { return fb_xres; }
int kindle_fb_get_yres(void)  { return fb_yres; }
int kindle_fb_get_scale(void) { return scale; }
int kindle_fb_get_game_h(void){ return game_h; }
int kindle_fb_get_game_x(void){ return game_x; }
int kindle_fb_get_stride(void){ return fb_stride; }
uint8_t *kindle_fb_get_ptr(void) { return fb0; }

int
kindle_fb_init(void)
{
	struct fb_var_screeninfo screeninfo;

	fdFB = open("/dev/fb0", O_RDWR);
	if (fdFB < 0) {
		perror("open /dev/fb0");
		return -1;
	}

	ioctl(fdFB, FBIOGET_VSCREENINFO, &screeninfo);

	int ppb = 8 / screeninfo.bits_per_pixel;
	fb_stride = screeninfo.xres_virtual / ppb;
	fb_xres = screeninfo.xres;
	fb_yres = screeninfo.yres;
	u32 vy = screeninfo.yres_virtual;

	fprintf(stderr, "kindle_fb: %dx%d, %d bpp, stride=%d\n",
		fb_xres, fb_yres, screeninfo.bits_per_pixel, fb_stride);

	fb0 = (u8 *)mmap(0, fb_yres * fb_stride,
		PROT_READ | PROT_WRITE, MAP_SHARED, fdFB, 0);
	if (fb0 == MAP_FAILED) {
		perror("mmap fb0");
		close(fdFB);
		return -1;
	}

	/*
	 * Compute layout: game fills width, top ~55% of screen.
	 * Bottom ~45% for the 5-row on-screen keyboard.
	 * Apple II HIRES = 280x192.
	 */
	int max_game_h = fb_yres * 55 / 100;
	game_w = fb_xres;
	game_h = fb_xres * 192 / 280;
	if (game_h > max_game_h)
		game_h = max_game_h;
	game_x = 0;
	game_y = 0;
	scale = fb_xres / 280;

	fprintf(stderr, "kindle_fb: scale=%d, game=%dx%d at (%d,%d), kb starts at y=%d\n",
		scale, game_w, game_h, game_x, game_y, game_h);

	/* Set up e-ink update ioctl */
	memset(&ua, 0, sizeof(ua));
	ua.x2 = fb_xres;
	ua.y2 = fb_yres;
	ua.which_fx = 21;

	memset(&ur, 0, sizeof(ur));
	ur.update_region.width = fb_xres;
	ur.update_region.height = fb_yres;
	ur.waveform_mode = 257;
	ur.update_marker = 1;
	ur.temp = 0x1001;

	memset(&ur51, 0, sizeof(ur51));
	ur51.update_region.width = fb_xres;
	ur51.update_region.height = fb_yres;
	ur51.waveform_mode = 257;
	ur51.update_marker = 1;
	ur51.temp = 0x1001;

	if (vy > fb_yres) {
		eupcode = EU50;
		eupdata = &ur;
		ur.update_mode = 0;
		if (ioctl(fdFB, eupcode, eupdata) < 0) {
			eupcode = EU51;
			eupdata = &ur51;
		}
	} else {
		eupcode = EU3;
		eupdata = &ua;
	}

	/* Clear entire framebuffer to white */
	memset(fb0, 0xFF, fb_yres * fb_stride);

	return 0;
}

void
kindle_fb_render_hires(const uint8_t *vram, uint16_t base_addr)
{
	/*
	 * Render 280x192 Apple II HIRES to game_w x game_h screen area.
	 * Uses proportional scaling to fill the full width.
	 */
	for (int line = 0; line < 192; line++) {
		/* Apple II HIRES addressing */
		int addr = base_addr
			+ ((line & 7) << 10)
			+ (((line >> 3) & 7) << 7)
			+ ((line >> 6) * 40);

		/* Screen Y range for this Apple II line */
		int sy_start = game_y + line * game_h / 192;
		int sy_end   = game_y + (line + 1) * game_h / 192;

		for (int col = 0; col < 40; col++) {
			uint8_t byte = vram[addr + col];

			for (int bit = 0; bit < 7; bit++) {
				int pixel_on = (byte >> bit) & 1;
				uint8_t color = pixel_on ? 0x00 : 0xFF;

				int a2_x = col * 7 + bit; /* 0..279 */

				/* Screen X range for this Apple II pixel */
				int sx_start = game_x + a2_x * game_w / 280;
				int sx_end   = game_x + (a2_x + 1) * game_w / 280;

				/* Fill the scaled rectangle */
				for (int sy = sy_start; sy < sy_end && sy < (int)fb_yres; sy++) {
					int row_off = sy * fb_stride;
					for (int sx = sx_start; sx < sx_end && sx < (int)fb_xres; sx++) {
						fb0[row_off + sx] = color;
					}
				}
			}
		}
	}
}

void
kindle_fb_rect(int x, int y, int w, int h, uint8_t color)
{
	for (int dy = 0; dy < h && (y + dy) < (int)fb_yres; dy++) {
		int row = (y + dy) * fb_stride;
		for (int dx = 0; dx < w && (x + dx) < (int)fb_xres; dx++) {
			fb0[row + (x + dx)] = color;
		}
	}
}

void
kindle_fb_update(void)
{
	if (ioctl(fdFB, eupcode, eupdata) < 0) {
		system("eips ''");
	}
}

int
kindle_fb_detect_keyboard_top(void)
{
	/*
	 * kterm's keyboard is 5 rows. On Kindle screens:
	 * - Each key row is roughly screen_height / 13 pixels tall
	 * - 5 rows + gaps = about 40% of screen height
	 * - Keyboard starts at about 60% from the top
	 *
	 * Actual measurement from log: 758x1024 screen, keyboard
	 * occupies bottom ~40%. So kb_top = screen_h * 58 / 100.
	 * Subtract a small safety margin so we don't clip the top row.
	 */
	int kb_top = fb_yres * 58 / 100;

	/* Sanity: must be below game area */
	if (kb_top < game_h + 10)
		kb_top = game_h + 10;

	fprintf(stderr, "kindle_fb: keyboard top = y=%d (screen %dx%d)\n",
		kb_top, fb_xres, fb_yres);
	return kb_top;
}

void
kindle_fb_close(void)
{
	/* Clear screen */
	if (fb0 && fb0 != MAP_FAILED) {
		memset(fb0, 0xFF, fb_yres * fb_stride);
		kindle_fb_update();
		usleep(500000);
		munmap(fb0, fb_yres * fb_stride);
	}
	if (fdFB > 0)
		close(fdFB);
}
