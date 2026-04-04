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

/* Input state machine: clear transitions, no scattered booleans */
typedef enum {
	INPUT_RUNNING,
	INPUT_EXIT_PROMPT,
	INPUT_QUIT,
	INPUT_SAVE_AND_QUIT,
} input_state_t;

static struct termios orig_termios;
static int term_setup = 0;
static input_state_t input_state = INPUT_RUNNING;
static int disk_swap_requested = 0;	/* one-shot flag, orthogonal to quit state */

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

	/* Clear kterm's terminal by sending ANSI escape codes */
	write(STDOUT_FILENO, "\033[2J\033[H", 7);

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
	int bh = gh / 3;
	int bx = (sw - bw) / 2;
	int by = (gh - bh) / 2;

	/* White box with black border */
	kindle_fb_rect(bx, by, bw, bh, 0xFF);
	kindle_fb_rect(bx, by, bw, 3, 0x00);
	kindle_fb_rect(bx, by + bh - 3, bw, 3, 0x00);
	kindle_fb_rect(bx, by, 3, bh, 0x00);
	kindle_fb_rect(bx + bw - 3, by, 3, bh, 0x00);

	/* Render prompt text directly on the framebuffer */
	int scale = kindle_fb_get_scale();
	int ts = (scale >= 4) ? 3 : 2;
	int ss = (scale >= 4) ? 2 : 1;

	const char *line1 = "Exit to Kindle?";
	const char *line2 = "S = Save & Exit";
	const char *line3 = "Y = Exit   N = Resume";
	int l1w = kindle_fb_text_width(line1, ts);
	int l2w = kindle_fb_text_width(line2, ss);
	int l3w = kindle_fb_text_width(line3, ss);
	int l1x = bx + (bw - l1w) / 2;
	int l2x = bx + (bw - l2w) / 2;
	int l3x = bx + (bw - l3w) / 2;
	int l1y = by + bh / 5;
	int l2y = by + bh / 2 - ss * 3;
	int l3y = by + bh * 4 / 5 - ss * 7;

	kindle_fb_draw_text(l1x, l1y, line1, 0x00, ts);
	kindle_fb_draw_text(l2x, l2y, line2, 0x40, ss);
	kindle_fb_draw_text(l3x, l3y, line3, 0x40, ss);

	kindle_fb_update();
	input_state = INPUT_EXIT_PROMPT;
}

int
kindle_input_poll(mii_t *mii)
{
	unsigned char buf[32];
	int n = read(STDIN_FILENO, buf, sizeof(buf));

	for (int i = 0; i < n; i++) {
		unsigned char ch = buf[i];

		/* Ctrl-C → quit immediately regardless of state */
		if (ch == 0x03) {
			input_state = INPUT_QUIT;
			return 1;
		}

		/* Exit prompt modal: only S/Y/N/ESC are valid */
		if (input_state == INPUT_EXIT_PROMPT) {
			if (ch == 's' || ch == 'S') {
				input_state = INPUT_SAVE_AND_QUIT;
				return 1;
			}
			if (ch == 'y' || ch == 'Y') {
				input_state = INPUT_QUIT;
				return 1;
			}
			if (ch == 'n' || ch == 'N' || ch == 0x1B) {
				input_state = INPUT_RUNNING;
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

		/* Ctrl-D → request disk swap */
		if (ch == 0x04) { disk_swap_requested = 1; continue; }

		/* ESC alone → show exit prompt */
		if (ch == 0x1B) { show_exit_prompt(); continue; }

		/* Newline → carriage return */
		if (ch == 0x0A) ch = 0x0D;

		/* Lowercase → uppercase (Apple II) */
		if (ch >= 'a' && ch <= 'z') ch = ch - 'a' + 'A';

		mii_keypress(mii, ch);
	}

	return input_state == INPUT_QUIT || input_state == INPUT_SAVE_AND_QUIT;
}

int
kindle_input_is_paused(void)
{
	return input_state == INPUT_EXIT_PROMPT;
}

int
kindle_input_save_requested(void)
{
	return input_state == INPUT_SAVE_AND_QUIT;
}

int
kindle_input_disk_swap_requested(void)
{
	int r = disk_swap_requested;
	disk_swap_requested = 0;
	return r;
}

void
kindle_input_close(void)
{
	if (term_setup)
		tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
}
