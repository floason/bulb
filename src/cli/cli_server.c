// floason (C) 2026
// Licensed under the MIT License.

#include <stdio.h>
#include <stdint.h>

#include "unisock.h"
#include "bulb_server.h"

#include "cli_server.h"
#include "cli_util.h"

static bool _cli_server_exception_handler(struct bulb_server* server, 
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
            cli_server_cleanup(server);
            exit(0);

        // A client connection was not accepted successfully.
        case SERVER_CLIENT_ACCEPT_FAIL:
        {
            char buffer[128];
            snprintf(buffer, sizeof(buffer), "Failed to accept new client connection: %d\n", 
                socket_errno());
            print_message(buffer);
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
            print_message(buffer);
            return true;
        }
        case SERVER_PRINT_STDOUT:
            print_message((const char*)data);
            return true;

        default:
            return !fatal;
    }
}

struct bulb_server* cli_server_init(uint16_t custom_port)
{
    struct bulb_server* server;

    uint16_t port = custom_port;
    if (custom_port == BULB_USE_DEFAULT_PORT)
    {
        enum server_error_state error_state;
        port = BULB_FIRST_PORT;
        while ((server = server_init(port, &error_state)) == NULL 
            && (error_state == SERVER_LISTEN_SOCKET_FAIL 
            && port < BULB_FIRST_PORT + 10))
        {
            printf("Could not start server using port %hu...\n", port);
            port++;
        }
    }
    else
        server = server_init(custom_port, NULL);
    ASSERT(server, return NULL);

    server_set_exception_handler(server, _cli_server_exception_handler);
    server_set_userinfo(server, &userinfo);
    ASSERT(server_listen(server), return NULL);

    // Disable stdin line buffering at this point.
    printf("Listening on port %hu\n", port);
    enable_console_io_functions();
    return server;
}

void cli_server_cleanup(struct bulb_server* server)
{
    if (server == NULL)
        return;
    cleanup();
    server_free(server);
}