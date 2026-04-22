// floason (C) 2026
// Licensed under the MIT License.

#include <stdio.h>
#include <stdint.h>

#include "console_io.h"
#include "bulb_client.h"

#include "cli_client.h"
#include "cli_util.h"

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
                clear_input_buffer_on_screen();
            cli_client_cleanup(client);
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
            print_message(buffer);
            return true;
        }
        case CLIENT_PRINT_STDOUT:
            print_message((const char*)data);
            return true;
        
        default:
            return !fatal;
    }
}

struct bulb_client* cli_client_init(const char* custom_host, uint16_t custom_port)
{
    // Get the host name to connect to and strip the newline character.
    char hostname[2048] = { 0 };
    if (custom_host == NULL)
    {
        printf("Server address: ");
        fgets(hostname, sizeof(hostname), stdin);
        hostname[strlen(hostname) - 1] = '\0';

        // If the host name given is empty, default to localhost.
        for (int i = 0; i < sizeof(hostname); i++)
        {
            if (hostname[i] == '\0')
                strcpy(hostname, "localhost");
            else if (!isspace(hostname[i]))
                break;
        }
    }
    else
        strncpy(hostname, custom_host, sizeof(hostname));

    // Get the port for the completed socket and strip the newline character.
    char port[7] = { 0 };   // Max digits in 16-bit port number + \n + NUL
    if (custom_port == BULB_USE_DEFAULT_PORT)
    {
        printf("Port number (leave blank if unknown): ");
        fgets(port, sizeof(port), stdin);
        port[strlen(port) - 1] = '\0';
        if (strlen(port) == 0)
            strcpy(port, STR(BULB_FIRST_PORT));
    }
    else
        snprintf(port, sizeof(port), "%hu", custom_port);

    // Instantiate and connect the new client instance.
    struct bulb_client* client = client_init(hostname, port, NULL);
    ASSERT(client, return NULL);
    client_set_exception_handler(client, _client_exception_handler);
    ASSERT(client_connect(client), return NULL);

    // Get the player username and strip the newline character. The max username length is 
    // MAX_NAME_LENGTH, so + 3 accomodates the \n and NUL characters afterwards and also 
    // allows the client code to report to the user whether the username is too long.
    char username[MAX_NAME_LENGTH + 3] = { 0 };
    if (strlen(userinfo.name) == 0)
    {
        printf("Username: ");
        fgets(username, sizeof(username), stdin);
        username[strlen(username) - 1] = '\0';
        if (strlen(username) > MAX_NAME_LENGTH)
        {
            username[MAX_NAME_LENGTH] = '\0';
            printf("WARNING: Your username is too long, so it has been truncated to \"%s\"!", username);
        }
        strcpy(userinfo.name, username);
    }

    // Disable stdin line buffering at this point.
    enable_console_io_functions();

    // Authenticate the user connection.
    ASSERT(client_authenticate(client, &userinfo), return NULL, 
        "Failed to authenticate server connection!\n");
    return client;
}

void cli_client_cleanup(struct bulb_client* client)
{
    if (client == NULL)
        return;
    cleanup();
    client_free(client);
}