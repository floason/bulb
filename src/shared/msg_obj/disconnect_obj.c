// floason (C) 2025
// Licensed under the MIT License.

// This object is used for reporting a disconnecting client to other clients.

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "unisock.h"
#include "disconnect_obj.h"
#include "bulb_obj.h"
#include "userinfo_obj.h"

// Read a disconnect_obj object. Returns NULL on failure.
struct bulb_obj* disconnect_obj_read(SOCKET sock, struct bulb_obj* header, size_t min_size)
{
    return bulb_obj_template_recv(sock, header, min_size);
}

// Write a disconnect_obj object. Returns false on failure.
bool disconnect_obj_write(SOCKET sock, const char* name)
{
    struct disconnect_obj obj = { };
    obj.base.type = BULB_DISCONNECT;
    obj.base.size = sizeof(struct disconnect_obj);
    strncpy(obj.name, name, sizeof(obj.name));
    return bulb_obj_write(sock, (struct bulb_obj*)&obj);
}

// Process a disconnect_obj object.
void disconnect_obj_process(struct disconnect_obj* obj, struct server_node* server, struct client_node* client)
{
    LOOP_CLIENTS(server->clients, client, node,
    {
        if (strcmp(obj->name, node->userinfo->name) == 0)
        {
            server_disconnect_client(server, node);
            return;
        }
    })
}