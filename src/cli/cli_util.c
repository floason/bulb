// floason (C) 2026
// Licensed under the MIT License.

#include <stdatomic.h>
#include <threads.h>

#include "trie.h"
#include "bulb_macros.h"
#include "bulb_structs.h"

#include "cli_util.h"

char input_buffer[MAX_MESSAGE_LENGTH + 1]   = "";
int input_buffer_w                          = 0;
bool waiting_for_input                      = false;
bool echo_input                             = true;

// <SEQ>NAME<SEQ>\0
char name_buffer[COLOR_LENGTH + MAX_NAME_LENGTH + COLOR_LENGTH + 1];

mtx_t print_message_lock;
atomic_bool print_message_lock_busy;

struct trie* cli_cmds = NULL;
struct bulb_userinfo userinfo = { 0 };

void clear_input_buffer_on_screen()
{
    // The current line length is calculated using the following format:
    // NAME: MESSAGE
    unsigned line_length = strlen(userinfo.name) + 2 + input_buffer_w;

    // TODO: introduce in-depth text scanning for more sophisticated messages which
    // contain newlines, as currently they would be parsed as just another character.
    unsigned cursor_x, cursor_y;
    unsigned num_columns = get_num_columns_for_console();
    int lines = line_length / num_columns + 1;
    get_console_cursor_pos(&cursor_x, &cursor_y);
    clear_lines_from_y(cursor_y - lines + 1, lines);
}

void print_message(const char* message)
{   
    // Use mutex locking here to ensure that messages are outputted synchronously.
    atomic_store(&print_message_lock_busy, true);
    mtx_lock(&print_message_lock);
    
    // If input is not being echoed, the message should be outputted normally so as to
    // maximise performance.
    if (!echo_input)
    {
        printf("%s", message);
        goto finish;
    }

    // The current line length is calculated using the following format:
    // NAME: MESSAGE
    unsigned line_length = strlen(userinfo.name) + 2 + input_buffer_w;

    // Any pending client user input must be cleared first.
    if (waiting_for_input)
        clear_input_buffer_on_screen();

    // Always print the message first, which should already be terminated with a newline.
    printf("%s", message);

    // If we had to do some console cleanup beforehand, print the current 
    // input buffer again.
    if (waiting_for_input)
    {
        printf("%s: %s", name_buffer, input_buffer);
        if (line_length % get_num_columns_for_console() == 0)
            printf("\n");
    }

finish:
    mtx_unlock(&print_message_lock);
    atomic_store(&print_message_lock_busy, false);
}