// floason (C) 2025
// Licensed under the MIT License.

// The client_node struct is useful to both client and server code, as it can be used
// to manage a total list of connected clients on either end.

#pragma once

#include <threads.h>

#include "unisock.h"

struct client_node
{
#ifdef CLIENT
    struct bulb_client* bulb_client;
#endif
    struct server_node* server_node;

    // TODO: populate with address, username, etc attributes

    SOCKET sock;
    thrd_t thread;
    struct client_node* next;   // This will always be NULL for the local client in client code!
    struct client_node* prev;
};