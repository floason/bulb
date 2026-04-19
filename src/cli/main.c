// floason (C) 2026
// Licensed under the MIT License.

#include <stdio.h>
#include <stdbool.h>
#include <memory.h>
#include <string.h>
#include <ctype.h>
#include <threads.h>
#include <stdatomic.h>

#include "unisock.h"
#include "bulb_version.h"
#include "bulb_structs.h"
#include "client.h"
#include "server.h"
#include "console_io.h"

#define COLOR_LENGTH                                11 // \x1B[38:5:###m

static char input_buffer[MAX_MESSAGE_LENGTH + 1]    = "";
static int input_buffer_w                           = 0;
static bool waiting_for_input                       = false;

static const char* COLOR_WHITE                      = "\x1B[38:5:15m";
static const char* COLOR_LIGHT_CYAN                 = "\x1B[38:5:159m";
static const char* COLOR_RED                        = "\x1B[38:5:1m";
static const char* COLOR_GREEN                      = "\x1B[38:5:2m";
static const char* COLOR_DEFAULT                    = "\x1B[39m";

// <SEQ>NAME<SEQ>\0
static char name_buffer[COLOR_LENGTH + MAX_NAME_LENGTH + COLOR_LENGTH + 1];

static mtx_t print_message_lock;
static atomic_bool print_message_lock_busy;

static struct bulb_userinfo userinfo = { 0 };

static void _cleanup()
{
    disable_console_io_functions();
}

static void _client_cleanup(struct bulb_client* client)
{
    _cleanup();
    client_free(client);
}

static void _server_cleanup(struct bulb_server* server)
{
    _cleanup();
    server_free(server);
}

static void _clear_input_buffer_on_screen()
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

static void _print_message(const char* message)
{   
    // Use mutex locking here to ensure that messages are outputted synchronously.
    atomic_store(&print_message_lock_busy, true);
    mtx_lock(&print_message_lock);
    
    // The current line length is calculated using the following format:
    // NAME: MESSAGE
    unsigned line_length = strlen(userinfo.name) + 2 + input_buffer_w;

    // Any pending client user input must be cleared first.
    if (waiting_for_input)
        _clear_input_buffer_on_screen();

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

    mtx_unlock(&print_message_lock);
    atomic_store(&print_message_lock_busy, false);
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
        case CLIENT_AUTH_FAIL:
        case CLIENT_DISCONNECT:
            client->disconnect_handled = true;
            if (waiting_for_input)
                _clear_input_buffer_on_screen();
            _client_cleanup(client);
            exit(0);

        // Handle asynchronous stdout that would otherwise disrupt the input flow.
        case CLIENT_RECEIVED_MESSAGE:
        {
            // <SEQ>NAME<SEQ>: MSG\n\0
            char buffer[COLOR_LENGTH + MAX_NAME_LENGTH + COLOR_LENGTH + 2 + MAX_MESSAGE_LENGTH + 2];
            struct bulb_message* msg = (struct bulb_message*)data;
            const char* colour = (msg->is_server ? COLOR_WHITE : COLOR_LIGHT_CYAN);
            snprintf(buffer, sizeof(buffer), "%s%s%s: %s\n", 
                colour, 
                msg->name, 
                COLOR_DEFAULT,
                msg->message);
            _print_message(buffer);
            return true;
        }
        case CLIENT_PRINT_STDOUT:
            _print_message((const char*)data);
            return true;
        
        default:
            return !fatal;
    }
}

