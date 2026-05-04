// floason (C) 2025
// Licensed under the MIT License.

#include <string.h>
#include <stdatomic.h>

#include "unisock.h"
#include "trie.h"
#include "server_node.h"
#include "userinfo_obj.h"
#include "disconnect_obj.h"
#include "stdout_obj.h"

#ifdef SERVER
#   include "bulb_server.h"
#endif

// Flag a client node for deletion.
static inline void _client_flag_for_deletion(struct client_node* client, bool server_shutdown)
{
    if (client_flagged_for_deletion(client))
        return;

    // Signal to the client that the connection is being shut down.
#ifdef SERVER
    disconnect_obj_write(&client->mt_sock, "", server_shutdown);
#endif

    // Flag the client for deletion.
    client_set_status(client, CLIENT_FLAGGED_FOR_DELETION);
    cnd_broadcast(&client->client_delete_signal);

    // Only hint that the socket is being shut down after the client is flagged 
    // deletion and the disconnect object has been written, so as to handle
    // terminating all client-associated threads appropriately. This specific
    // architecture is designed so that any pending objects can still be sent
    // via the socket connection (as objects are enqueued before send() is called).
    mt_socket_hint_shutdown(&client->mt_sock);
}

// Close and free a client node.
static inline void _client_close(struct client_node* client)
{
    if (client->mt_sock.socket != INVALID_SOCKET)
        cleanup_mt_socket(&client->mt_sock);
    mtx_destroy(&client->client_status_lock);
    mtx_destroy(&client->send_thread_lock);
    mtx_destroy(&client->ping_lock);
    cnd_destroy(&client->client_delete_signal);

    if (client->userinfo != NULL)
        tagged_free(client->userinfo, TAG_BULB_OBJ);
    tagged_free(client, TAG_CLIENT_NODE);
}

// Initialise the server node.
struct server_node* server_shared_node_alloc()
{
    struct server_node* server = tagged_malloc(sizeof(struct server_node), TAG_SERVER_NODE);
    mtx_init(&server->free_flagged_clients_mutex, mtx_plain | mtx_recursive);
    mtx_init(&server->server_emptied_mutex, mtx_plain);
    cnd_init(&server->server_emptied_signal);
    server->clients = trie_new();
    return server;
}

// Connect a new client to a server node's clients list.
void server_connect_client(struct server_node* server, struct client_node* client)
{
    // The client node should already have its socket and userinfo configured.

    trie_add(server->clients, client->userinfo->info.name, client);
    server->number_connected++;

    // See further client connection (i.e. authentication) code in msg_obj/userinfo_obj.c.
}   

// Start disconnecting a client from a server node's clients list.
void server_disconnect_client(struct server_node* server, 
                              struct client_node* client, 
                              bool print_msg, 
                              bool unlink,
                              bool server_shutdown)
{
#ifdef SERVER
    // If the client did not fail server authentication checks, log whether it disconnected 
    // or if the client attempted to connect but a connection could not be established to 
    // begin with.
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client->addr.sin_addr, ip_str, sizeof(ip_str));
    if (print_msg)
    {
        if (client->userinfo != NULL)
        {
            server_printf(server, "Client \"%s\" (%s) has disconnected\n", client->userinfo->info.name, 
                ip_str);

            // The server reports this to clients, rather than simply printing this on each
            // client, so that I don't have to re-implement the same code used with stdout_obj
            // that allows client code to format console output properly.
            char buffer[64 + MAX_NAME_LENGTH];
            snprintf(buffer, sizeof(buffer), "Client \"%s\" has disconnected\n",
                client->userinfo->info.name);
            LOOP_CLIENTS(server, client, node, stdout_obj_write(&node->mt_sock, buffer));
        }
        else
            server_printf(server, "Client from address %s failed to connect\n", ip_str);
    }

    // Synchronise the client's departure with all other clients.
    if (client->status == CLIENT_VALIDATED)
    {
        LOOP_CLIENTS(server, client, node, 
            disconnect_obj_write(&node->mt_sock, client->userinfo->info.name, server_shutdown));
    }
