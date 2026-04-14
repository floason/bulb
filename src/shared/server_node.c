// floason (C) 2025
// Licensed under the MIT License.

#include <string.h>

#include "unisock.h"
#include "trie.h"
#include "server_node.h"
#include "userinfo_obj.h"

#ifdef SERVER
#   include "disconnect_obj.h"
#   include "stdout_obj.h"
#endif

// Flag a client node for deletion.
static inline void _client_flag_for_deletion(struct client_node* client)
{
    if (client->mt_sock.socket != INVALID_SOCKET)
    {
        shutdown(client->mt_sock.socket, SHUT_RDWR);
        closesocket(client->mt_sock.socket);
        client->mt_sock.socket = INVALID_SOCKET;
    }
#ifdef SERVER
    client_set_status(client, CLIENT_FLAGGED_FOR_DELETION);
#else
    // This is fine as there only exists a single client thread in client code,
    // which is for the local client, which has its own cleanup mechanism.
    client_set_status(client, CLIENT_READY_TO_DELETE);
#endif
}

// Close and free a client node.
static inline void _client_close(struct client_node* client)
{
    if (client->userinfo != NULL)
        tagged_free(client->userinfo, TAG_BULB_OBJ);
    tagged_free(client, TAG_CLIENT_NODE);
}

// Initialize the server node.
void server_node_init(struct server_node* node)
{
    memset(node, 0, sizeof(struct server_node));
    mtx_init(&node->free_flagged_clients_mutex, mtx_plain | mtx_recursive);
    node->clients = trie_new();
}

// Connect a new client to a server node's clients list.
void server_connect_client(struct server_node* server, struct client_node* client)
{
    // The client node should already have its socket configured.

    trie_add(server->clients, client->userinfo->name, client);
    server->number_connected++;

    // See further client connection (i.e. authentication) code in msg_obj/userinfo_obj.c.
}   

// Disconnect a client from a server node's clients list. This will free the client
// node from memory.
void server_disconnect_client(struct server_node* server, struct client_node* client, bool print_msg)
{
#ifdef SERVER
    // If the client did not fail server authentication checks, log whether it disconnected 
    // or if the client attempted to connect but a connection could not be established to 
    // begin with.
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client->addr.sin_addr, ip_str, sizeof(ip_str));
    if (print_msg)
    {
        if (client->userinfo)
        {
            printf("Client \"%s\" (%s) has disconnected\n", client->userinfo->name, ip_str);

            // The server reports this to clients, rather than simply printing this on each
            // client, so that I don't have to re-implement the same code used with stdout_obj
            // that allows client code to format console output properly.
            char buffer[64 + MAX_NAME_LENGTH];
            snprintf(buffer, sizeof(buffer), "Client \"%s\" has disconnected\n",
                client->userinfo->name);
            LOOP_CLIENTS(server, client, node, stdout_obj_write(&node->mt_sock, buffer));
        }
        else
            fprintf(stderr, "Client from address %s failed to connect\n", ip_str);
    }
#endif

    trie_delete(server->clients, client->userinfo->name);
    LINKED_LIST_ADD(client, server->flagged_clients_list, server->flagged_clients_list_tail);

    // Synchronise the client's departure with all other clients.
#ifdef SERVER
    if (client->status == CLIENT_VALIDATED)
    {
        LOOP_CLIENTS(server, client, node, 
            disconnect_obj_write(&node->mt_sock, client->userinfo->name));
    }
#endif

    server->number_connected--;
    _client_flag_for_deletion(client);
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
    fprintf(stderr, "Client \"%s\" (%s) has been kicked from the server: %s", client->userinfo->name, 
        ip_str, msg);
    stdout_obj_write(&client->mt_sock, msg);
    
    // Write to all other clients that this user has been kicked.
    char buffer[1024 + MAX_NAME_LENGTH];
    snprintf(buffer, sizeof(buffer), "Client \"%s\" has been kicked from the server: %s",
        client->userinfo->name, msg);
    LOOP_CLIENTS(server, client, node, stdout_obj_write(&node->mt_sock, buffer));

    // Start disconnecting the client.
    server_disconnect_client(server, client, false);
#endif

    ASSERT(false, return);
}

// Disconnect a client that is flagged for deletion.
void server_free_flagged_client(struct server_node* server, struct client_node* client)
{
    ASSERT(client->status == CLIENT_READY_TO_DELETE, return, 
        "Attempted to delete client \"%s\", which is not ready to delete!", client->userinfo->name);

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
// being terminated.
void server_disconnect_all_clients(struct server_node* server)
{
    TRIE_DFS(server->clients, node, {},
    {
        struct client_node* client = (struct client_node*)node;
        _client_flag_for_deletion(client);
        _client_close(client);
    }, {});
    trie_free(server->clients);
}