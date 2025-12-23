// floason (C) 2025
// Licensed under the MIT License.

#include <stdbool.h>

#include "unisock.h"
#include "obj_process.h"
#include "server_node.h"
#include "client_node.h"
#include "bulb_obj.h"
#include "stdout_obj.h"

// Process a Bulb object. The object will be free()'d afterwards. Returns false on error.
bool bulb_process_object(struct bulb_obj* obj, struct server_node* server, struct client_node* client)
{
    switch (obj->type)
    {
        case BULB_OBJ:
            ASSERT(false, { return true; }, "Test object was found in stream");
        case BULB_STDOUT:
            stdout_obj_process((struct stdout_obj*)obj, server, client);
            return true;
        default:
            return false;
    }
}