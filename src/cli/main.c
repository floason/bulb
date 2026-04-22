// floason (C) 2026
// Licensed under the MIT License.

// This may not possibly be the cleanest organisation, but it works.

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <memory.h>
#include <string.h>
#include <ctype.h>
#include <threads.h>
#include <stdatomic.h>

#include "trie.h"
#include "bulb_version.h"
#include "bulb_structs.h"
#include "bulb_client.h"
#include "bulb_server.h"
#include "console_io.h"

#include "cli_util.h"
#include "cli_client.h"
#include "cli_server.h"

#define CLI_PRINT_CMD_ERROR(CMD, ERROR_MSG)                                                         \
    {                                                                                               \
        fprintf(stderr, "%s for option: %s\nFor help on commands, type \"bulb -h\"\n", ERROR_MSG,   \
            CMD->name);                                                                             \
        return false;                                                                               \
    }

static bool is_server = false;
static const char* custom_host = NULL;
static uint16_t port = BULB_USE_DEFAULT_PORT;

struct cli_cmd;

// Used internally for command-line options. On returning false, the entire process will exit.
typedef bool (*cli_cmd_func)(struct cli_cmd* cmd, const char* argument);

struct cli_cmd
{
    cli_cmd_func func;
    const char* name;
    const char* desc;
    const char* arg_name;   // Set to NULL to disable.
};
static unsigned max_cmd_len;

static void _cli_add_cmd(const char* name, const char* desc, cli_cmd_func func, const char* arg_name)
{
    trie_add_copy(cli_cmds, 
                  name, 
                  (void*)&((struct cli_cmd){ func, name, desc, arg_name }), 
                  sizeof(struct cli_cmd));
    max_cmd_len = MAX(max_cmd_len, strlen(name) + ((arg_name != NULL) ? (strlen(arg_name) + 1) : 0));
}

static bool _cli_cmd_help(struct cli_cmd* cmd, const char* argument)
{
    printf("[BULB CLI] ");
    bulb_printver();
    printf("usage: bulb [option] ... [--server [server_option] ...]\n\noptions:\n");
    
    TRIE_DFS(cli_cmds, node, 
    {
        // Separate into separate categories for different groups of commands.
        struct cli_cmd* cmd = (struct cli_cmd*)node;
        if (strcmp(cmd->name, "--server") == 0)
            printf("\nserver mode:\n");
        else if (strncmp(cmd->name, "--client", 8) == 0)
            printf("\nclient mode:\n");

        // -o            description
        // --option      description
        // --option arg  description
        printf(" %s", cmd->name);
        if (cmd->arg_name != NULL)
            printf(" %s", cmd->arg_name);
        printf("%*c %s\n", 
            (int)(max_cmd_len 
                - strlen(cmd->name) 
                - ((cmd->arg_name != NULL) ? (strlen(cmd->arg_name) + 1) : 0) 
                + 1), 
            ' ', 
            cmd->desc);
    });
    
    return false;
}

static bool _cli_cmd_port(struct cli_cmd* cmd, const char* argument)
{   
    char* end;
    port = strtol(argument, &end, 10);
    if (*end != '\0')
        CLI_PRINT_CMD_ERROR(cmd, "Expected integer");
    return true;
}

static bool _cli_cmd_name(struct cli_cmd* cmd, const char* argument)
{
    strncpy(userinfo.name, argument, sizeof(userinfo.name));
    return true;
}

static bool _cli_cmd_desc(struct cli_cmd* cmd, const char* argument)
{
    strncpy(userinfo.description, argument, sizeof(userinfo.description));
    return true;
}

static bool _cli_cmd_host(struct cli_cmd* cmd, const char* argument)
{   
    custom_host = argument;
    return true;
}

static bool _cli_cmd_disable_input(struct cli_cmd* cmd, const char* argument)
{
    echo_input = false;
    return true;
}

static bool _cli_cmd_server(struct cli_cmd* cmd, const char* argument)
{
    is_server = true;
    return true;
}