#endif

    // By default, unlink should be toggled as server_disconnect_client should only be
    // called for single clients. However, if the server shuts down, it must loop through
    // every client in order to disconnect them. Unlinking them while traversing the tree
    // is thus likely to cause a segmentation fault.
    if (unlink)
    {
        if (client->userinfo)
            trie_delete(server->clients, client->userinfo->info.name);
        LINKED_LIST_ADD(client, server->flagged_clients_list, server->flagged_clients_list_tail);
    }

    if (server_shutdown)
        client->exit_is_orderly = true;

    server->number_connected--;
    _client_flag_for_deletion(client, server_shutdown);
}

// Check if a client is connected without iterating through the entire list of clients.
// Returns the client node if found, othewrise NULL.
struct client_node* server_find_by_name(struct server_node* server, const char* name)
{
    if (strlen(name) > MAX_NAME_LENGTH)
        return NULL;
    return trie_find(server->clients, name);
}

// Kick a client. This should be called from server code only, and is assumed to be
// called only from object code too.
void server_kick(struct server_node* server, struct client_node* client, const char* msg)
{
#ifdef SERVER
    // Log the client's departure in the server console and to the client itself.
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client->addr.sin_addr, ip_str, sizeof(ip_str));
    server_printf(server, "Client \"%s\" (%s) has been kicked from the server: %s\n", 
        client->userinfo->info.name, ip_str, msg);
    stdout_obj_write(&client->mt_sock, msg);
    
    // Write to all other clients that this user has been kicked.
    char buffer[1024 + MAX_NAME_LENGTH];
    snprintf(buffer, sizeof(buffer), "Client \"%s\" has been kicked from the server: %s\n",
        client->userinfo->info.name, msg);
    LOOP_CLIENTS(server, client, node, stdout_obj_write(&node->mt_sock, buffer));

    // Start disconnecting the client.
    server_disconnect_client(server, client, false, true, false);
    return;
#endif

    ASSERT(false, return);
}

// Disconnect a client that is flagged for deletion.
void server_free_flagged_client(struct server_node* server, struct client_node* client)
{
    ASSERT(client->status == CLIENT_READY_TO_DELETE, return, 
        "Attempted to delete client \"%s\", which is not ready to delete!", client->userinfo->info.name);

    LINKED_LIST_REMOVE(client, server->flagged_clients_list, server->flagged_clients_list_tail);
    _client_close(client);
}

// Disconnect any clients that are flagged for deletion.
void server_free_flagged_clients(struct server_node* server)
{
    mtx_lock(&server->free_flagged_clients_mutex);

    struct client_node* node = server->flagged_clients_list;
    while (node != NULL)
    {
        struct client_node* next = node->next;
        if (node->status == CLIENT_READY_TO_DELETE)
            server_free_flagged_client(server, node);
        node = next;
    }

    mtx_unlock(&server->free_flagged_clients_mutex);
}

// Disconnect all connected clients from a server node's clients list. This will free
// all client nodes from memory. This should only be called when the Bulb protocol is
// being terminated as it frees the server node from memory.
void server_disconnect_all_clients(struct server_node* server)
{
    TRIE_DFS(server->clients, node,
    {
        struct client_node* client = (struct client_node*)node;
        _client_flag_for_deletion(client, false);
        _client_close(client);
    });
    trie_free(server->clients);

    mtx_destroy(&server->free_flagged_clients_mutex);
    mtx_destroy(&server->server_emptied_mutex);
    cnd_destroy(&server->server_emptied_signal);

    tagged_free(server, TAG_SERVER_NODE);
}

// Print a message to the server console by triggering a SERVER_PRINT_STDOUT exception.
void server_printf(struct server_node* server, char* buffer, ...)
{
#ifdef SERVER
    va_list argv;
    char message[2048];
    va_start(argv, buffer);
    vsnprintf(message, sizeof(message), buffer, argv);
    va_end(argv);

    server_throw_exception(server->bulb_server, SERVER_PRINT_STDOUT, (void*)message);
#endif
}