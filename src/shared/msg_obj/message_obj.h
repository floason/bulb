// floason (C) 2025
// Licensed under the MIT License.

// This object is used for sending a message to other clients, through the server.

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "unisock.h"
#include "bulb_macros.h"
#include "server_node.h"
#include "client_node.h"
#include "bulb_obj.h"

struct message_obj
{
    struct bulb_obj base;
    char name[MAX_NAME_LENGTH + 1];
    char message[MAX_MESSAGE_LENGTH + 1];
    bool from_server;
};

// Read a message_obj object. Returns NULL on failure.
struct bulb_obj* message_obj_read(struct mt_socket* sock, struct bulb_obj* header, size_t size);

// Write a message_obj object. Returns false on failure.
bool message_obj_write(struct mt_socket* sock, const char* name, const char* msg, bool from_server);

// Process a message_obj object.
void message_obj_process(struct message_obj* obj, struct server_node* server, struct client_node* client);