int main(int argc, char** argv)
{
    int return_value = 0;
    struct bulb_client* client = NULL;
    struct bulb_server* server = NULL;

    mtx_init(&print_message_lock, mtx_plain | mtx_recursive);
    strcpy(userinfo.description, "using Bulb CLI");

    cli_cmds = trie_new();
    _cli_add_cmd("-h", "lists all Bulb CLI commands", _cli_cmd_help, NULL);
    _cli_add_cmd("-p", "use specific port (default: 32765)", _cli_cmd_port, "port");
    _cli_add_cmd("-n", "set username", _cli_cmd_name, "name");
    _cli_add_cmd("-d", "set description", _cli_cmd_desc, "description");
    _cli_add_cmd("--host", "connect to specific host address", _cli_cmd_host, "address");
    _cli_add_cmd("--disable-echo-input", "do not display input while typing (default: echoing on)",
        _cli_cmd_disable_input, NULL);
    _cli_add_cmd("--server", "launch Bulb CLI in server mode (default: client mode)", _cli_cmd_server, 
        NULL);

    // Parse any given command line parameters.
    struct cli_cmd* identified_cmd = NULL;
    const char* identified_arg = NULL;
    for (int i = 1; i < argc; i++)
    {
        if (identified_cmd != NULL)
            identified_arg = argv[i];
        else
        {
            // Identify the current option the user has passed.
            identified_cmd = (struct cli_cmd*)trie_find(cli_cmds, argv[i]);
            if (identified_cmd == NULL)
            {
                printf("Unknown option: %s\nFor help on commands, type \"bulb -h\"\n", argv[i]);
                goto finish;
            }

            // If the specified option requires an argument, continue the loop first.
            if (identified_cmd->arg_name != NULL)
                continue;
        }

        if (!identified_cmd->func(identified_cmd, identified_arg))
            goto fail;
        identified_cmd = NULL;
        identified_arg = NULL;
    }
    
    // Exit if there is a missing argument for the pending command.
    if (identified_cmd != NULL)
    {
        fprintf(stderr, "Missing argument for option: %s\nFor help on commands, type \"bulb -h\"\n", 
            identified_cmd->name);
        goto finish;
    }

    enable_ansi_sequences();
    printf_clear_screen();
    
    if (is_server)
    {
        // Running as a server.
        printf("[SERVER] ");
        bulb_printver();
        ASSERT(server = cli_server_init(port), goto fail);

        // This is done after calling cli_server_init() so that the server name displayed
        // when using the status command is not "[SERVER]".
        strcpy(userinfo.name, "[SERVER]");
    }
    else
    {
        // Running as a client.
        printf("[CLIENT] ");
        bulb_printver();
        ASSERT(client = cli_client_init(custom_host, port), goto fail);
    }

    // Wait for user input. From this point, the custom console_io.h functions must be used
    // for stdin/stdout.
    snprintf(name_buffer, sizeof(name_buffer), "%s%s%s", COLOR_RED, userinfo.name, COLOR_DEFAULT);
    waiting_for_input = true;
new_iteration:
    if (echo_input)
        printf("%s: ", name_buffer);
    for (;;)
    {
        // Wait for next user input. This doubles as a busywait loop on POSIX
        // systems, as the use of ESC[6n in an asynchronous thread conflicts with
        // this loop repeatedly blocking for user input itself.
        while (atomic_load(&print_message_lock_busy));
        int c = read_stdin_char();
        if (c == STDIN_EMPTY)
            continue;

        // Parse the inputted character.
        mtx_lock(&print_message_lock);
        switch (c)
        {
            // Reject NUL.
            case '\0':
                break;

            // Handle EOF.
            case EOF:
                printf("\n\nEOF reached; was this intentional?");
                goto finish;

            // Handle enter.
            case '\n':
            case '\r':
            {
                // Print a new line to mark the beginning of the next message only if
                // the terminal cursor is not at the beginning of a new line already.
                if (echo_input
                    && (strlen(userinfo.name) + 2 + input_buffer_w) % get_num_columns_for_console() > 0)
                    printf("\n");

                // If input is not echoed while being typed, print the input buffer now.
                if (!echo_input)
                    printf("%s: %s\n", name_buffer, input_buffer);

                bool cmd_success, func_success;
                waiting_for_input = false;
                if (is_server)
                    func_success = server_input(server, input_buffer, &cmd_success);
                else
                    func_success = client_input(client, input_buffer, &cmd_success);

                if (func_success)
                {
                    if (cmd_success)
                        printf("%sCommand executed successfully!%s\n", COLOR_GREEN, COLOR_DEFAULT);
                    else
                        printf("%sCould not find command.%s\n", COLOR_RED, COLOR_DEFAULT);
                }
                memset(input_buffer, '\0', sizeof(input_buffer));
                input_buffer_w = 0;
                
                waiting_for_input = true;
                mtx_unlock(&print_message_lock);
                goto new_iteration;
            }
            
            // Visible characters should be written to the input buffer, unless
            // this is the backspace key, which is evaluated at runtime and
            // therefore must be checked here.
            default:

                if (c == get_console_backspace_key())
                {
                    if (input_buffer_w > 0)
                    {
                        if (echo_input)
                            clear_last_character(strlen(userinfo.name) + 2 + input_buffer_w);
                        input_buffer[--input_buffer_w] = '\0';
                    }
                }
                else if (input_buffer_w < sizeof(input_buffer) - 1)
                {
                    // Only visible characters should be added to the input buffer,
                    // although they will still be printed to the console so as to
                    // not cause weird disparities in user input.
                    if (isprint(c))
                        input_buffer[input_buffer_w++] = c;
                    if (!echo_input)
                        continue;
                    putchar(c);

                    // Due to backwards-compatibility with VT100 functionality,
                    // inputting the last character that fits on the current column
                    // row does not actually send the cursor to the next line. This
                    // wrapping behaviour only occurs when the next inputted
                    // character only fits on the next line instead. Thus, the 
                    // former functionality must be handled manually.
                    if ((strlen(userinfo.name) + 2 + input_buffer_w) % get_num_columns_for_console() == 0)
                        printf("\n");
                }
                break;
        }
        mtx_unlock(&print_message_lock);
    }

fail:
    return_value = 1;
finish:
    if (is_server)
        cli_server_cleanup(server);
    else
        cli_client_cleanup(client);
    return return_value;
}