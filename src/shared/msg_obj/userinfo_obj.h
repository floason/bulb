// floason (C) 2025
// Licensed under the MIT License.

// This object is used for two purposes:
// 1) Authenticate a user connection with the server.
// 2) Store user information of each connected client.

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "unisock.h"
#include "bulb_macros.h"
#include "server_node.h"
#include "client_node.h"
#include "bulb_obj.h"

struct userinfo_obj
{
    struct bulb_obj base;

    // Known to all clients.
    char name[MAX_NAME_LENGTH + 1];

    // Used to authenticate the version of the client.
    short major;
    short minor;
    short patch;
};

// Read a userinfo_obj object. Returns NULL on failure.
struct bulb_obj* userinfo_obj_read(SOCKET sock, struct bulb_obj* header, size_t min_size);

// Write a userinfo_obj object. Returns false on failure.
bool userinfo_obj_write(SOCKET sock, struct userinfo_obj* obj);

// Process a userinfo_obj object.
void userinfo_obj_process(struct userinfo_obj* obj, struct server_node* server, struct client_node* client);