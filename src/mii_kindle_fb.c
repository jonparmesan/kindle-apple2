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
static int fdFB = -1;
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
	if (!fb0 || !vram) return;
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
	if (!fb0) return;
	/* Clamp to framebuffer bounds */
	int x0 = x < 0 ? 0 : x;
	int y0 = y < 0 ? 0 : y;
	int x1 = x + w > (int)fb_xres ? (int)fb_xres : x + w;
	int y1 = y + h > (int)fb_yres ? (int)fb_yres : y + h;
	for (int py = y0; py < y1; py++) {
		int row = py * fb_stride;
		for (int px = x0; px < x1; px++) {
			fb0[row + px] = color;
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

/*
 * 4x4 ordered Bayer dither matrix, scaled to 0-255.
 * Produces stable spatial patterns that work well on e-ink
 * (no temporal shimmer like Floyd-Steinberg would cause).
 */
static const uint8_t bayer4[4][4] = {
	{   0, 128,  32, 160 },
	{ 192,  64, 224,  96 },
	{  48, 176,  16, 144 },
	{ 240, 112, 208,  80 },
};

void
kindle_fb_render_pixels(const uint32_t *pixels)
{
	if (!fb0 || !pixels) return;
	/*
	 * Render the MII color pixel buffer (560x384, 2x Apple II res)
	 * to the Kindle framebuffer with grayscale dithering.
	 *
	 * We sample every other pixel/line (280x192 effective) since
	 * the MII buffer is 2x and matches the Apple II's true resolution.
	 */
	for (int line = 0; line < 192; line++) {
		/* Screen Y range for this Apple II line */
		int sy_start = game_y + line * game_h / 192;
		int sy_end   = game_y + (line + 1) * game_h / 192;

		for (int col = 0; col < 280; col++) {
			/* Sample from top-left of each 2x2 block in the pixel buffer */
			uint32_t rgba = pixels[(line * 2) * 560 + (col * 2)];
			uint8_t r = rgba & 0xFF;
			uint8_t g = (rgba >> 8) & 0xFF;
			uint8_t b = (rgba >> 16) & 0xFF;

			/* BT.601 luminance (integer approximation) */
			uint8_t luma = (54 * r + 183 * g + 19 * b) >> 8;

			/* Ordered dither: offset luminance by Bayer threshold,
			 * then quantize to 4-bit (16 gray levels) */
			int dithered = luma + ((int)bayer4[line & 3][col & 3] - 128) / 4;
			if (dithered < 0) dithered = 0;
			if (dithered > 255) dithered = 255;
			uint8_t gray = dithered & 0xF0;

			/* Screen X range for this Apple II pixel */
			int sx_start = game_x + col * game_w / 280;
			int sx_end   = game_x + (col + 1) * game_w / 280;

			/* Fill the scaled rectangle */
			for (int sy = sy_start; sy < sy_end && sy < (int)fb_yres; sy++) {
				int row_off = sy * fb_stride;
				for (int sx = sx_start; sx < sx_end && sx < (int)fb_xres; sx++) {
					fb0[row_off + sx] = gray;
				}
			}
		}
	}
}

/*
 * Embedded 5x7 bitmap font for ASCII 32-126.
 * Each character is 5 columns wide; each column is 7 bits (LSB = top row).
 * Standard "Tom Thumb"-style minimal font.
 */
static const uint8_t font5x7[][5] = {
	{0x00,0x00,0x00,0x00,0x00}, /*   (space) */
	{0x00,0x00,0x5F,0x00,0x00}, /* ! */
	{0x00,0x07,0x00,0x07,0x00}, /* " */
	{0x14,0x7F,0x14,0x7F,0x14}, /* # */
	{0x24,0x2A,0x7F,0x2A,0x12}, /* $ */
	{0x23,0x13,0x08,0x64,0x62}, /* % */
	{0x36,0x49,0x55,0x22,0x50}, /* & */
	{0x00,0x05,0x03,0x00,0x00}, /* ' */
	{0x00,0x1C,0x22,0x41,0x00}, /* ( */
	{0x00,0x41,0x22,0x1C,0x00}, /* ) */
	{0x08,0x2A,0x1C,0x2A,0x08}, /* * */
	{0x08,0x08,0x3E,0x08,0x08}, /* + */
	{0x00,0x50,0x30,0x00,0x00}, /* , */
	{0x08,0x08,0x08,0x08,0x08}, /* - */
	{0x00,0x60,0x60,0x00,0x00}, /* . */
	{0x20,0x10,0x08,0x04,0x02}, /* / */
	{0x3E,0x51,0x49,0x45,0x3E}, /* 0 */
	{0x00,0x42,0x7F,0x40,0x00}, /* 1 */
	{0x42,0x61,0x51,0x49,0x46}, /* 2 */
	{0x21,0x41,0x45,0x4B,0x31}, /* 3 */
	{0x18,0x14,0x12,0x7F,0x10}, /* 4 */
	{0x27,0x45,0x45,0x45,0x39}, /* 5 */
	{0x3C,0x4A,0x49,0x49,0x30}, /* 6 */
	{0x01,0x71,0x09,0x05,0x03}, /* 7 */
	{0x36,0x49,0x49,0x49,0x36}, /* 8 */
	{0x06,0x49,0x49,0x29,0x1E}, /* 9 */
	{0x00,0x36,0x36,0x00,0x00}, /* : */
	{0x00,0x56,0x36,0x00,0x00}, /* ; */
	{0x00,0x08,0x14,0x22,0x41}, /* < */
	{0x14,0x14,0x14,0x14,0x14}, /* = */
	{0x41,0x22,0x14,0x08,0x00}, /* > */
	{0x02,0x01,0x51,0x09,0x06}, /* ? */
	{0x32,0x49,0x79,0x41,0x3E}, /* @ */
	{0x7E,0x11,0x11,0x11,0x7E}, /* A */
	{0x7F,0x49,0x49,0x49,0x36}, /* B */
	{0x3E,0x41,0x41,0x41,0x22}, /* C */
	{0x7F,0x41,0x41,0x22,0x1C}, /* D */
	{0x7F,0x49,0x49,0x49,0x41}, /* E */
	{0x7F,0x09,0x09,0x01,0x01}, /* F */
	{0x3E,0x41,0x41,0x51,0x32}, /* G */
	{0x7F,0x08,0x08,0x08,0x7F}, /* H */
	{0x00,0x41,0x7F,0x41,0x00}, /* I */
	{0x20,0x40,0x41,0x3F,0x01}, /* J */
	{0x7F,0x08,0x14,0x22,0x41}, /* K */
	{0x7F,0x40,0x40,0x40,0x40}, /* L */
	{0x7F,0x02,0x04,0x02,0x7F}, /* M */
	{0x7F,0x04,0x08,0x10,0x7F}, /* N */
	{0x3E,0x41,0x41,0x41,0x3E}, /* O */
	{0x7F,0x09,0x09,0x09,0x06}, /* P */
	{0x3E,0x41,0x51,0x21,0x5E}, /* Q */
	{0x7F,0x09,0x19,0x29,0x46}, /* R */
	{0x46,0x49,0x49,0x49,0x31}, /* S */
	{0x01,0x01,0x7F,0x01,0x01}, /* T */
	{0x3F,0x40,0x40,0x40,0x3F}, /* U */
	{0x1F,0x20,0x40,0x20,0x1F}, /* V */
	{0x7F,0x20,0x18,0x20,0x7F}, /* W */
	{0x63,0x14,0x08,0x14,0x63}, /* X */
	{0x03,0x04,0x78,0x04,0x03}, /* Y */
	{0x61,0x51,0x49,0x45,0x43}, /* Z */
	{0x00,0x00,0x7F,0x41,0x41}, /* [ */
	{0x02,0x04,0x08,0x10,0x20}, /* \ */
	{0x41,0x41,0x7F,0x00,0x00}, /* ] */
	{0x04,0x02,0x01,0x02,0x04}, /* ^ */
	{0x40,0x40,0x40,0x40,0x40}, /* _ */
	{0x00,0x01,0x02,0x04,0x00}, /* ` */
	{0x20,0x54,0x54,0x54,0x78}, /* a */
	{0x7F,0x48,0x44,0x44,0x38}, /* b */
	{0x38,0x44,0x44,0x44,0x20}, /* c */
	{0x38,0x44,0x44,0x48,0x7F}, /* d */
	{0x38,0x54,0x54,0x54,0x18}, /* e */
	{0x08,0x7E,0x09,0x01,0x02}, /* f */
	{0x08,0x14,0x54,0x54,0x3C}, /* g */
	{0x7F,0x08,0x04,0x04,0x78}, /* h */
	{0x00,0x44,0x7D,0x40,0x00}, /* i */
	{0x20,0x40,0x44,0x3D,0x00}, /* j */
	{0x00,0x7F,0x10,0x28,0x44}, /* k */
	{0x00,0x41,0x7F,0x40,0x00}, /* l */
	{0x7C,0x04,0x18,0x04,0x78}, /* m */
	{0x7C,0x08,0x04,0x04,0x78}, /* n */
	{0x38,0x44,0x44,0x44,0x38}, /* o */
	{0x7C,0x14,0x14,0x14,0x08}, /* p */
	{0x08,0x14,0x14,0x18,0x7C}, /* q */
	{0x7C,0x08,0x04,0x04,0x08}, /* r */
	{0x48,0x54,0x54,0x54,0x20}, /* s */
	{0x04,0x3F,0x44,0x40,0x20}, /* t */
	{0x3C,0x40,0x40,0x20,0x7C}, /* u */
	{0x1C,0x20,0x40,0x20,0x1C}, /* v */
	{0x3C,0x40,0x30,0x40,0x3C}, /* w */
	{0x44,0x28,0x10,0x28,0x44}, /* x */
	{0x0C,0x50,0x50,0x50,0x3C}, /* y */
	{0x44,0x64,0x54,0x4C,0x44}, /* z */
	{0x00,0x08,0x36,0x41,0x00}, /* { */
	{0x00,0x00,0x7F,0x00,0x00}, /* | */
	{0x00,0x41,0x36,0x08,0x00}, /* } */
	{0x08,0x08,0x2A,0x1C,0x08}, /* ~ */
};

void
kindle_fb_draw_text(int x, int y, const char *str, uint8_t color,
	int font_scale)
{
	if (!fb0 || !str) return;
	if (font_scale < 1) font_scale = 1;

	int char_w = 6 * font_scale; /* 5 pixels + 1 spacing */
	int cx = x;

	for (; *str; str++) {
		unsigned char ch = *str;
		if (ch < 32 || ch > 126) ch = '?';

		const uint8_t *glyph = font5x7[ch - 32];

		for (int col = 0; col < 5; col++) {
			uint8_t bits = glyph[col];
			for (int row = 0; row < 7; row++) {
				if (bits & (1 << row)) {
					/* Draw a font_scale x font_scale block */
					int px = cx + col * font_scale;
					int py = y + row * font_scale;
					kindle_fb_rect(px, py, font_scale, font_scale, color);
				}
			}
		}
		cx += char_w;
	}
}

int
kindle_fb_text_width(const char *str, int font_scale)
{
	if (!str) return 0;
	int len = 0;
	while (*str++) len++;
	return len * 6 * font_scale;
}

void
kindle_fb_draw_splash(const char *disk_name, int mono)
{
	if (!fb0) return;

	/* Clear game area to white */
	for (int y = 0; y < game_h && y < (int)fb_yres; y++) {
		memset(fb0 + y * fb_stride, 0xFF, fb_xres);
	}

	/* Extract basename from path */
	const char *base = disk_name;
	for (const char *p = disk_name; *p; p++) {
		if (*p == '/') base = p + 1;
	}

	int title_scale = scale >= 4 ? 4 : (scale >= 2 ? 3 : 2);
	int info_scale = scale >= 4 ? 3 : 2;
	int small_scale = scale >= 4 ? 2 : 1;

	/* Title: "Apple IIe" centered */
	const char *title = "Apple IIe";
	int tw = kindle_fb_text_width(title, title_scale);
	int tx = ((int)fb_xres - tw) / 2;
	int ty = game_h / 4;
	kindle_fb_draw_text(tx, ty, title, 0x00, title_scale);

	/* Disk name centered below title */
	int nw = kindle_fb_text_width(base, info_scale);
	int nx = ((int)fb_xres - nw) / 2;
	int ny = ty + title_scale * 10;
	kindle_fb_draw_text(nx, ny, base, 0x40, info_scale);

	/* "Booting from disk..." centered below */
	const char *boot_msg = "Booting from disk...";
	int bw = kindle_fb_text_width(boot_msg, small_scale);
	int bx = ((int)fb_xres - bw) / 2;
	int by = ny + info_scale * 12;
	kindle_fb_draw_text(bx, by, boot_msg, 0x80, small_scale);

	/* Mode indicator */
	const char *mode = mono ? "[MONO]" : "[GRAY]";
	int mw = kindle_fb_text_width(mode, small_scale);
	int mx = ((int)fb_xres - mw) / 2;
	int my = by + small_scale * 12;
	kindle_fb_draw_text(mx, my, mode, 0xA0, small_scale);

	kindle_fb_update();
}

void
kindle_fb_draw_error(const char *message)
{
	if (!fb0) return;

	/* Draw a centered error box */
	int bw = (int)fb_xres * 3 / 4;
	int bh = game_h / 3;
	int bx = ((int)fb_xres - bw) / 2;
	int by = (game_h - bh) / 2;

	/* White box with thick black border */
	kindle_fb_rect(bx, by, bw, bh, 0xFF);
	kindle_fb_rect(bx, by, bw, 4, 0x00);
	kindle_fb_rect(bx, by + bh - 4, bw, 4, 0x00);
	kindle_fb_rect(bx, by, 4, bh, 0x00);
	kindle_fb_rect(bx + bw - 4, by, 4, bh, 0x00);

	int err_scale = scale >= 4 ? 3 : 2;
	int msg_scale = scale >= 4 ? 2 : 1;

	/* "ERROR" title */
	const char *err_title = "ERROR";
	int ew = kindle_fb_text_width(err_title, err_scale);
	int ex = ((int)fb_xres - ew) / 2;
	int ey = by + bh / 4;
	kindle_fb_draw_text(ex, ey, err_title, 0x00, err_scale);

	/* Error message */
	int mw = kindle_fb_text_width(message, msg_scale);
	int mx = ((int)fb_xres - mw) / 2;
	/* Clamp to box width */
	if (mx < bx + 10) mx = bx + 10;
	int my = ey + err_scale * 12;
	kindle_fb_draw_text(mx, my, message, 0x30, msg_scale);

	kindle_fb_update();
}

void
kindle_fb_draw_status(const char *text)
{
	if (!fb0 || !text) return;

	/* Status bar sits between game area bottom and keyboard top */
	int kb_top = fb_yres * 58 / 100;
	if (kb_top < game_h + 5)
		kb_top = game_h + 5;

	int bar_y = game_h + 2;
	int bar_h = kb_top - game_h - 2;
	if (bar_h < 8) return; /* no room */

	/* Clear status area */
	for (int y = bar_y; y < bar_y + bar_h && y < (int)fb_yres; y++) {
		memset(fb0 + y * fb_stride, 0xFF, fb_xres);
	}

	/* Draw a thin separator line */
	kindle_fb_rect(0, bar_y, fb_xres, 1, 0xC0);

	/* Draw text centered */
	int s = (bar_h > 20) ? 2 : 1;
	int tw = kindle_fb_text_width(text, s);
	int tx = ((int)fb_xres - tw) / 2;
	int ty = bar_y + (bar_h - s * 7) / 2;
	kindle_fb_draw_text(tx, ty, text, 0x40, s);
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
	fb0 = NULL;
	if (fdFB >= 0)
		close(fdFB);
	fdFB = -1;
}
