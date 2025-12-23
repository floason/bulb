// floason (C) 2025
// Licensed under the MIT License.

#pragma once

#include <stdbool.h>

#include "unisock.h"
#include "bulb_obj.h"

// Read a Bulb object from a socket. The object is dynamically allocated and thus
// must be released from memory afterwards.
struct bulb_obj* bulb_obj_read(SOCKET sock, bool* socket_closed);