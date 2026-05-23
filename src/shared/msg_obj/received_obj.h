// floason (C) 2026
// Licensed under the MIT License.

// This object is used to dequeue the next timestamp node from the a given
// client's timestamps queue, to mark that the next object has been received
// successfully on the other end.

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "unisock.h"
#include "networking.h"
#include "bulb_obj.h"
#include "server_node.h"
#include "client_node.h"

struct received_obj
{
    struct bulb_obj base;
};

// Read a received_obj object. Returns NULL on failure.
struct bulb_obj* received_obj_read(struct mt_socket* sock, struct bulb_obj* header, size_t size);

// Write a received_obj object. userinfo can be NULL if validate_only is true. 
// Returns false on failure.
bool received_obj_write(struct mt_socket* sock);

// Process a received_obj object.
void received_obj_process(struct received_obj* obj, struct server_node* server, struct client_node* client);