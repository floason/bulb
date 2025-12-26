// floason (C) 2025
// Licensed under the MIT License.

// This object is used for sending a message to other clients, through the server.

#include <stdbool.h>
#include <stddef.h>

#include "unisock.h"
#include "bulb_macros.h"
#include "server_node.h"
#include "client_node.h"
#include "stdout_obj.h"
#include "userinfo_obj.h"
#include "message_obj.h"

// Read a message_obj object. Returns NULL on failure.
struct bulb_obj* message_obj_read(SOCKET sock, struct bulb_obj* header, size_t min_size)
{
    return bulb_obj_template_recv(sock, header, min_size);
}

// Write a message_obj object. Returns false on failure.
bool message_obj_write(SOCKET sock, const char* msg)
{
    struct message_obj obj;
    obj.base.type = BULB_MESSAGE;
    obj.base.size = sizeof(struct message_obj);
    strncpy(obj.message, msg, sizeof(obj.message));
    return bulb_obj_write(sock, (struct bulb_obj*)&obj);
}

// Process a message_obj object.
void message_obj_process(struct message_obj* obj, struct server_node* server, struct client_node* client)
{
    char buffer[MAX_NAME_LENGTH + 2 + MAX_MESSAGE_LENGTH + 2]; // NAME: MESSAGE\n\0
    snprintf(buffer, sizeof(buffer), "%s: %s\n", client->userinfo->name, obj->message);
    printf("%s", buffer);
    LOOP_CLIENTS(server->clients, client, node, stdout_obj_write(node->sock, buffer));
}