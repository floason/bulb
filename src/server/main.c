// floason (C) 2025
// Licensed under the MIT License.

#include <stdio.h>

#include "bulb_version.h"
#include "server.h"

static bool _server_exception_handler(struct bulb_server* server, 
                                      enum server_error_state error, 
                                      bool fatal, 
                                      void* data)
{
    switch (error)
    {
        // Clean up and exit once the server listen thread terminates.
        case SERVER_FINISH:
            server_free(server);
            exit(0);

        default:
            return !fatal;
    }
}

int main()
{
    printf("[SERVER] ");
    bulb_printver();

    // TODO: try probing successive ports on SERVER_ADDRESS_FAIL
    struct bulb_server* server = server_init(STR(FIRST_PORT), NULL);
    ASSERT(server, return 1);

    server_set_exception_handler(server, _server_exception_handler);
    ASSERT(server_listen(server), 
    {
        server_free(server);
        return 1;
    });

    // Busy-wait on the main thread so that the server thread doesn't terminated prematurely.
    // The main thread sleeps so that task scheduling does not prioritise this thread.
    for (;;) 
    { 
#if defined WIN32
        Sleep(1000);
#elif defined __UNIX__
        sleep(1);
#endif
    }

    server_free(server);
    return 0;
}