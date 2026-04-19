// floason (C) 2025
// Licensed under the MIT License.

// Internal documentation:
// - Console co-ordinates on Windows start from (0, 0), however when using ANSI
//   sequences, they should be expected to start from (1, 1) instead.

// While this code is specifically purposed for Bulb's CLI binary, this header
// can be re-purposed for other projects, hence its placement in the root src/
// directory.

#pragma once

#include <stdbool.h>

#include "util.h"

#define STDIN_EMPTY -2

// Enable ANSI escape sequences. Returns false on failure.
bool enable_ansi_sequences();

// Clean the entire screen.
void printf_clear_screen();

// Windows: disables ENABLE_LINE_INPUT, which disables line buffering.
// Linux: disables canonical mode and console echo on input, which
// disables line buffering. Output buffering is also disabled and a
// signal handler is set up for SIGWINCH for retrieving the terminal size.
// Returns false on failure.
bool enable_console_io_functions();

// Get the key used for backspace, only after disabling line input!
int get_console_backspace_key();

// Get the current console cursor position (starting from 0, 0), only after
// disabling line input! Returns false on failure.
bool get_console_cursor_pos(unsigned* x, unsigned* y);

// Get the number of columns per console row, only after disabling line input!
// Returns -1 on failure.
int get_num_columns_for_console();

// Clear the last written character, only after disabling line input!
// Returns false only on unsupported operating system.
bool clear_last_character(int estimated_cursor_x);

// Clear n lines from a given vertical point on the console, only after
// disabling line input! Returns false on failure.
bool clear_lines_from_y(unsigned y_pos, unsigned count);

// Read each character separately from stdin, only after disabling line input!
// Returns \0 if character read is not a visible character, or on failure.
int read_stdin_char();

// Clean-up console settings after calling enable_console_io_functions().
// Returns false only on unsupported operating system.
bool disable_console_io_functions();