// floason (C) 2025
// Licensed under the MIT License.

// The server_node struct implements the backbone of the Bulb protocol and
// supplements both the client and the server.

#pragma once

#include <stdbool.h>

#include "unisock.h"
#include "networking.h"
#include "trie.h"
#include "bulb_structs.h"
#include "shared_interface.h"
#include "client_node.h"   

// Clients should NOT be disconnected/kicked from the server within this loop!
#define LOOP_CLIENTS(SERVER, EXCEPT, ID, SCOPE)                                 \
    {                                                                           \
        mtx_lock(&SERVER->connection_update_mutex);                             \
        TRIE_DFS(SERVER->clients, trie##ID,                                     \
        {                                                                       \
            struct client_node* ID = (struct client_node*)trie##ID;             \
            if (ID != EXCEPT && ID->status == CLIENT_VALIDATED)                 \
                SCOPE;                                                          \
        });                                                                     \
        mtx_unlock(&SERVER->connection_update_mutex);                           \
    }                                                                  

struct bulb_server;

struct server_node
{
#ifdef SERVER
    struct bulb_server* bulb_server;
    SOCKET listen_sock;
#endif

    // Server information.
    struct bulb_userinfo info;

    unsigned number_connected;
    unsigned number_pending_deletion;
    mtx_t connection_update_mutex;
    mtx_t server_emptied_mutex;
    cnd_t server_emptied_signal;

    // Client socket communication architecture.
    thrd_t client_manage_thread;
    mtx_t client_update_lock;
    cnd_t client_update_signal;
    bool cleanup;
    struct mt_socket* socket_recv_queue;
    struct mt_socket* socket_recv_tail;
    struct mt_socket* socket_send_queue;
    struct mt_socket* socket_send_tail;
    struct socket_manager* sm_head;
    struct socket_manager* sm_tail;

    // Dictionary of actual connected clients.
    struct trie* clients;

    // List of clients' userinfo objects.
    struct bulb_userinfo* clients_info_head;
    struct bulb_userinfo* clients_info_tail;
    
    // List of clients flagged for deletion.
    struct client_node* flagged_clients_list;
    struct client_node* flagged_clients_list_tail;
};

typedef void (*loop_clients_func)(struct server_node* server, struct client_node* client);

// Initialise the server node.
struct server_node* server_shared_node_alloc();

// Begin listening to a client's socket. The client's socket object will be
// automatically released from memory as soon as it is disused.
void server_listen_client(struct server_node* server, struct client_node* client);

// Connect a new client to a server node's clients list.
void server_connect_client(struct server_node* server, struct client_node* client);

// Start disconnecting a client from a server node's clients list.
void server_disconnect_client(struct server_node* server, 
                              struct client_node* client, 
                              bool print_msg, 
                              bool unlink,
                              bool server_shutdown);

// Check if a client is connected without iterating through the entire list of clients.
// Returns the client node if found, othewrise NULL.
struct client_node* server_find_by_name(struct server_node* server, const char* name);

// Loop through each client.
void server_loop_clients(struct server_node* server, struct client_node* except, loop_clients_func func);

// Kick a client. This should be called from server code only, and is assumed to be
// called only from object code too.
void server_kick(struct server_node* server, struct client_node* client, const char* msg);

// Ban a client. As the client is not always assumed to be in the server, an immediate kick 
// should be invoked separately. This should be called from server code only. Returns false
// if the client was already banned.
bool server_ban(struct server_node* server, const char* ip_addr, const char* reason);

// Unbans an address. Returns false if the address was not in the ban database.
bool server_unban(struct server_node* server, const char* ip_addr);

// Disconnect a client that is flagged for deletion.
void server_free_flagged_client(struct server_node* server, struct client_node* client);

// Disconnect any clients that are flagged for deletion.
void server_free_flagged_clients(struct server_node* server);

// Disconnect all connected clients from a server node's clients list. This will free
// all client nodes from memory. This should only be called when the Bulb protocol is
// being terminated as it frees the server node from memory.
void server_disconnect_all_clients(struct server_node* server);