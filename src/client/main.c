// floason (C) 2025
// Licensed under the MIT License.

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
    // TODO: prompt user for hostname:port
    // TODO: user details in a future revision

    struct bulb_client* client = client_init("localhost", STR(FIRST_PORT), NULL);
    ASSERT(client, { return 1; });

    client_set_exception_handler(client, _client_exception_handler);
    ASSERT(client_connect(client), 
    {
        client_free(client);
        return 1;
    });

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
}