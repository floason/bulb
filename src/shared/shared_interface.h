// floason (C) 2025
// Licensed under the MIT License.

#pragma once

#include "bulb_macros.h"

#ifdef CLIENT
#   define BULB_CONSOLE localclient
#else
#   define BULB_CONSOLE server
#endif

// Print a message to the given node by triggering a *_PRINT_STDOUT exception.
void bulb_printf(void* bulb_node, const char* buffer, ...);

// Print a message to the given node by triggering a *_PRINT_STDOUT exception,
// using a custom stdout type.
void bulb_printf_type(void* bulb_node, enum stdout_type type, const char* buffer, ...);