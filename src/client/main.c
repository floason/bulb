// floason (C) 2025
// Licensed under the MIT License.

#include <stdio.h>
#include <memory.h>
#include <string.h>

#include "bulb_version.h"
#include "client.h"     // MUST BE INCLUED BEFORE "console_io.h"!
#include "console_io.h"

static char input_buffer[MAX_MESSAGE_LENGTH + 1] = { };
static int input_buffer_w = 0;
static bool waiting_for_input = false;

static void _cleanup(struct bulb_client* client)
{
    client_free(client);
    cleanup_console_mode();
}

static bool _client_exception_handler(struct bulb_client* client, 
                                      enum client_error_state error, 
                                      bool fatal, 
                                      void* data)
{
    switch (error)
    {
        // Clean up and exit once the client thread terminates.
        case CLIENT_DISCONNECT:
        case CLIENT_FORCE_DISCONNECT:
            _cleanup(client);
            exit(0);

        // Handle asynchronous stdout that would otherwise disrupt the input flow.
        case CLIENT_PRINT_MESSAGE:
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
                unsigned lines;
                get_num_lines_for_console(line_length, &lines, 0);
                clear_lines_from_y(cursor_y - lines + 1, lines);
            }

            // Always print the message first. It should be terminated with a newline.
            char* message = (char*)data;
            write_stdout_fstring("%s", message);

            // If we had to do some console cleanup beforehand, print the current 
            // input buffer again.
            if (waiting_for_input)
                write_stdout_fstring("%s: %s", client->local_node->userinfo->name, input_buffer);

            return true;
        }
        
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
    console_io_input_buffer = input_buffer;

    // Get the host name to connect to and strip the newline character.
    char hostname[2048];
    printf("Server address: ");
    fgets(hostname, sizeof(hostname), stdin);
    hostname[strlen(hostname) - 1] = '\0';

    // Get the port for the completed socket and strip the newline character.
    char port[7];   // Max digits in 16-bit port number + \n + NUL
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
    char username[MAX_NAME_LENGTH + 3];
    printf("Username: ");
    fgets(username, sizeof(username), stdin);
    username[strlen(username) - 1] = '\0';
    if (strlen(username) > MAX_NAME_LENGTH)
    {
        username[MAX_NAME_LENGTH] = '\0';
        printf("WARNING: Your username is too long, so it has been truncated to \"%s\"!", username);
    }
    
    // Authenticate the user connection.
    struct userinfo_obj userinfo = { };
    strcpy(userinfo.name, username);
    ASSERT(client_authenticate(client, userinfo), goto fail, "Could not authenticate user %s!", 
        username);

    // Wait for user input. From this point, the custom console_io.h functions must be used
    // for stdin/stdout.
    char buffer[MAX_NAME_LENGTH + 3]; // NAME: \0
    snprintf(buffer, sizeof(buffer), "%s: ", userinfo.name);
    console_io_input_prepend = buffer;
    disable_line_buffering();
    waiting_for_input = true;
new_iteration:
    write_stdout_fstring("%s", buffer);
    for (;;)
    {
        int c = read_stdin_char();

        switch (c)
        {
            // Non-visible characters are captured here.
            case '\0':
                break;

            // Handle enter.
            case '\n':
            case '\r':
                client_input(client, input_buffer);
                memset(input_buffer, '\0', sizeof(input_buffer));
                input_buffer_w = 0;
                write_stdout_fstring("\n");
                goto new_iteration;
            
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
                    input_buffer[input_buffer_w++] = c;
                    write_stdout_char(c);
                }
                break;
        }
    }

    _cleanup(client);
    return 0;

fail:
    _cleanup(client);
    return 1;
}