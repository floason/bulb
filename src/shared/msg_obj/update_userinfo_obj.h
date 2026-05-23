// floason (C) 2025
// Licensed under the MIT License.

// This object is used for updating a given node's userinfo object's details.
// The given node is either a client node or the server node.

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "unisock.h"
#include "networking.h"
#include "bulb_macros.h"
#include "bulb_structs.h"
#include "server_node.h"
#include "client_node.h"
#include "bulb_obj.h"

struct update_userinfo_obj
{
    struct bulb_obj base;
    struct bulb_userinfo updated_info;
    char client_name[MAX_NAME_LENGTH + 1];
};

// Read an update_userinfo_obj object. Returns NULL on failure.
struct bulb_obj* update_userinfo_obj_read(struct mt_socket* sock, struct bulb_obj* header, size_t size);

// Write an update_userinfo_obj object. Returns false on failure.
bool update_userinfo_obj_write(struct mt_socket* sock, 
                               struct bulb_userinfo* userinfo, 
                               const char* client_name);

// Process an update_userinfo_obj object.
void update_userinfo_obj_process(struct update_userinfo_obj* obj, 
                                 struct server_node* server, 
                                 struct client_node* client);