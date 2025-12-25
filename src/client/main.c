// floason (C) 2025
// Licensed under the MIT License.

#include <memory.h>
#include <string.h>

#include "client.h"

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
            client_free(client);
            exit(0);
        
        default:
            return !fatal;
    }
}

int main()
{
    // Get the host name to connect to and strip the newline character.
    char hostname[2048];
    printf("%s", "Server address: ");
    fgets(hostname, sizeof(hostname), stdin);
    hostname[strlen(hostname) - 1] = '\0';

    // Get the port for the completed socket and strip the newline character.
    char port[7];   // Max digits in 16-bit port number + \n + NUL
    printf("%s", "Port number (leave blank if unknown): ");
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
    // MAX_NAME_LENGTH, so + 3 accomodates the \n and NUL character afterwards and also 
    // allows the client code to report to the user whether the username is too long.
    char username[MAX_NAME_LENGTH + 3];
    printf("%s", "Username: ");
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

    // Busy-wait on the main thread so that the client thread doesn't terminated prematurely.
    // The main thread sleeps so that task scheduling does not prioritise this thread.
    for (;;) 
    { 
#if defined WIN32
        Sleep(1000);
#elif defined __UNIX__
        sleep(1);
#endif
    }

    client_free(client);
    return 0;

fail:
    client_free(client);
    return 1;
}