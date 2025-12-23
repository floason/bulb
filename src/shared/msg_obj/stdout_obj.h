// floason (C) 2025
// Licensed under the MIT License.

// This object is used for writing a buffer to stdout.

#pragma once

#include <stdbool.h>

#include "unisock.h"
#include "server_node.h"
#include "client_node.h"
#include "bulb_obj.h"

struct stdout_obj
{
    struct bulb_obj base;
    char buffer[];  // strlen() can be used to determine the size of the string to output
};

// Read a stdout_obj object. Returns NULL on failure.
struct bulb_obj* stdout_obj_read(SOCKET sock, struct bulb_obj* header);

// Write a stdout_obj object. Returns false on failure.
bool stdout_obj_write(SOCKET sock, const char* msg);

// Process a stdout_obj object.
void stdout_obj_process(struct stdout_obj* obj, struct server_node* server, struct client_node* client);