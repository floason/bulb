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

#ifdef CLIENT
#   include "bulb_client.h"
#endif

// Read a disconnect_obj object. Returns NULL on failure.
struct bulb_obj* disconnect_obj_read(struct mt_socket* sock, struct bulb_obj* header, size_t size)
{
    return bulb_obj_template_recv(sock, header, size);
}

// Write a disconnect_obj object. Returns false on failure.
bool disconnect_obj_write(struct mt_socket* sock, const char* name)
{
    struct disconnect_obj obj = { };
    obj.base.type = BULB_DISCONNECT;
    obj.base.size = sizeof(struct disconnect_obj);
    strncpy(obj.name, name, sizeof(obj.name));
    return bulb_obj_write(sock, (struct bulb_obj*)&obj);
}

// Process a disconnect_obj object.
void disconnect_obj_process(struct disconnect_obj* obj, 
                            struct server_node* server, 
                            struct client_node* client)
{
#ifdef CLIENT
    LOOP_CLIENTS(server, NULL, node,
    {
        if (strcmp(obj->name, node->userinfo->info.name) == 0)
        {
            if (node == client)
                client_throw_exception(client->bulb_client, CLIENT_DISCONNECT, NULL);
            else
                server_disconnect_client(server, node, true, true);
            break;
        }
    })
#endif
    tagged_free(obj, TAG_BULB_OBJ);
}