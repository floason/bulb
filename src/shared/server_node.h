// floason (C) 2025
// Licensed under the MIT License.

// The server_node struct is useful to both client and server code, as it can be used
// to manage a total list of connected clients on either end, while also reading server
// information.

#pragma once

#include <stdbool.h>

#include "unisock.h"
#include "trie.h"
#include "client_node.h"   
    
#define LOOP_CLIENTS(SERVER, EXCEPT, ID, SCOPE)                                 \
    {                                                                           \
        mtx_lock(&SERVER->free_flagged_clients_mutex);                          \
        TRIE_DFS(SERVER->clients, trie##ID,                                     \
        {                                                                       \
            struct client_node* ID = (struct client_node*)trie##ID;             \
            if (ID != EXCEPT && ID->status == CLIENT_VALIDATED)                 \
                SCOPE;                                                          \
        });                                                                     \
        mtx_unlock(&SERVER->free_flagged_clients_mutex);                        \
    }                                                                       

struct bulb_server;

struct server_node
{
#ifdef SERVER
    struct bulb_server* bulb_server;
#endif

    // TODO: populate with server details such as name

    unsigned number_connected;
    mtx_t free_flagged_clients_mutex;

    // Dictionary of actual connected clients.
    struct trie* clients;

    // List of clients flagged for deletion.
    struct client_node* flagged_clients_list;
    struct client_node* flagged_clients_list_tail;
};

typedef void (*loop_clients_func)(struct server_node* server, struct client_node* client);

// Initialise the server node.
void server_node_init(struct server_node* node);

// Connect a new client to a server node's clients list.
void server_connect_client(struct server_node* server, struct client_node* client);

// Disconnect a client from a server node's clients list. This will free the client
// node from memory.
void server_disconnect_client(struct server_node* server, struct client_node* client, bool print_msg);

// Check if a client is connected without iterating through the entire list of clients.
// Returns the client node if found, othewrise NULL.
struct client_node* server_find_by_name(struct server_node* server, const char* name);

// Loop through each client.
void server_loop_clients(struct server_node* server, struct client_node* except, loop_clients_func func);

// Kick a client. This should be called from server code only, and is assumed to be
// called only from object code too.
void server_kick(struct server_node* server, struct client_node* client, const char* msg);

// Disconnect a client that is flagged for deletion.
void server_free_flagged_client(struct server_node* server, struct client_node* client);

// Disconnect any clients that are flagged for deletion.
void server_free_flagged_clients(struct server_node* server);

// Disconnect all connected clients from a server node's clients list. This will free
// all client nodes from memory.
void server_disconnect_all_clients(struct server_node* server);