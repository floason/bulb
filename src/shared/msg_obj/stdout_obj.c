// floason (C) 2025
// Licensed under the MIT License.

// This object is used for writing a buffer to stdout.

#include "unisock.h"
#include "server_node.h"
#include "client_node.h"
#include "bulb_obj.h"
#include "stdout_obj.h"

// Read a stdout_obj. Returns NULL on failure.
struct bulb_obj* stdout_obj_read(SOCKET sock, struct bulb_obj* header)
{
    return bulb_obj_template_recv(sock, header);
}

// Write a stdout_obj object. Returns false on failure.
bool stdout_obj_write(SOCKET sock, const char* msg)
{
    // The size of the object is the size of the base structure + the length of the message
    // + 1 for the NUL character at the end.
    size_t size = sizeof(struct stdout_obj) + strlen(msg) + 1;
    struct stdout_obj* obj = quick_malloc(size);
    obj->base.type = BULB_STDOUT;
    obj->base.size = size;
    strcpy(obj->buffer, msg);

    if (bulb_obj_write(sock, (struct bulb_obj*)obj) == false)
    {
        free(obj);
        return false;
    }
    free(obj);
    return true;
}

// Process a stdout_obj object.
void stdout_obj_process(struct stdout_obj* obj, struct server_node* server, struct client_node* client)
{
    printf("%s", obj->buffer);
    free(obj);
}