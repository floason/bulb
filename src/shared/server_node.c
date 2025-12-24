// floason (C) 2025
// Licensed under the MIT License.

#include <string.h>

#include "unisock.h"
#include "server_node.h"
#include "userinfo_obj.h" // TODO: remove

// Close and free a client node.
static void _client_close(struct client_node* client)
{
    if (client->userinfo != NULL)
        free(client->userinfo);
    if (client->sock != INVALID_SOCKET)
    {
        shutdown(client->sock, SHUT_RDWR);
        closesocket(client->sock);
    }
    free(client);
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

    client->next = NULL;
    client->prev = server->clients;
    if (server->clients == NULL)
        server->clients = client;
}   

// Disconnect a client from a server node's clients list. This will free the client
// node from memory.
void server_disconnect_client(struct server_node* server, struct client_node* client)
{
    // TODO: remove
    if (client->userinfo)
        printf("client %s disconnect\n", client->userinfo->name);

    if (client->prev)
        client->prev->next = client->next;
    else
        server->clients = client->next;
    if (client->next)
        client->next->prev = client->prev;

    _client_close(client);
}

// Disconnect all connected clients from a server node's clients list. This will free
// all client nodes from memory.
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