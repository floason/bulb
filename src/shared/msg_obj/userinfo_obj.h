// floason (C) 2025
// Licensed under the MIT License.

// This object is used for three purposes:
// 1) Authenticate a user connection with the server.
// 2) Store user information of each connected client.
// 3) Copy server information to a connecting client.

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "unisock.h"
#include "bulb_structs.h"
#include "server_node.h"
#include "client_node.h"
#include "bulb_obj.h"

struct userinfo_obj
{
    struct bulb_obj base;
    struct bulb_userinfo info;
};

// Read a userinfo_obj object. Returns NULL on failure.
struct bulb_obj* userinfo_obj_read(struct mt_socket* sock, struct bulb_obj* header, size_t size);

// Write a userinfo_obj object. Returns false on failure.
bool userinfo_obj_write(struct mt_socket* sock, struct userinfo_obj* obj);

// Process a userinfo_obj object.
void userinfo_obj_process(struct userinfo_obj* obj, struct server_node* server, struct client_node* client);