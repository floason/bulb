// floason (C) 2026
// Licensed under the MIT License.

// Internal documentation:
// - Console co-ordinates on Windows start from (0, 0), however when using ANSI
//   sequences, they should be expected to start from (1, 1) instead.

// While this code is specifically purposed for Bulb's CLI binary, this header
// can be re-purposed for other projects, hence its placement in the root src/
// directory.

#include <stdbool.h>
#include <ctype.h>
#include <threads.h>
#include <errno.h>

#include "console_io.h"
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
#   include <signal.h>
#   include <sys/ioctl.h>

    static struct termios stdin_mode;
    static struct sigaction sigwinch_action;
    static struct sigaction sigwinch_old;
    static struct winsize term_size;
    
    // Cache the current executing terminal's number of rows and columns,
    // using a signal handler for SIGWINCH.
    void sigwinch_handler(int signum)
    {
        ASSERT(ioctl(STDIN_FILENO, TIOCGWINSZ, &term_size) != -1, return, 
            "Could not call tcgetwinsize(): %d", errno);
    }
#endif

// Used with get_console_cursor_pos() on POSIX.
static mtx_t block_getchar;
static char pre_esc_buffer[16];
static unsigned pre_esc_buffer_r = 0;
static unsigned pre_esc_buffer_w = 0;

// Enable ANSI escape sequences. Returns false on failure.
bool enable_ansi_sequences()
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
void printf_clear_screen()
{
    printf("\x1B[2J\x1B[3J\x1B[1;1H");
}

// Windows: disables ENABLE_LINE_INPUT, which disables line buffering.
// Linux: disables canonical mode and console echo on input, which
// disables line buffering. Output buffering is also disabled and a
// signal handler is set up for SIGWINCH for retrieving the terminal size.
// Returns false on failure.
bool enable_console_io_functions()
{
    mtx_init(&block_getchar, mtx_plain);

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

    // Set up a signal handler for SIGWINCH for caching the current
    // terminal size.
    memset(&sigwinch_action, 0, sizeof(sigwinch_action));
    sigwinch_action.sa_handler = sigwinch_handler;
    sigaction(SIGWINCH, &sigwinch_action, &sigwinch_old);
    raise(SIGWINCH);
    return true;
#endif

    ASSERT(false, return false, "Operating system not supported!\n");
}

// Get the key used for backspace, only after disabling line input!
int get_console_backspace_key()
{
#if defined __UNIX__
    return stdin_mode.c_cc[VERASE];
#endif

    return '\b';
}

// Get the current console cursor position (starting from 0, 0), only after
// disabling line input! Returns false on failure.
bool get_console_cursor_pos(unsigned* x, unsigned* y)
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
    mtx_lock(&block_getchar);
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

    mtx_unlock(&block_getchar);
    return true;
#endif
    
    ASSERT(false, return false, "Operating system not supported!\n");
}

// Get the number of columns per console row, only after disabling line input!
// Returns -1 on failure.
int get_num_columns_for_console()
{
#if defined WIN32
    CONSOLE_SCREEN_BUFFER_INFO info;
    ASSERT(GetConsoleScreenBufferInfo(stdout_handle, &info), return false,
        "Failed to get console info: %d\n", GetLastError());

    return info.srWindow.Right - info.srWindow.Left + 1;
#elif defined __UNIX__
    return term_size.ws_col;
#endif

    ASSERT(false, return -1, "Operating system not supported!\n");
}

// Clear the last written character, only after disabling line input!
// Returns false only on unsupported operating system.
bool clear_last_character(int estimated_input_buffer_length)
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
    // estimated_input_buffer_length (if positive) can be used over 
    // get_console_cursor_pos(), which is orders of magnitude fasters,
    // at the cost of limited accuracy.
    bool start_of_line = false;
    int max_columns = get_num_columns_for_console();
    if (estimated_input_buffer_length < 0)
    {
        unsigned cached_x, cached_y;
        get_console_cursor_pos(&cached_x, &cached_y);
        start_of_line = (cached_x == 0);
    }
    else
        start_of_line = (estimated_input_buffer_length % max_columns == 0);

    if (start_of_line)
        printf("\x1B[F\x1B[%dG \x1B[%dG", max_columns, max_columns);
    else
        printf("\b \b");
    return true;
#endif

    ASSERT(false, return false, "Operating system not supported!\n");
}

// Clear n lines from a given vertical point on the console, only after
// disabling line input! Returns false on failure.
bool clear_lines_from_y(unsigned y_pos, unsigned count)
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
int read_stdin_char()
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

    // Now actually try reading from stdin, however this code should time out
    // immediately so as to not stall on waiting for a character to appear in
    // stdin. If a character is found in the stdin filestream, it should still
    // return STDIN_EMPTY if get_console_cursor_pos() is currently executing.
    struct pollfd pfd;
    pfd.fd = STDIN_FILENO;
    pfd.events = POLLIN;
    if (poll(&pfd, 1, 0) > 0 && !mtx_trylock(&block_getchar))
    {
        char c = getchar();
        mtx_unlock(&block_getchar);
        return c;
    }
    else
        return STDIN_EMPTY;
#endif

    ASSERT(false, return '\0', "Operating system not supported!\n");
}

// Clean-up console settings after calling enable_console_io_functions().
// Returns false only on unsupported operating system.
bool disable_console_io_functions()
{
#if defined WIN32
    SetConsoleMode(stdout_handle, stdout_mode);
    SetConsoleMode(stdin_handle, stdin_mode);
    return true;
#elif defined __UNIX__
    tcsetattr(STDIN_FILENO, TCSANOW, &stdin_mode);
    sigaction(SIGWINCH, &sigwinch_old, NULL);
    return true;
#endif

    ASSERT(false, return false, "Operating system not supported!\n");
}