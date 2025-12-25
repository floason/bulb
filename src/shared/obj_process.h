// floason (C) 2025
// Licensed under the MIT License.

#include <stdbool.h>

#include "server_node.h"
#include "client_node.h"
#include "bulb_obj.h"

// Process a Bulb object. The object may be free()'d afterwards. Returns false on error.
bool bulb_process_object(struct bulb_obj* obj, struct server_node* server, struct client_node* client);