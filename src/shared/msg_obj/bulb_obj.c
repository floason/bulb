// floason (C) 2025
// Licensed under the MIT License.

/*
 * Object reading/processing is due for an overall re-write.
 *
 * While the original model was efficient for basic asynchronous object retrieval
 * and sending, it has since grown organically and now requires the use of polling
 * so as to determine the current socket state. This model is not efficient if
 * multiple threads are polling on the exact same socket. I have not yet actually
 * decided to re-write the entire model to use a single event polling thread per
 * client socket connection, as I have other features to introduce first.
 *
 * Except this re-write to take place before v1.
*/

#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <threads.h>
#include <string.h>

#include "unisock.h"
#include "bulb_obj.h"

// This is a basic template for reading a Bulb object that has no additional reading
// requirements. Returns NULL on failure.
struct bulb_obj* bulb_obj_template_recv(struct mt_socket* sock, struct bulb_obj* header, size_t size)
{    
    // Validate the size of the object first.
    if (header->size != size)
        return NULL;

    // Read the socket stream into a buffer and copy its contents into a dynamically
    // allocated object. This is the standard method for reading objects.
    char buffer[RECV_BUFFER_SIZE];
    struct bulb_obj* obj = tagged_malloc(header->size, TAG_BULB_OBJ);
    size_t offset = 0;
    do
    {
        int read; 
        while ((read = recv(sock->socket, (char*)obj + offset, 
            MIN(header->size - offset, RECV_BUFFER_SIZE), 0)) == SOCKET_ERROR)
        {
            // If the socket is blocking, poll and then try again.
            if (socket_errno() == SOCKET_AGAIN && mt_socket_poll(sock, false))
                continue;
            tagged_free(obj, TAG_BULB_OBJ);
            return NULL;
        }
        offset += read;
    } while (header->size > offset);

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

    size_t remaining = obj->size;
    do
    {
        size_t written = MIN(remaining, RECV_BUFFER_SIZE);
        struct mt_socket_write_node* node = (struct mt_socket_write_node*)tagged_malloc(
            sizeof(struct mt_socket_write_node) + written, TAG_TEMP);
        memcpy(node->data, (const char*)obj + (obj->size - remaining), written);
        node->len = written;

        QUEUE_ENQUEUE(node, sock->send_queue, sock->send_queue_tail);
        remaining -= written;
    } while (remaining > 0);
    
    cnd_signal(&sock->send_signal);
    mtx_unlock(&sock->write_lock);
    return true;
}