static bool _server_exception_handler(struct bulb_server* server, 
                                      enum server_error_state error, 
                                      bool fatal, 
                                      void* data)
{
    switch (error)
    {
        // Clean up and exit once the server listen thread terminates.
        case SERVER_FINISH:
            // The only reason the SERVER_FINISH enum is utilised at the moment is
            // when the server calls the exit command. Thus, we can ensure that the
            // server operator is aware the command has executed successfully by
            // printing its success.
            printf("%sCommand executed successfully!\nShutting down the server...\n%s", COLOR_GREEN, 
                COLOR_DEFAULT);

            // Handle the actual disconnect sequence.
            server->disconnect_handled = true;
            server_free(server);
            exit(0);

        // A client connection was not accepted successfully.
        case SERVER_CLIENT_ACCEPT_FAIL:
        {
            char buffer[128];
            snprintf(buffer, sizeof(buffer), "Failed to accept new client connection: %d\n", 
                socket_errno());
            _print_message(buffer);
            return true;
        }

        // Handle asynchronous stdout that would otherwise disrupt the input flow.
        case SERVER_RECEIVED_MESSAGE:
        {
            // <SEQ>NAME<SEQ>: MSG\n\0
            char buffer[COLOR_LENGTH + MAX_NAME_LENGTH + COLOR_LENGTH + 2 + MAX_MESSAGE_LENGTH + 2];
            struct bulb_message* msg = (struct bulb_message*)data;
            snprintf(buffer, sizeof(buffer), "%s%s%s: %s\n", 
                COLOR_LIGHT_CYAN, 
                msg->name, 
                COLOR_DEFAULT,
                msg->message);
            _print_message(buffer);
            return true;
        }
        case SERVER_PRINT_STDOUT:
            _print_message((const char*)data);
            return true;

        default:
            return !fatal;
    }
}

int main(int argc, char** argv)
{
    int return_value = 0;
    struct bulb_client* client = NULL;
    struct bulb_server* server = NULL;

    mtx_init(&print_message_lock, mtx_plain | mtx_recursive);
    enable_ansi_sequences();
    printf_clear_screen();
    
    // Choose operation mode by reading the first parameter. This is simplified
    // and more sophisticated command-line argument parsing will be introduced
    // in the future.
    bool is_server = false;
    if (argc > 1 && strcmp(argv[1], "--server") == 0)
    {
        // Running as a server.
        is_server = true;
        printf("[SERVER] ");
        bulb_printver();

        strncpy(userinfo.name, "[SERVER]", sizeof(userinfo.name));

        // TODO: try probing successive ports on SERVER_ADDRESS_FAIL
        server = server_init(STR(FIRST_PORT), NULL);
        ASSERT(server, return 1);

        server_set_exception_handler(server, _server_exception_handler);
        ASSERT(server_listen(server), goto fail);
        
        // Disable stdin line buffering at this point.
        enable_console_io_functions();
    }
    else
    {
        // Running as a client.
        printf("[CLIENT] ");
        bulb_printver();

        // Get the host name to connect to and strip the newline character.
        char hostname[2048] = { 0 };
        printf("Server address: ");
        fgets(hostname, sizeof(hostname), stdin);
        hostname[strlen(hostname) - 1] = '\0';

        // If the host name given is empty, default to localhost.
        for (int i = 0; i < sizeof(hostname); ++i)
        {
            if (hostname[i] == '\0')
                strcpy(hostname, "localhost");
            else if (!isspace(hostname[i]))
                break;
        }

        // Get the port for the completed socket and strip the newline character.
        char port[7] = { 0 };   // Max digits in 16-bit port number + \n + NUL
        printf("Port number (leave blank if unknown): ");
        fgets(port, sizeof(port), stdin);
        port[strlen(port) - 1] = '\0';
        if (strlen(port) == 0)
            strcpy(port, STR(FIRST_PORT));

        // Instantiate and connect the new client instance.
        client = client_init(hostname, port, NULL);
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
        enable_console_io_functions();

        // Authenticate the user connection.
        strcpy(userinfo.name, username);
        ASSERT(client_authenticate(client, userinfo), goto fail, "Could not authenticate user %s!", 
            username);
    }

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
                if ((strlen(userinfo.name) + 2 + input_buffer_w) % get_num_columns_for_console() > 0)
                    printf("\n");

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
        _server_cleanup(server);
    else
        _client_cleanup(client);
    return return_value;
}