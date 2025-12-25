// floason (C) 2025
// Licensed under the MIT License.

// This object is used for reporting a disconnecting client to other clients.

#pragma once

#include <stdbool.h>

#include "unisock.h"
#include "bulb_macros.h"
#include "server_node.h"
#include "client_node.h"
#include "bulb_obj.h"
#include "userinfo_obj.h"

struct disconnect_obj
{
    struct bulb_obj base;
    char name[MAX_NAME_LENGTH + 1]; // Names are unique, so this is used as a client identifier.
};

// Read a disconnect_obj object. Returns NULL on failure.
struct bulb_obj* disconnect_obj_read(SOCKET sock, struct bulb_obj* header);

// Write a disconnect_obj object. Returns false on failure.
bool disconnect_obj_write(SOCKET sock, const char* name);

// Process a disconnect_obj object.
void disconnect_obj_process(struct disconnect_obj* obj, struct server_node* server, struct client_node* client);