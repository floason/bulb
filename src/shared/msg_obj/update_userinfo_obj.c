// floason (C) 2025
// Licensed under the MIT License.

// This object is used for reporting a disconnecting client to other clients.

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "unisock.h"
#include "networking.h"
#include "bulb_structs.h"
#include "server_node.h"
#include "client_node.h"
#include "bulb_obj.h"
#include "update_userinfo_obj.h"
#include "userinfo_obj.h"

// Read an update_userinfo_obj object. Returns NULL on failure.
struct bulb_obj* update_userinfo_obj_read(struct mt_socket* sock, struct bulb_obj* header, size_t size)
{
    return bulb_obj_template_recv(sock, header, size);
}

// Write an update_userinfo_obj object. Returns false on failure.
bool update_userinfo_obj_write(struct mt_socket* sock, 
                               struct bulb_userinfo* userinfo, 
                               const char* client_name)
{
    struct update_userinfo_obj obj = { .base.type = BULB_UPDATE_USERINFO,
                                       .base.size = sizeof(struct update_userinfo_obj) };
    memcpy(&obj.updated_info, userinfo, sizeof(obj.updated_info));
    strncpy(obj.client_name, client_name, sizeof(obj.client_name));
    return bulb_obj_write(sock, (struct bulb_obj*)&obj);
}

// Process an update_userinfo_obj object.
void update_userinfo_obj_process(struct update_userinfo_obj* obj, 
                                 struct server_node* server, 
                                 struct client_node* client)
{
#ifdef CLIENT
    struct bulb_userinfo* userinfo;
    if (strlen(obj->client_name) > 0)
    {
        struct client_node* node = server_find_by_name(server, obj->client_name);
        ASSERT(node != NULL, goto not_found, "Could not find node by name \"%s!\"\n", obj->client_name);
        userinfo = &node->userinfo->info;
    }
    else
        userinfo = &server->info;
    userinfo->ping_ms = obj->updated_info.ping_ms;
#endif
not_found:
    tagged_free(obj, TAG_BULB_OBJ);
}