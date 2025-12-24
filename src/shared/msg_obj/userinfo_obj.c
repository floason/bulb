// floason (C) 2025
// Licensed under the MIT License.

// This object is used for two purposes:
// 1) Authenticate a user connection with the server.
// 2) Store user information of each connected client.

#include <string.h>

#include "unisock.h"
#include "stdout_obj.h"
#include "userinfo_obj.h"

// Read a userinfo_obj object. Returns NULL on failure.
struct bulb_obj* userinfo_obj_read(SOCKET sock, struct bulb_obj* header)
{
    return bulb_obj_template_recv(sock, header);
}

// Write a userinfo_obj object. Returns false on failure.
bool userinfo_obj_write(SOCKET sock, struct userinfo_obj* obj)
{
    return bulb_obj_write(sock, (struct bulb_obj*)obj);
}

// Process a userinfo_obj object.
void userinfo_obj_process(struct userinfo_obj* obj, struct server_node* server, struct client_node* client)
{
#ifdef SERVER
    // This must always be set, so that the server can free the userinfo node
    // appropriately, once the client is disconnected.
    client->userinfo = obj;

    // Reject clients with empty usernames.
    if (strlen(obj->name) == 0)
    {
        stdout_obj_write(client->sock, "Your username cannot be empty!\n");
        client->delete = true;
        return;
    }

    // Reject clients with the same username as another user.
    LOOP_CLIENTS(server->clients, client, node,
    {
        if (strcmp(client->userinfo->name, node->userinfo->name) == 0)
        {
            stdout_obj_write(client->sock, "Sorry, another client is already connected with this name!\n");
            client->delete = true;
            return;
        }
    });

    // Validate the client and log its entry.    
    client->validated = true;
    LOOP_CLIENTS(server->clients, NULL, node,
    {
        char buffer[256];
        snprintf(buffer, sizeof(buffer), "Client \"%s\" has connected\n", client->userinfo->name);
        stdout_obj_write(node->sock, buffer);
    });
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client->addr.sin_addr, ip_str, sizeof(ip_str));
    printf("Client \"%s\" (%s) has connected\n", client->userinfo->name, ip_str);
#endif
}