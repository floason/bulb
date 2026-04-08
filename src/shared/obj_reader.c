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

// Read a Bulb object from a socket. The object is dynamically allocated and thus
// must be released from memory afterwards.
struct bulb_obj* bulb_obj_read(struct mt_socket* sock, char* error_msg, size_t len)
{
    // The purpose of this function is to read a single object that's currently
    // buffered. Streamed data buffered by multiple send() calls may be read in
    // only a single recv() call, so the very first read this function does will
    // only peek into the socket stream. The final number of bytes that will be
    // read will only correspond to the data size of the final object.
    memset(error_msg, 0, len);

    // Peek into the socket stream to read the object header. In the case that 
    // significant data fragmentation occurs (although this should be very unlikely), 
    // this function must busywait until the buffer is filled.
    char buffer[sizeof(struct bulb_obj)];
    int read;
    while ((read = recv(sock->socket, buffer, sizeof(buffer), MSG_PEEK)) < (int)sizeof(struct bulb_obj))
    {
        // If read does not return a valid length, the connection has likely been closed.
        if (read <= 0)
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
            // This object should not be received whatsoever.
#ifdef CLIENT
            ASSERT(false, return NULL, "Test object was found in stream")
#else
            snprintf(error_msg, len, "Client attempted to send test object\n");
#endif
            return NULL;
        case BULB_STDOUT:
#ifdef SERVER
            // Because this object is of variable length and incorporates zero character 
            // filtering, this object must NOT be sent by client code as it is inherently 
            // dangerous.
            snprintf(error_msg, len, "Client attempted to send stdout_obj\n");
            return NULL;
#endif

            // It is difficult to perform size validations due to the variadic size of 
            // this object.
            return stdout_obj_read(sock, header, header->size);
        case BULB_USERINFO:
            return userinfo_obj_read(sock, header, sizeof(struct userinfo_obj));
        case BULB_CONNECT:
            return connect_obj_read(sock, header, sizeof(struct connect_obj));
        case BULB_DISCONNECT:
            return disconnect_obj_read(sock, header, sizeof(struct disconnect_obj));
        case BULB_MESSAGE:
            return message_obj_read(sock, header, sizeof(struct message_obj));
        default:
#ifdef CLIENT
            ASSERT(false, return NULL, "Invalid obj type %d\n", header->type);
#else
            snprintf(error_msg, len, "Client attempted to send invalid obj type %d\n", header->type);
#endif
            return NULL;
    }
}