// floason (C) 2025
// Licensed under the MIT License.

#include <string.h>

#include "unisock.h"
#include "obj_reader.h"
#include "stdout_obj.h"
#include "userinfo_obj.h"
#include "connect_obj.h"
#include "disconnect_obj.h"
#include "message_obj.h"

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

    // If read does not return a valid length, the connection has likely been closed.
    if (read <= 0)
    {
        *socket_closed = (read != SOCKET_ERROR);
        return NULL;
    }
    
    // A switch table is used to select the exact read function to use for reading the
    // given object from the given socket stream. The size of each object is passed
    // as a parameter to each non-default object, in order to validate against
    // objects that could crash the server from invalid clients.
    struct bulb_obj* header = (struct bulb_obj*)buffer;
    switch (header->type)
    {
        case BULB_OBJ:
            return _bulb_obj_read(sock, header);
        case BULB_STDOUT:
            // + 1 is added to the minimum size to provide buffer space for the NUL
            // character.
            return stdout_obj_read(sock, header, sizeof(struct stdout_obj) + 1);
        case BULB_USERINFO:
            return userinfo_obj_read(sock, header, sizeof(struct userinfo_obj));
        case BULB_CONNECT:
            return connect_obj_read(sock, header, sizeof(struct connect_obj));
        case BULB_DISCONNECT:
            return disconnect_obj_read(sock, header, sizeof(struct disconnect_obj));
        case BULB_MESSAGE:
            return message_obj_read(sock, header, sizeof(struct message_obj));
        default:
            ASSERT(false, return NULL, "Invalid obj type %d\n", header->type);
    }
}