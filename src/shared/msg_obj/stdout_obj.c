// floason (C) 2025
// Licensed under the MIT License.

// This object is used for writing a buffer to stdout.

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "unisock.h"
#include "server_node.h"
#include "client_node.h"
#include "stdout_obj.h"
#include "userinfo_obj.h"

#ifdef CLIENT
#   include "client.h"
#endif

// Read a stdout_obj. Returns NULL on failure.
struct bulb_obj* stdout_obj_read(struct mt_socket* sock, struct bulb_obj* header, size_t size)
{
    struct stdout_obj* obj = (struct stdout_obj*)bulb_obj_template_recv(sock, header, size);
    if (obj == NULL)
        return NULL;
    obj->buffer[header->size - sizeof(struct stdout_obj) - 1] = '\0';
    return (struct bulb_obj*)obj;
}

// Write a stdout_obj object. Returns false on failure.
bool stdout_obj_write(struct mt_socket* sock, const char* msg)
{
    // The size of the object is the size of the base structure + the length of the message
    // + 1 for the NUL character at the end.
    size_t size = sizeof(struct stdout_obj) + strlen(msg) + 1;
    struct stdout_obj* obj = tagged_malloc(size, TAG_BULB_OBJ);
    obj->base.type = BULB_STDOUT;
    obj->base.size = size;
    strcpy(obj->buffer, msg);

    if (bulb_obj_write(sock, (struct bulb_obj*)obj) == false)
    {
        tagged_free(obj, TAG_BULB_OBJ);
        return false;
    }
    tagged_free(obj, TAG_BULB_OBJ);
    return true;
}

// Process a stdout_obj object.
void stdout_obj_process(struct stdout_obj* obj, struct server_node* server, struct client_node* client)
{
#ifdef CLIENT
    client_throw_exception(client->bulb_client, CLIENT_PRINT_STDOUT, (void*)&obj->buffer);
#else
    // Throw an assert as this should've been caught when the object was received.
    ASSERT(false, return, "stdout_obj found in processing queue from client thread!\n");
#endif

finish:
    tagged_free(obj, TAG_BULB_OBJ);
}