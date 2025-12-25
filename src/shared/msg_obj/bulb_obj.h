// floason (C) 2025
// Licensed under the MIT License.

// All Bulb objects should inherit from this struct.

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "unisock.h"

#define RECV_BUFFER_SIZE    512

enum bulb_obj_type
{
    BULB_OBJ,

    BULB_STDOUT,
    BULB_USERINFO,
    BULB_CONNECT,
    BULB_DISCONNECT,
    BULB_MESSAGE
};

struct bulb_obj
{
    enum bulb_obj_type type;
    size_t size;
};

// This is a basic template for reading a Bulb object that has no additional reading
// requirements. Returns NULL on failure.
struct bulb_obj* bulb_obj_template_recv(SOCKET sock, struct bulb_obj* header);

// Send a Bulb object of an arbitrary type to a socket stream. Returns false on failure.
bool bulb_obj_write(SOCKET sock, struct bulb_obj* obj);