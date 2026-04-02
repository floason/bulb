// floason (C) 2025
// Licensed under the MIT License.

// Internal documentation:
// - Console co-ordinates on Windows start from (0, 0), however when using ANSI
//   sequences, they should be expected to start from (1, 1) instead.

#pragma once

#include <stdbool.h>
#include <ctype.h>

#include "util.h"

#if defined WIN32
#   include "windows.h"
#   include "conio.h"

    static HANDLE stdout_handle;
    static HANDLE stdin_handle;
    static DWORD stdout_mode;
    static DWORD stdin_mode;
#elif defined __UNIX__
#   include <unistd.h>
#   include <poll.h>
#   include <termios.h>

    static struct termios stdin_mode;
#endif

#define STDIN_EMPTY -2

// Used with get_console_cursor_pos() on POSIX.
static bool block_read_stdin_char = false;
static char pre_esc_buffer[16];
static unsigned pre_esc_buffer_r = 0;
static unsigned pre_esc_buffer_w = 0;

// Enable ANSI escape sequences. Returns false on failure.
static inline bool enable_ansi_sequences()
{
#if defined WIN32
    stdout_handle = GetStdHandle(STD_OUTPUT_HANDLE);
    ASSERT(GetConsoleMode(stdout_handle, &stdout_mode), return false, 
        "Failed to get current stdout console mode: %d\n", GetLastError());
    ASSERT(SetConsoleMode(stdout_handle, stdout_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING), 
        return false, "Failed to eanble virtual terminal processing: %d\n", GetLastError());
    return true;
#elif defined __UNIX__
    // Not needed on *NIX.
    return true;
#endif

    ASSERT(false, return false, "Operating system not supported!\n");
}

// Clean the entire screen.
static inline void printf_clear_screen()
{
    printf("\x1B[2J\x1B[3J\x1B[1;1H");
}

// Windows: disables ENABLE_LINE_INPUT, which disables line buffering.
// Linux: disables canonical mode and console echo on input, which
// disables line buffering. Output buffering is also disabled.
// Returns false on failure.
static inline bool disable_line_buffering()
{
#if defined WIN32
    stdin_handle = GetStdHandle(STD_INPUT_HANDLE);
    ASSERT(GetConsoleMode(stdin_handle, &stdin_mode), return false, 
        "Failed to get current stdin console mode: %d\n", GetLastError());
    ASSERT(SetConsoleMode(stdin_handle, stdin_mode & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT)), 
        return false, "Failed to disable line input: %d\n", GetLastError());
    return true;
#elif defined __UNIX__
    struct termios temp;
    tcgetattr(STDIN_FILENO, &stdin_mode);
    memcpy(&temp, &stdin_mode, sizeof(temp));
    temp.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &temp);

    // Also disable stdout buffering.
    setbuf(stdout, NULL);

    return true;
#endif

    ASSERT(false, return false, "Operating system not supported!\n");
}

// Get the key used for backspace, only after disabling line input!
static inline int get_console_backspace_key()
{
#if defined __UNIX__
    return stdin_mode.c_cc[VERASE];
#endif

    return '\b';
}

// Get the current console cursor position (starting from 0, 0), only after
// disabling line input! Returns false on failure.
static inline bool get_console_cursor_pos(unsigned* x, unsigned* y)
{
    ASSERT(x, return false);
    ASSERT(y, return false);

#if defined WIN32
    CONSOLE_SCREEN_BUFFER_INFO info;
    ASSERT(GetConsoleScreenBufferInfo(stdout_handle, &info), return false, 
        "Failed to get cursor position: %d\n", GetLastError());
    *x = info.dwCursorPosition.X;
    *y = info.dwCursorPosition.Y;
    return true;
#elif defined __UNIX__
    // Send Device Status Report (DSR) ANSI sequence.
    block_read_stdin_char = true;
    printf("\x1B[6n");

    // If there's anything to be read before ESC, feed it into the
    // pre_esc_buffer queue. The buffer can only fit 16 characters, but
    // genuine clients should realistically not even be filling it beyond
    // a character or two.
    char c;
    while ((c = getchar()) != '\x1B')
        pre_esc_buffer[(pre_esc_buffer_w = (pre_esc_buffer_w + 1) % sizeof(pre_esc_buffer))] = c;

    // Read back the current terminal cursor position. This is sent to
    // stdin as ESC[#;#R (first # is row, second # is column)
    char num_buffer[6] = { 0 }; // Assumes pos is capped from 0-65535.
    while ((c = getchar()) != 'R')
    {
        int len = strlen(num_buffer);
        if (isdigit(c) && len < sizeof(num_buffer) - 1)
            num_buffer[len] = c;
        else if (len > 0)
        {
            *y = atoi(num_buffer) - 1;
            memset(num_buffer, '\0', sizeof(num_buffer));
        }
    }
    *x = atoi(num_buffer) - 1;

    block_read_stdin_char = false;
    return true;
#endif
    
    ASSERT(false, return false, "Operating system not supported!\n");
}

