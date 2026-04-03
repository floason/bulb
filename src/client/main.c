// floason (C) 2025
// Licensed under the MIT License.

#include <stdio.h>
#include <stdbool.h>
#include <memory.h>
#include <string.h>
#include <ctype.h>

#include "bulb_version.h"
#include "client.h"         // MUST BE INCLUED BEFORE "console_io.h"!
#include "console_io.h"

#define COLOR_LENGTH                                11 // \x1B[38:5:###m

static char input_buffer[MAX_MESSAGE_LENGTH + 1]    = "";
static int input_buffer_w                           = 0;
static bool waiting_for_input                       = false;

static const char* COLOR_LIGHT_CYAN                 = "\x1B[38:5:159m";
static const char* COLOR_RED                        = "\x1B[38:5:1m";
static const char* COLOR_GREEN                      = "\x1B[38:5:2m";
static const char* COLOR_DEFAULT                    = "\x1B[39m";

// <SEQ>NAME<SEQ>\0
static char name_buffer[COLOR_LENGTH + MAX_NAME_LENGTH + COLOR_LENGTH + 1];

static void _cleanup(struct bulb_client* client)
{
    cleanup_console_mode();
    client_free(client);
}

static void _print_message(struct bulb_client* client, const char* message)
{
    // The current line length is calculated using the following format:
    // NAME: MESSAGE
    unsigned line_length = strlen(client->local_node->userinfo->name) + 2 + input_buffer_w;

    // If this client is ready to type input, we need to clear that off 
    // the screen fines.
    unsigned cursor_x;
    unsigned cursor_y;
    get_console_cursor_pos(&cursor_x, &cursor_y);
    if (waiting_for_input)
    {
        int lines = line_length / get_num_columns_for_console() + 1;
        clear_lines_from_y(cursor_y - lines + 1, lines);
    }

    // Always print the message first, which should already be terminated with a newline.
    printf("%s", message);

    // If we had to do some console cleanup beforehand, print the current 
    // input buffer again.
    if (waiting_for_input)
        printf("%s: %s", name_buffer, input_buffer);
}

static bool _client_exception_handler(struct bulb_client* client, 
                                      enum client_error_state error, 
                                      bool fatal, 
                                      void* data)
{
    char* message = NULL;

    switch (error)
    {
        // Clean up and exit once the client thread terminates.
        case CLIENT_FORCE_DISCONNECT:
            puts("\n\nThe server connection has closed unexpectedly.");
        case CLIENT_DISCONNECT:
            client->disconnect_handled = true;
            _cleanup(client);
            exit(0);

        // Handle asynchronous stdout that would otherwise disrupt the input flow.
        case CLIENT_RECEIVED_MESSAGE:
        {
            // <SEQ>NAME<SEQ>: MSG\n\0
            char buffer[COLOR_LENGTH + MAX_NAME_LENGTH + COLOR_LENGTH + 2 + MAX_MESSAGE_LENGTH + 2];
            struct client_message* msg = (struct client_message*)data;
            snprintf(buffer, sizeof(buffer), "%s%s%s: %s\n", 
                COLOR_LIGHT_CYAN, 
                msg->name, 
                COLOR_DEFAULT,
                msg->message);
            _print_message(client, buffer);
            return true;
        }
        case CLIENT_PRINT_STDOUT:
            _print_message(client, (const char*)data);
            return true;
        
        default:
            return !fatal;
    }
}

int main()
{
    enable_ansi_sequences();
    printf_clear_screen();
    printf("[CLIENT] ");
    bulb_printver();

    // Get the host name to connect to and strip the newline character.
    char hostname[2048] = { 0 };
    printf("Server address: ");
    fgets(hostname, sizeof(hostname), stdin);
    hostname[strlen(hostname) - 1] = '\0';

    // Get the port for the completed socket and strip the newline character.
    char port[7] = { 0 };   // Max digits in 16-bit port number + \n + NUL
    printf("Port number (leave blank if unknown): ");
    fgets(port, sizeof(port), stdin);
    port[strlen(port) - 1] = '\0';
    if (strlen(port) == 0)
        strcpy(port, STR(FIRST_PORT));

    // Instantiate and connect the new client instance.
    struct bulb_client* client = client_init(hostname, port, NULL);
    ASSERT(client, return 1);
    client_set_exception_handler(client, _client_exception_handler);
    ASSERT(client_connect(client), goto fail);

    // Get the player username and strip the newline character. The max username length is 
    // MAX_NAME_LENGTH, so + 3 accomodates the \n and NUL characters afterwards and also 
    // allows the client code to report to the user whether the username is too long.
    char username[MAX_NAME_LENGTH + 3] = { 0 };
    printf("Username: ");
    fgets(username, sizeof(username), stdin);
    username[strlen(username) - 1] = '\0';
    if (strlen(username) > MAX_NAME_LENGTH)
    {
        username[MAX_NAME_LENGTH] = '\0';
        printf("WARNING: Your username is too long, so it has been truncated to \"%s\"!", username);
    }

    // Disable stdin line buffering at this point.
    disable_line_buffering();
    
    // Authenticate the user connection.
    struct userinfo_obj userinfo = { };
    strcpy(userinfo.name, username);
    ASSERT(client_authenticate(client, userinfo), goto fail, "Could not authenticate user %s!", 
        username);

    // Wait for user input. From this point, the custom console_io.h functions must be used
    // for stdin/stdout.
    snprintf(name_buffer, sizeof(name_buffer), "%s%s%s", COLOR_RED, userinfo.name, COLOR_DEFAULT);
    waiting_for_input = true;
new_iteration:
    printf("%s: ", name_buffer);
    for (;;)
    {
        // Wait for next user input. This doubles as a busywait loop on POSIX
        // systems, as the use of ESC[6n in an asynchronous thread conflicts with
        // this loop repeatedly blocking for user input itself.
        int c = read_stdin_char();
        if (c == STDIN_EMPTY)
            continue;

        // Parse the inputted character.
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
                bool cmd_success;
                printf("\n");
                if (client_input(client, input_buffer, &cmd_success))
                {
                    if (cmd_success)
                        printf("%sCommand executed successfully!%s\n", COLOR_GREEN, COLOR_DEFAULT);
                    else
                        printf("%sCould not find command.%s\n", COLOR_RED, COLOR_DEFAULT);
                }
                memset(input_buffer, '\0', sizeof(input_buffer));
                input_buffer_w = 0;
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
                        input_buffer[--input_buffer_w] = '\0';
                        clear_last_character();
                    }
                }
                else if (input_buffer_w < sizeof(input_buffer) - 1)
                {
                    // Only visible characters should be added to the input buffer,
                    // although they will still be printed to the console so as to
                    // not cause weird disparities in user input.
                    if (isprint(c))
                        input_buffer[input_buffer_w++] = c;
                    putchar(c);
                }
                break;
        }
    }

finish:
    _cleanup(client);
    return 0;

fail:
    _cleanup(client);
    return 1;
}