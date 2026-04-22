// floason (C) 2025
// Licensed under the MIT License.

// This object is used for three purposes:
// 1) Authenticate a user connection with the server.
// 2) Store user information of each connected client.
// 3) Copy server information to a connecting client.

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "unisock.h"
#include "bulb_version.h"
#include "stdout_obj.h"
#include "userinfo_obj.h"
#include "connect_obj.h"

// Read a userinfo_obj object. Returns NULL on failure.
struct bulb_obj* userinfo_obj_read(struct mt_socket* sock, struct bulb_obj* header, size_t size)
{
    return bulb_obj_template_recv(sock, header, size);
}

// Write a userinfo_obj object. Returns false on failure.
bool userinfo_obj_write(struct mt_socket* sock, struct userinfo_obj* obj)
{
    return bulb_obj_write(sock, (struct bulb_obj*)obj);
}

// Process a userinfo_obj object.
void userinfo_obj_process(struct userinfo_obj* obj, struct server_node* server, struct client_node* client)
{
#ifdef SERVER
    // Reject clients with empty usernames.
    if (strlen(obj->info.name) == 0)
    {
        stdout_obj_write(&client->mt_sock, "Your username cannot be empty!\n");
        goto kick_client;
    }

    // Reject clients with non-renderable usernames.
    if (!str_isprint(obj->info.name))
    {
        stdout_obj_write(&client->mt_sock, "Your username must contain only displayable characters!\n");
        goto kick_client;
    }

    // Get the connecting IP address.
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client->addr.sin_addr, ip_str, sizeof(ip_str));

    // Reject the client if its version does not match that of the server.
    if (obj->info.major != MAJOR || obj->info.minor != MINOR || obj->info.patch != PATCH)
    {
        // Attempt to inform the client that its version number is incorrect.
        char client_buffer[256];
        snprintf(client_buffer, sizeof(client_buffer), "Your client version is %d.%d.%d, however the "   \
            "server expects a client version of %d.%d.%d!\n", obj->info.major, obj->info.minor, 
            obj->info.patch, MAJOR, MINOR, PATCH);
        stdout_obj_write(&client->mt_sock, client_buffer);

        // Report the invalid version of the connecting client to the server 
        // console.
        server_printf(server, "Client \"%s\" (%s) failed to connect as its version is %d.%d.%d (server " \
            "expects version %d.%d.%d)\n", obj->info.name, ip_str, obj->info.major, obj->info.minor, 
            obj->info.patch, MAJOR, MINOR, PATCH);

        goto kick_client;
    }

    // Reject the client if it has the same username as another user.
    bool client_kicked = false;
    LOOP_CLIENTS(server, client, node,
    {
        if (strcmp(obj->info.name, node->userinfo->info.name) == 0)
        {
            stdout_obj_write(&client->mt_sock, 
                "Sorry, another client is already connected with that name!\n");
            server_printf(server, "Client \"%s\" (%s) failed to connect as the given username is "      \
                "already occupied\n", obj->info.name, ip_str);
            
            client_kicked = true;
            break;
        }
    });
    if (client_kicked)
        goto kick_client;

    // Validate the client and log its entry.
    client->userinfo = obj;
    client_set_status(client, CLIENT_VALIDATED);
    server_connect_client(server, client);
    LOOP_CLIENTS(server, NULL, node,
    {
        char buffer[64 + MAX_NAME_LENGTH];
        snprintf(buffer, sizeof(buffer), "Client \"%s\" has connected\n", client->userinfo->info.name);
        stdout_obj_write(&node->mt_sock, buffer);
    });
    connect_obj_write(&client->mt_sock, NULL, true);
    server_printf(server, "Client \"%s\" (%s) has connected\n", client->userinfo->info.name, ip_str);

    // Send the server's userinfo node to the client.
    struct userinfo_obj server_obj;
    memcpy(&server_obj.info, &server->info, sizeof(server_obj.info));
    server_obj.base.type = BULB_USERINFO;
    server_obj.base.size = sizeof(server_obj);
    userinfo_obj_write(&client->mt_sock, &server_obj);

    // Synchronise the client list on each client.
    LOOP_CLIENTS(server, client, node, 
    {
        connect_obj_write(&client->mt_sock, node->userinfo, false);
        connect_obj_write(&node->mt_sock, client->userinfo, false);
    });
    
    return;

kick_client:
    server_disconnect_client(server, client, false, true);
    return;
#else
    memcpy(&server->info, &obj->info, sizeof(server->info));
#endif
}