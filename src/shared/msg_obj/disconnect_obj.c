// floason (C) 2025
// Licensed under the MIT License.

// This object is used for reporting a disconnecting client to other clients.

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "unisock.h"
#include "networking.h"
#include "server_node.h"
#include "client_node.h"
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
bool disconnect_obj_write(struct mt_socket* sock, const char* name, bool server_shutdown)
{
    struct disconnect_obj obj = { .base.type = BULB_DISCONNECT,
                                  .base.size = sizeof(struct disconnect_obj),
                                  .server_shutdown = server_shutdown };
    strncpy(obj.name, name, sizeof(obj.name));
    return bulb_obj_write(sock, (struct bulb_obj*)&obj);
}

// Process a disconnect_obj object.
void disconnect_obj_process(struct disconnect_obj* obj, 
                            struct server_node* server, 
                            struct client_node* client)
{
#ifdef CLIENT
    if (strlen(obj->name) == 0)
        server_disconnect_client(server, client, false, true, obj->server_shutdown);
    else
    {
        struct client_node* node = server_find_by_name(server, obj->name);
        ASSERT(node != NULL, goto not_found, "Could not find node by name \"%s\"!\n", obj->name);
        ASSERT(node != client, goto not_found, 
            "Server object sent disconnect object using localclient name\n");
        server_disconnect_client(server, node, true, true, obj->server_shutdown);
    }
#endif
not_found:
    free(obj);
}