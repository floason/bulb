// floason (C) 2025
// Licensed under the MIT License.

// This object is used for synchronising a new client connection for each client.

#pragma once

#include <stdbool.h>

#include "unisock.h"
#include "bulb_obj.h"
#include "userinfo_obj.h"

struct connect_obj
{
    struct bulb_obj base;
    struct userinfo_obj userinfo;
};

// Read a connect_obj object. Returns NULL on failure.
struct bulb_obj* connect_obj_read(SOCKET sock, struct bulb_obj* header);

// Write a connect_obj object. Returns false on failure.
bool connect_obj_write(SOCKET sock, struct userinfo_obj* userinfo);

// Process a connect_obj object.
void connect_obj_process(struct connect_obj* obj, struct server_node* server, struct client_node* client);