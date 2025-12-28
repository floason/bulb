// floason (C) 2025
// Licensed under the MIT License.

// Some of the code in this header feels a bit rough, but it works, for now.

#pragma once

#include <stdbool.h>

#include "util.h"

#if defined WIN32
#   include "windows.h"
#   include "conio.h"

    static HANDLE stdout_handle;
    static HANDLE stdin_handle;
    static DWORD stdout_mode;
    static DWORD stdin_mode;
#elif defined __UNIX__
    #include <ncurses.h>
#endif

int console_io_cached_columns = -1;
const char* console_io_input_buffer = NULL;
const char* console_io_input_prepend = NULL;

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

// Clean the entire screen. This should only be called before disable_line_buffering().
static inline void printf_clear_screen()
{
    printf("\x1B[2J\x1B[3J\x1B[0;0H");
}

// Cache the number of columns of the current console, only after disabling 
// line input! Returns false on failure.
static inline bool cache_console_columns()
{
#if defined WIN32
    CONSOLE_SCREEN_BUFFER_INFO info;
    ASSERT(GetConsoleScreenBufferInfo(stdout_handle, &info), return false,
        "Failed to get console info: %d\n", GetLastError());

    console_io_cached_columns = info.srWindow.Right - info.srWindow.Left + 1;
    return true;
#elif defined __UNIX__
    int _y;
    getmaxyx(stdscr, _y, console_io_cached_columns);
    return true;
#endif

    ASSERT(false, return false, "Operating system not supported!\n");
}

// Windows: disables ENABLE_LINE_INPUT, which disables line buffering.
// Linux: initializes ncurses
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
    // Initialize ncurses.
    initscr();
    cbreak();                           // Disable line buffering.
    keypad(stdscr, true);               // Enable function keys.
    noecho();                           // Disable character echoing.
    scrollok(stdscr, true);             // Allow scrolling.
    //mousemask(ALL_MOUSE_EVENTS, NULL);  // Enable mouse events. TODO: SCROLLBACK
    
    cache_console_columns();
    return true;
#endif

    ASSERT(false, return false, "Operating system not supported!\n");
}

// Get the key used for backspace, only after disabling line input!
static inline int get_console_backspace_key()
{
#if defined WIN32
    return '\b';
#elif defined __UNIX__
    return KEY_BACKSPACE;
#endif

    ASSERT(false, return '\0', "Operating system not supported!\n");
}

// Get the current console cursor position (starting from 0, 0), only after
// disabling line input! Returns false on failure.
static inline bool get_console_cursor_pos(unsigned* x, unsigned* y)
{
#if defined WIN32
    CONSOLE_SCREEN_BUFFER_INFO info;
    ASSERT(GetConsoleScreenBufferInfo(stdout_handle, &info), return false, 
        "Failed to get cursor position: %d\n", GetLastError());
    *x = info.dwCursorPosition.X;
    *y = info.dwCursorPosition.Y;
    return true;
#elif defined __UNIX__
    int ncurses_x;
    int ncurses_y;
    getyx(stdscr, ncurses_y, ncurses_x);
    *x = ncurses_x;
    *y = ncurses_y;
    return true;
#endif
    
    ASSERT(false, return false, "Operating system not supported!\n");
}

// Get the number of lines a string would fit in the current console window,
// only after disabling line input! Set force_columns to 0 for automatic
// console column length detection. Returns false on failure.
static inline bool get_num_lines_for_console(unsigned str_length, unsigned* lines, int force_columns)
{
#if defined WIN32
    CONSOLE_SCREEN_BUFFER_INFO info;
    ASSERT(GetConsoleScreenBufferInfo(stdout_handle, &info), return false,
        "Failed to get console info: %d\n", GetLastError());

    *lines = str_length / (info.srWindow.Right - info.srWindow.Left + 1) + 1;
    return true;
#elif defined __UNIX__
    int rows = 0;
    int columns = force_columns;
    if (columns <= 0)
        getmaxyx(stdscr, rows, columns);
    
    *lines = str_length / columns + 1;
    return true;
#endif
    
    ASSERT(false, return false, "Operating system not supported!\n");
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
    int cur_x = getcurx(stdscr);
    if (cur_x == 0)
    {
        int rows;
        int columns;
        getmaxyx(stdscr, rows, columns);

        int new_x = columns - 1;
        int new_y = getcury(stdscr) - 1;
        move(new_y, new_x);
        addch(' ');
        move(new_y, new_x);
    }
    else
    {
        int new_x = cur_x - 1;
        int cur_y = getcury(stdscr);
        move(cur_y, new_x);
        addch(' ');
        move(cur_y, new_x);
    }

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
    int rows;
    int columns;
    getmaxyx(stdscr, rows, columns);

    move(y_pos, 0);
    printw("%*c", columns * count, '\0');
    refresh();
    move(y_pos, 0);

    return true;
#endif

    ASSERT(false, return false, "Operating system not supported!\n");
}

// Write a character to stdout, only after disabling line input!
// Returns false only on unsupported operating system.
static inline bool write_stdout_char(char c)
{
#if defined WIN32
    putc(c, stdout);
    return true;
#elif defined __UNIX__
    addch(c);
    refresh();
    return true;
#endif

    ASSERT(false, return false, "Operating system not supported!\n");
}

// Print a formatted string to stdout, only after disabling line input!
// Returns false only on unsupported operating system.
static inline bool write_stdout_fstring(const char* fmt, ...)
{
    va_list argv;
    va_start(argv, fmt);
#if defined WIN32
    vprintf(fmt, argv);
#elif defined __UNIX__
    vw_printw(stdscr, fmt, argv);
    refresh();
#else
    ASSERT(false, return false, "Operating system not supported!\n");
#endif
    va_end(argv);
    return true;
}

// Read each character separately from stdin, only after disabling line input!
// Returns \0 if character read is not a visible character, or on failure.
static inline int read_stdin_char()
{
#if defined WIN32
    for (;;)
    {
        INPUT_RECORD record;
        DWORD num_events;
        ASSERT(ReadConsoleInputA(stdin_handle, &record, 1, &num_events), return '\0',
            "Failed to read a character: %d\n", GetLastError());
        if (record.EventType == KEY_EVENT && record.Event.KeyEvent.bKeyDown)
            return record.Event.KeyEvent.uChar.AsciiChar;
    }
#elif defined __UNIX__
    for (;;)
    {
        int c = getch();
        switch (c)
        {
            // Handle console resize. TODO: consider entire screen when scrollback
            // implemented
            case KEY_RESIZE:
            {
                if (console_io_input_buffer != NULL)
                {
                    // Get the length of the entire input text to clear.
                    size_t len = strlen(console_io_input_buffer);
                    if (console_io_input_prepend != NULL)
                        len += strlen(console_io_input_prepend);

                    // Clear the current drawn input buffer. 
                    unsigned lines;
                    get_num_lines_for_console(len, &lines, console_io_cached_columns);
                    clear_lines_from_y(getcury(stdscr) - lines + 1, lines);

                    // Print the input buffer again.
                    if (console_io_input_prepend != NULL)
                        write_stdout_fstring("%s%s", console_io_input_prepend, console_io_input_buffer);
                    else
                        write_stdout_fstring("%s", console_io_input_buffer);

                    // Cache the new terminal columns length.
                    cache_console_columns();
                }
            }

            // TODO: scrollback in the future
            case KEY_MOUSE:
                break;

            default:
                return c;
        }
    }
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
    endwin();
    return true;
#endif

    ASSERT(false, return false, "Operating system not supported!\n");
}