// Get the number of columns per console row, only after disabling line input!
// Returns -1 on failure.
static inline int get_num_columns_for_console()
{
#if defined WIN32
    CONSOLE_SCREEN_BUFFER_INFO info;
    ASSERT(GetConsoleScreenBufferInfo(stdout_handle, &info), return false,
        "Failed to get console info: %d\n", GetLastError());

    return info.srWindow.Right - info.srWindow.Left + 1;
#elif defined __UNIX__
    // There isn't any direct ANSI sequence for retrieving a terminal's size,
    // however we can position the cursor at an unrealistic distance away from
    // the start, and assuming the terminal clamps the cursor position, we
    // can utilise this information instead.
    int cached_x, cached_y;
    int max_columns, throwaway_y;
    get_console_cursor_pos(&cached_x, &cached_y);
    printf("\x1B[%d;65535H", cached_y + 1);
    get_console_cursor_pos(&max_columns, &throwaway_y);
    printf("\x1B[%d;%dH", cached_y + 1, cached_x + 1);
    return max_columns;
#endif

    ASSERT(false, return -1, "Operating system not supported!\n");
}

// Clear the last written character, only after disabling line input!
// Returns false only on unsupported operating system.
static inline bool clear_last_character()
{
#if defined WIN32
    CONSOLE_SCREEN_BUFFER_INFO info;
    ASSERT(GetConsoleScreenBufferInfo(stdout_handle, &info), return false,
        "Failed to get console info: %d\n", GetLastError());

    if (info.dwCursorPosition.X == 0)
    {
        COORD coord;
        coord.X = info.srWindow.Right - info.srWindow.Left;
        coord.Y = info.dwCursorPosition.Y - 1;

        ASSERT(SetConsoleCursorPosition(stdout_handle, coord), return false, 
            "Failed to set cursor position: %d\n", GetLastError());
        putc(' ', stdout);
        ASSERT(SetConsoleCursorPosition(stdout_handle, coord), return false, 
            "Failed to set cursor position: %d\n", GetLastError());
    }
    else
        printf("\b \b");

    return true;
#elif defined __UNIX__
    int cached_x, cached_y;
    get_console_cursor_pos(&cached_x, &cached_y);
    if (cached_x == 0)
    {
        int max_columns = get_num_columns_for_console();
        printf("\x1B[%d;%dH", cached_y, max_columns + 1);
        printf(" ");
        printf("\x1B[%d;%dH", cached_y, max_columns + 1);
    }
    else
        printf("\b \b");
    return true;
#endif

    ASSERT(false, return false, "Operating system not supported!\n");
}

// Clear n lines from a given vertical point on the console, only after
// disabling line input! Returns false on failure.
static inline bool clear_lines_from_y(unsigned y_pos, unsigned count)
{
#if defined WIN32
    CONSOLE_SCREEN_BUFFER_INFO info;
    ASSERT(GetConsoleScreenBufferInfo(stdout_handle, &info), return false,
        "Failed to get console info: %d\n", GetLastError());

    COORD coord;
    coord.X = 0;
    coord.Y = y_pos;
    ASSERT(SetConsoleCursorPosition(stdout_handle, coord), return false, 
        "Failed to set cursor position: %d\n", GetLastError());
    printf("%*c", (info.srWindow.Right - info.srWindow.Left + 1) * count, '\0');
    ASSERT(SetConsoleCursorPosition(stdout_handle, coord), return false, 
        "Failed to set cursor position: %d\n", GetLastError());
    
    return true;
#elif defined __UNIX__
    printf("\x1B[%d;1H", y_pos + count);
    for (int i = 0; i < count - 1; i++)
        printf("\x1B[2K\x1B[A");
    printf("\x1B[2K");
    return true;
#endif

    ASSERT(false, return false, "Operating system not supported!\n");
}

// Read each character separately from stdin, only after disabling line input!
// Returns \0 if character read is not a visible character, or on failure.
static inline int read_stdin_char()
{
#if defined WIN32
    char c;
    ASSERT(ReadFile(stdin_handle, &c, 1, NULL, NULL), return '\0',
        "Failed to read a character: %d\n", GetLastError());
    return c;
#elif defined __UNIX__
    // This code is written to work alongside get_console_cursor_pos() on Linux,
    // which (at least in my Bulb client main.c code) is called in an
    // asynchronous thread to that containing the main loop that processes user
    // input, which obviously also reads stdin as ESC[6n responds with the
    // current console cursor position using stdin.

    // If the pre_esc_buffer queue has been filled by get_console_cursor_pos(),
    // prioritise that first.
    if (pre_esc_buffer_r != pre_esc_buffer_w)
        return pre_esc_buffer[(pre_esc_buffer_r = (pre_esc_buffer_r + 1) % sizeof(pre_esc_buffer))];
    
    // Otherwise, we need to read directly from stdin. If get_console_cursor_pos()
    // is currently executing, stop for now.
    if (block_read_stdin_char)
        return STDIN_EMPTY;
    
    // Now actually try reading from stdin, however this code should time out
    // immediately so as to not stall on waiting for a character to appear in
    // stdin.
    struct pollfd pfd;
    pfd.fd = STDIN_FILENO;
    pfd.events = POLLIN;
    if (poll(&pfd, 1, 0) > 0)
        return getchar();
    else
        return STDIN_EMPTY;
#endif

    ASSERT(false, return '\0', "Operating system not supported!\n");
}

// Clean-up console settings after calling disable_line_input().
// Returns false only on unsupported operating system.
static inline bool cleanup_console_mode()
{
#if defined WIN32
    SetConsoleMode(stdout_handle, stdout_mode);
    SetConsoleMode(stdin_handle, stdin_mode);
    return true;
#elif defined __UNIX__
    tcsetattr(STDIN_FILENO, TCSANOW, &stdin_mode);
    return true;
#endif

    ASSERT(false, return false, "Operating system not supported!\n");
}