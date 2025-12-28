// floason (C) 2025
// Licensed under the MIT License.

// The client_node struct is useful to both client and server code, as it can be used
// to manage a total list of connected clients on either end.

#pragma once

#include <stdbool.h>
#include <threads.h>

#include "unisock.h"
#include "bulb_macros.h"

struct userinfo_obj;

struct client_node
{
#ifdef CLIENT
    struct bulb_client* bulb_client;
#endif
    struct server_node* server_node;
    struct userinfo_obj* userinfo;
    bool validated;

#ifdef SERVER
    // Used during client validation.
    bool delete;

    struct sockaddr_in addr;
#endif

    SOCKET sock;
    thrd_t thread;
    struct client_node* next;   // This will always be NULL for the local client in client code!
    struct client_node* prev;
};