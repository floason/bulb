// floason (C) 2026
// Licensed under the MIT License.

// This object is used for measuring latency a client and server and vice-versa.

#pragma once

#include <stdbool.h>
#include <time.h>

#include "unisock.h"
#include "server_node.h"
#include "client_node.h"
#include "bulb_obj.h"

struct ping_obj
{
    struct bulb_obj base;
    bool final_destination;
};

// Read a ping_obj object. Returns NULL on failure.
struct bulb_obj* ping_obj_read(struct mt_socket* sock, struct bulb_obj* header, size_t size);

// Write a ping_obj object. Returns false on failure.
bool ping_obj_write(struct mt_socket* sock, bool final_destination);

// Process a ping_obj object.
void ping_obj_process(struct ping_obj* obj, struct server_node* server, struct client_node* client);