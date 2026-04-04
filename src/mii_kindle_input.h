/*
 * mii_kindle_input.h - Keyboard input via stdin (for use with kterm)
 */
#pragma once

#include "mii.h"

int kindle_input_init(void);
void kindle_input_draw_keyboard(void);
int kindle_input_poll(mii_t *mii);
void kindle_input_close(void);
int kindle_input_is_paused(void);  /* 1 if exit prompt is showing */
int kindle_input_save_requested(void); /* 1 if user chose Save & Exit */
int kindle_input_disk_swap_requested(void); /* 1 if Ctrl-D pressed, auto-clears */
