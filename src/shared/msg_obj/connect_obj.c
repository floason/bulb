// floason (C) 2025
// Licensed under the MIT License.

// This object is used for synchronising a new client connection for each client.
// It is also used for validating a client that just connected.

#include <stdbool.h>
#include <stddef.h>

#include "unisock.h"
#include "connect_obj.h"
#include "userinfo_obj.h"
#include "server_node.h"

// Read a connect_obj object. Returns NULL on failure.
struct bulb_obj* connect_obj_read(SOCKET sock, struct bulb_obj* header, size_t min_size)
{
    return bulb_obj_template_recv(sock, header, min_size);
}

// Write a connect_obj object. userinfo can be NULL if validate_only is true. 
// Returns false on failure.
bool connect_obj_write(SOCKET sock, struct userinfo_obj* userinfo, bool validate_only)
{
    struct connect_obj obj = { };
    obj.base.type = BULB_CONNECT;
    obj.base.size = sizeof(struct connect_obj);
    obj.validate_only = validate_only;
    if (userinfo != NULL)
        memcpy(&obj.userinfo, userinfo, sizeof(struct userinfo_obj));
    return bulb_obj_write(sock, (struct bulb_obj*)&obj);
}

// Process a connect_obj object.
void connect_obj_process(struct connect_obj* obj, struct server_node* server, struct client_node* client)
{
    // This should only be called on each client after the server has finished validating a new client.

    // Because of the aforementioned precondition, we can mark this client as validated, if not done so
    // already.
    if (obj->validate_only)
    {
        client->validated = true;
        return;
    }

    struct client_node* node = quick_malloc(sizeof(struct client_node));
    node->validated = true;
    node->server_node = server;
    node->userinfo = quick_malloc(sizeof(struct userinfo_obj));
    memcpy(node->userinfo, &obj->userinfo, sizeof(struct userinfo_obj));
 
    server_connect_client(server, node);
}