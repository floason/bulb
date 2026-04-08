// floason (C) 2025
// Licensed under the MIT License.

// The server_node struct is useful to both client and server code, as it can be used
// to manage a total list of connected clients on either end, while also reading server
// information.

#pragma once

#include <stdbool.h>

#include "unisock.h"
#include "client_node.h"

#define LOOP_CLIENTS(SERVER, EXCEPT, ID, SCOPE)                             \
    {                                                                       \
        struct client_node* ID = SERVER->clients;                           \
        while (ID != NULL)                                                  \
        {                                                                   \
            if (ID != EXCEPT && ID->validated && !ID->flag_for_deletion)    \
                SCOPE;                                                      \
            ID = ID->next;                                                  \
        }                                                                   \
    }            
    
struct bulb_server;

struct server_node
{
#ifdef SERVER
    struct bulb_server* bulb_server;
#endif

    // TODO: populate with server details such as name

    unsigned number_connected;

    // List of actual connected clients.
    struct client_node* clients;        // The first node will always be the local client in client code!
    struct client_node* clients_tail;   
    
    // List of clients flagged for deletion.
    struct client_node* flagged_clients_list;
    struct client_node* flagged_clients_list_tail;
};

// Initialise the server node.
void server_node_init(struct server_node* node);

// Connect a new client to a server node's clients list.
void server_connect_client(struct server_node* server, struct client_node* client);

// Disconnect a client from a server node's clients list. This will free the client
// node from memory.
void server_disconnect_client(struct server_node* server, struct client_node* client);

// Kick a client. This should be called from server code only, and is assumed to be
// called only from object code too.
void server_kick(struct server_node* server, struct client_node* client, const char* msg);

// Disconnect any clients that are flagged for deletion.
void server_free_flagged_clients(struct server_node* server);

// Disconnect all connected clients from a server node's clients list. This will free
// all client nodes from memory.
void server_disconnect_all_clients(struct server_node* server);