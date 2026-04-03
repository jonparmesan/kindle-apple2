/*
 * mii_kindle_input.c - Keyboard input via stdin only (kterm provides keyboard)
 * No touch input — all keys come from kterm's on-screen keyboard via stdin.
 */
#include "mii_kindle_input.h"
#include "mii_kindle_fb.h"

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>

static struct termios orig_termios;
static int term_setup = 0;
static int quit_requested = 0;
static int exit_prompt_showing = 0;

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
	return exit_prompt_showing;
}

void
kindle_input_close(void)
{
	if (term_setup)
		tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
}
