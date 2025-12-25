// floason (C) 2025
// Licensed under the MIT License.

#include "unisock.h"
#include "bulb_obj.h"

// This is a basic template for reading a Bulb object that has no additional reading
// requirements. Returns NULL on failure.
struct bulb_obj* bulb_obj_template_recv(SOCKET sock, struct bulb_obj* header)
{
    char buffer[RECV_BUFFER_SIZE];
    struct bulb_obj* obj = quick_malloc(header->size);
    size_t offset = 0;
    do
    {
        int read = recv(sock, buffer, min(header->size - offset, sizeof(buffer)), 0);
        ASSERT(read != SOCKET_ERROR, return NULL, "Socket error during bulb_obj_template_recv()\n");
        memcpy((char*)obj + offset, buffer, read);
        offset += read;
    } while (header->size > offset);
    return obj;
}

// Send a Bulb object of an arbitrary type to a socket stream. Returns false on failure.
bool bulb_obj_write(SOCKET sock, struct bulb_obj* obj)
{
    size_t remaining = obj->size;
    do
    {
        int written = send(sock, (const char*)obj, min(remaining, RECV_BUFFER_SIZE), 0);
        if (written == SOCKET_ERROR)
            return false;
        remaining -= written;
    } while (remaining > 0);
    return true;
}