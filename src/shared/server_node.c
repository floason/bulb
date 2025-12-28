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
    client->prev = server->clients_tail;
    if (server->clients_tail)
        server->clients_tail->next = client;
    server->clients_tail = client;
    if (server->clients == NULL)
        server->clients = client;

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
            LOOP_CLIENTS(server->clients, client, node, 
                stdout_obj_write(node->sock, buffer));
        }
        else
            fprintf(stderr, "Client from address %s failed to connect\n", ip_str);
    }
#endif

    if (client->prev)
        client->prev->next = client->next;
    else
        server->clients = client->next;
    if (client->next)
        client->next->prev = client->prev;
    else
        server->clients_tail = client->prev;

    // Synchronise the client's departure with all other clients.
#ifdef SERVER
    if (client->validated)
        LOOP_CLIENTS(server->clients, client, node, 
            disconnect_obj_write(node->sock, client->userinfo->name));
#endif

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