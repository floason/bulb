// floason (C) 2025
// Licensed under the MIT License.

#include "unisock.h"
#include "obj_reader.h"
#include "stdout_obj.h"

// Handle reading a root Bulb object.
static struct bulb_obj* _bulb_obj_read(SOCKET sock, struct bulb_obj* header)
{
    // We need to flush the bulb_obj object out of the buffer first.
    char discard[sizeof(struct bulb_obj)];
    recv(sock, discard, sizeof(discard), 0);
    
    struct bulb_obj* obj = quick_malloc(sizeof(struct bulb_obj));
    memcpy(obj, header, sizeof(struct bulb_obj));
    return obj;
}

// Read a Bulb object from a socket. The object is dynamically allocated and thus
// must be released from memory afterwards.
struct bulb_obj* bulb_obj_read(SOCKET sock, bool* socket_closed)
{
    // The purpose of this function is to read a single object that's currently
    // buffered. Streamed data buffered by multiple send() calls may be read in
    // only a single recv() call, so the very first read this function does will
    // only peek into the socket stream. The final number of bytes that will be
    // read will only correspond to the data size of the final object.
    *socket_closed = false;

    char buffer[sizeof(struct bulb_obj)];
    int read = recv(sock, buffer, sizeof(buffer), MSG_PEEK);
    ASSERT(read != SOCKET_ERROR, { return NULL; }, "Socket error during bulb_obj_read()\n");

    // If read is zero, the connection has likely been closed.
    if (read == 0)
    {
        *socket_closed = true;
        return NULL;
    }
    
    struct bulb_obj* header = (struct bulb_obj*)buffer;
    switch (header->type)
    {
        case BULB_OBJ:
            return _bulb_obj_read(sock, header);
        case BULB_STDOUT:
            return stdout_obj_read(sock, header);
        default:
            ASSERT(false, { return NULL; }, "Invalid obj type %d\n", header->type);
    }
}