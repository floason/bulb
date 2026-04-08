// floason (C) 2025
// Licensed under the MIT License.

#include <string.h>

#include "unisock.h"
#include "server_node.h"
#include "userinfo_obj.h"

#ifdef SERVER
#   include "disconnect_obj.h"
#   include "stdout_obj.h"
#endif

#define LINK_CLIENT(CLIENT, HEAD, TAIL)         \
    {                                           \
        CLIENT->next = NULL;                    \
        CLIENT->prev = TAIL;                    \
        if (TAIL != NULL)                       \
            TAIL->next = CLIENT;                \
        TAIL = CLIENT;                          \
        if (HEAD == NULL)                       \
            HEAD = CLIENT;                      \
    }                                           \

#define UNLINK_CLIENT(CLIENT, HEAD, TAIL)       \
    {                                           \
        if (CLIENT->prev)                       \
            CLIENT->prev->next = CLIENT->next;  \
        else                                    \
            HEAD = CLIENT->next;                \
        if (CLIENT->next)                       \
            CLIENT->next->prev = CLIENT->prev;  \
        else                                    \
            TAIL = CLIENT->prev;                \
    }                                           \

// Flag a client node for deletion.
static inline void _client_flag_for_deletion(struct client_node* client)
{
    if (client->mt_sock.socket != INVALID_SOCKET)
    {
        shutdown(client->mt_sock.socket, SHUT_RDWR);
        closesocket(client->mt_sock.socket);
    }
    client->flag_for_deletion = true;
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
}

// Connect a new client to a server node's clients list.
void server_connect_client(struct server_node* server, struct client_node* client)
{
    // The client node should already have its socket configured.

    LINK_CLIENT(client, server->clients, server->clients_tail);
    server->number_connected++;

    // See further client connection (i.e. authentication) code in msg_obj/userinfo_obj.c.
}   

// Disconnect a client from a server node's clients list. This will free the client
// node from memory.
void server_disconnect_client(struct server_node* server, struct client_node* client)
{
#ifdef SERVER
    // If the client did not fail server authentication checks, log whether it disconnected 
    // or if the client attempted to connect but a connection could not be established to 
    // begin with.
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client->addr.sin_addr, ip_str, sizeof(ip_str));
    if (!client->delete)
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

    UNLINK_CLIENT(client, server->clients, server->clients_tail);
    LINK_CLIENT(client, server->flagged_clients_list, server->flagged_clients_list_tail);

    // Synchronise the client's departure with all other clients.
#ifdef SERVER
    if (client->validated)
    {
        LOOP_CLIENTS(server, client, node, 
            disconnect_obj_write(&node->mt_sock, client->userinfo->name));
    }
#endif

    server->number_connected--;
    _client_flag_for_deletion(client);
}

// Kick a client. This should be called from server code only, and is assumed to be
// called only from object code too.
void server_kick(struct server_node* server, struct client_node* client, const char* msg)
{
#ifdef SERVER
    // Log the client's departure in the server console and to the client itself.
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client->addr.sin_addr, ip_str, sizeof(ip_str));
    printf("Client \"%s\" (%s) has been kicked from the server: %s", client->userinfo->name, ip_str, 
        msg);
    stdout_obj_write(&client->mt_sock, msg);
    
    // Write to all other clients that this user has been kicked.
    char buffer[1024 + MAX_NAME_LENGTH];
    snprintf(buffer, sizeof(buffer), "Client \"%s\" has been kicked from the server: %s",
        client->userinfo->name, msg);
    LOOP_CLIENTS(server, client, node, stdout_obj_write(&node->mt_sock, buffer));

    // Tell the server thread to disconnect the client node after the calling object
    // is finished processing.
    client->delete = true;
#endif
}

// Disconnect any clients that are flagged for deletion. This does not use
// LOOP_CLIENTS(), as it would otherwise be blocked by this function, thus
// resulting in a deadlock.
void server_free_flagged_clients(struct server_node* server)
{
    struct client_node* node = server->flagged_clients_list;
    while (node != NULL)
    {
        struct client_node* next = node->next;
        if (node->flag_for_deletion)
            _client_close(node);
        node = next;
    }
    
    server->flagged_clients_list = NULL;
    server->flagged_clients_list_tail = NULL;
}

// Disconnect all connected clients from a server node's clients list. This will free
// all client nodes from memory. This should only be called when the Bulb protocol is
// being terminated.
void server_disconnect_all_clients(struct server_node* server)
{
    struct client_node* next = server->clients;
    while (next)
    {
        struct client_node* node = next;
        next = next->next;
        _client_close(node);
    }
    server->clients = NULL;
}