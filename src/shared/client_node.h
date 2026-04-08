// floason (C) 2025
// Licensed under the MIT License.

// The client_node struct is useful to both client and server code, as it can be used
// to manage a total list of connected clients on either end.

#pragma once

#include <stdbool.h>
#include <threads.h>

#include "unisock.h"
#include "bulb_macros.h"

struct bulb_client;
struct userinfo_obj;

struct client_node
{
#ifdef CLIENT
    struct bulb_client* bulb_client;
#endif
    struct server_node* server_node;
    struct userinfo_obj* userinfo;
    bool validated;

    // In order to prevent race conditions during client-looping code, client node
    // objects cannot be immediately free()'d from memory. This must be handled
    // at an appropriate time where LOOP_CLIENTS() is not being called.
    bool flag_for_deletion;

#ifdef SERVER
    // Used only by the server for triggering the client deletion code after an object
    // is finished being processed.
    bool delete;

    struct sockaddr_in addr;
#endif

    thrd_t thread;
    struct mt_socket mt_sock;
    struct client_node* next;   // This will always be NULL for the local client in client code!
    struct client_node* prev;
};