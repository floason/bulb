// floason (C) 2025
// Licensed under the MIT License.

// Internal documentation:
// - Console co-ordinates on Windows start from (0, 0), however when using ANSI
//   sequences, they should be expected to start from (1, 1) instead.

#pragma once

#include <stdbool.h>

#include "util.h"

#if defined WIN32
#   include "windows.h"
#   include "conio.h"
#elif defined __UNIX__
#   include <unistd.h>
#   include <poll.h>
#   include <termios.h>
#endif

#define STDIN_EMPTY -2

// Enable ANSI escape sequences. Returns false on failure.
bool enable_ansi_sequences();

// Clean the entire screen.
void printf_clear_screen();

// Move the cursor to the beginning of the next line.
void start_next_console_line();

// Windows: disables ENABLE_LINE_INPUT, which disables line buffering.
// Linux: disables canonical mode and console echo on input, which
// disables line buffering. Output buffering is also disabled.
// Returns false on failure.
bool disable_line_buffering();

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
bool clear_last_character();

// Clear n lines from a given vertical point on the console, only after
// disabling line input! Returns false on failure.
bool clear_lines_from_y(unsigned y_pos, unsigned count);

// Read each character separately from stdin, only after disabling line input!
// Returns \0 if character read is not a visible character, or on failure.
int read_stdin_char();

// Clean-up console settings after calling disable_line_input().
// Returns false only on unsupported operating system.
bool cleanup_console_mode();