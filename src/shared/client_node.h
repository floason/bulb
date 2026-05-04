// floason (C) 2025
// Licensed under the MIT License.

// The client_node struct is useful to both client and server code, as it can be used
// to manage a total list of connected clients on either end.

#pragma once

#include <stdbool.h>
#include <stdatomic.h>
#include <threads.h>

#include "unisock.h"
#include "trie.h"

#define SPAWN_CLIENT_THREAD(CLIENT, NAME)                                   \
    {                                                                       \
        thrd_create(&CLIENT->NAME, server_client_##NAME, (void*)CLIENT);    \
        CLIENT->thread_ref_count++;                                         \
    }

enum client_status
{
    // The client has just made a request to connect to the server.
    CLIENT_REQUESTING = 0,

    // The client is currently active.
    CLIENT_VALIDATED,

    // server_client_disconnect() has been called on this client node, therefore the
    // deletion process has begun. In order to prevent race conditions during 
    // client-looping code, client node objects cannot be immediately free()'d from memory. 
    // This must be handled at an appropriate time where LOOP_CLIENTS() is not being called.
    CLIENT_FLAGGED_FOR_DELETION,

    // This client is now ready to delete.
    CLIENT_READY_TO_DELETE
};

struct bulb_client;
struct userinfo_obj;

struct client_node
{
#ifdef CLIENT
    struct bulb_client* bulb_client;
#endif
    struct server_node* server_node;
    struct userinfo_obj* userinfo;
    enum client_status status;
    bool exit_is_orderly;

#ifdef SERVER
    struct sockaddr_in addr;
#endif

    // Client communication architecture.
    thrd_t recv_thread;
    thrd_t send_thread;
    thrd_t ping_thread;
    mtx_t send_thread_lock;     // Used for preventing race conditions with client node deletion.
    mtx_t client_status_lock;   // Used for preventing (albeit unlikely) race conditions for client status.
    mtx_t ping_lock;            // Used for waking up the ping thread upon client deletion.
    cnd_t client_delete_signal; // Signalled when the client has been flagged for deletion.
    struct mt_socket mt_sock;
    unsigned thread_ref_count;
    bool ready_to_ping;

    // Used for linking client nodes when pending deallocation.
    struct client_node* next;
    struct client_node* prev;

    // Used for LOOP_CLIENTS().
    struct trie* loop_next;
    struct trie* loop_prev;
};

#ifdef CLIENT
    extern struct client_node* localclient;
#endif

// Initialize a managed (i.e. not other client nodes for client code)
// client node's shared variables in place.
void client_shared_node_init(struct client_node* client);

// Is a client being prepared for deletion?
bool client_flagged_for_deletion(struct client_node* client);

// Update a client's status, if appropriate.
void client_set_status(struct client_node* client, enum client_status flag);

// Handle writing objects in a new thread for the given client connection asynchronously
// from the main client object listen thread, so as to not disrupt the backbone client
// thread if it attempts to communicate with other clients (message_obj in particular).
int server_client_send_thread(void* c);