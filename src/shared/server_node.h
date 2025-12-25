// floason (C) 2025
// Licensed under the MIT License.

// The server_node struct is useful to both client and server code, as it can be used
// to manage a total list of connected clients on either end, while also reading server
// information.

#pragma once

#include "unisock.h"
#include "client_node.h"

#define LOOP_CLIENTS(LIST, EXCEPT, ID, SCOPE)   \
    {                                           \
        struct client_node* ID = LIST;          \
        while (ID != NULL)                      \
        {                                       \
            if (ID != EXCEPT && ID->validated)  \
                SCOPE;                          \
            ID = ID->next;                      \
        }                                       \
    }                                           

struct server_node
{
    // TODO: populate with server details such as name

    struct client_node* clients;        // The first node will always be the local client in client code!
    struct client_node* clients_tail;    
};

// Initialise the server node.
void server_node_init(struct server_node* node);

// Connect a new client to a server node's clients list.
void server_connect_client(struct server_node* server, struct client_node* client);

// Disconnect a client from a server node's clients list. This will free the client
// node from memory.
void server_disconnect_client(struct server_node* server, struct client_node* client);

// Disconnect all connected clients from a server node's clients list. This will free
// all client nodes from memory.
void server_disconnect_all_clients(struct server_node* server);