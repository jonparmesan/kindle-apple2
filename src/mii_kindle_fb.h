/*
 * mii_kindle_fb.h - Kindle e-ink framebuffer backend for MII
 *
 * Dynamically detects screen resolution and scales to fit.
 * Works on all Kindle models (600x800 through 1236x1648).
 */
#pragma once

#include <stdint.h>

/* Initialize the Kindle framebuffer. Detects resolution, computes scaling. */
int kindle_fb_init(void);

/* Render Apple II HIRES page to the Kindle framebuffer (monochrome).
 * vram: pointer to full Apple II memory
 * base_addr: 0x2000 (page 1) or 0x4000 (page 2)
 */
void kindle_fb_render_hires(const uint8_t *vram, uint16_t base_addr);

/* Render MII color pixel buffer to Kindle framebuffer with dithering.
 * pixels: mii.video.pixels (560x384 RGBA buffer, 2x Apple II resolution)
 * Converts to grayscale with ordered Bayer dithering for e-ink.
 */
void kindle_fb_render_pixels(const uint32_t *pixels);

/* Draw a filled rectangle (absolute screen coordinates) */
void kindle_fb_rect(int x, int y, int w, int h, uint8_t color);

/* Trigger e-ink display update */
void kindle_fb_update(void);

/* Close framebuffer and clear screen */
void kindle_fb_close(void);

/* Runtime screen info (set after kindle_fb_init) */
int kindle_fb_get_xres(void);
int kindle_fb_get_yres(void);
int kindle_fb_get_scale(void);
int kindle_fb_get_game_h(void);   /* height of game area in pixels */
int kindle_fb_get_game_x(void);   /* x offset to center game */
int kindle_fb_get_stride(void);
uint8_t *kindle_fb_get_ptr(void); /* raw framebuffer pointer */

/* Scan the framebuffer to find where kterm's keyboard starts.
 * Call after kterm has rendered (sleep first). Returns Y coordinate. */
int kindle_fb_detect_keyboard_top(void);
