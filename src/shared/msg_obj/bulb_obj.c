// floason (C) 2025
// Licensed under the MIT License.

#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <threads.h>

#include "unisock.h"
#include "bulb_obj.h"

// This is a basic template for reading a Bulb object that has no additional reading
// requirements. Returns NULL on failure.
struct bulb_obj* bulb_obj_template_recv(struct mt_socket* sock, struct bulb_obj* header, size_t min_size)
{    
    // Read the socket stream into a buffer and copy its contents into a dynamically
    // allocated object. This is the standard method for reading objects.
    char buffer[RECV_BUFFER_SIZE];
    struct bulb_obj* obj = tagged_malloc(header->size, TAG_BULB_OBJ);
    size_t offset = 0;
    do
    {
        int read = recv(sock->socket, (char*)obj + offset, 
            MIN(header->size - offset, RECV_BUFFER_SIZE), 0);
        if (read == SOCKET_ERROR)
            return NULL;
        offset += read;
    } while (header->size > offset);

    // If the current offset is below the minimum size required, the object must be
    // immediately invalidated as using it can result in a segmentation fault.
    if (offset < min_size)
    {
        tagged_free(obj, TAG_BULB_OBJ);
        return NULL;
    }

    return obj;
}

// Send a Bulb object of an arbitrary type to a socket stream. Returns false on failure.
bool bulb_obj_write(struct mt_socket* sock, struct bulb_obj* obj)
{
    // While bulb_obj_template_recv() should only be called by a single thread per socket,
    // this function in theory can be called using the same socket by multiple threads.
    // This can cause interleaving in data written to clients, particularly if multiple
    // clients are messaging at the same time. Therefore, mutexes are used with this
    // function.
    mtx_lock(&sock->write_lock);

    bool return_value = true;
    size_t remaining = obj->size;
    do
    {
        int written = send(sock->socket, (const char*)obj + (obj->size - remaining), 
            MIN(remaining, RECV_BUFFER_SIZE), 0);
        if (written == SOCKET_ERROR)
        {
            return_value = false;
            goto finish;
        }
        remaining -= written;
    } while (remaining > 0);

finish:
    mtx_unlock(&sock->write_lock);
    return return_value;
}