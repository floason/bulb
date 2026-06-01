// floason (C) 2025
// Licensed under the MIT License.

#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <threads.h>
#include <string.h>
#include <time.h>

#include "unisock.h"
#include "networking.h"
#include "bulb_obj.h"

// This is a basic template for reading a Bulb object that has no additional reading
// requirements. Returns NULL on failure.
struct bulb_obj* bulb_obj_template_recv(struct mt_socket* sock, struct bulb_obj* header, size_t size)
{    
    // Validate the size of the object first.
    if (header->size != size)
        return NULL;

    // Copy the buffered data nodes into a new Bulb object instance. This is the 
    // standard method for reading objects.
    struct bulb_obj* obj = quick_malloc(header->size);
    size_t offset = 0;
    do
    {
        struct mt_socket_data_node* node;
        QUEUE_DEQUEUE(node, sock->data_recv_queue, sock->data_recv_tail);
        ASSERT(offset + node->len <= header->size, return NULL, "Too large queued data for object");
        memcpy((char*)obj + offset, node->data, node->len);

        offset += node->len;
        free(node);
    } while (header->size > offset);

    return obj;
}

// Send a Bulb object of an arbitrary type to a socket stream. Returns false on failure.
bool bulb_obj_write(struct mt_socket* sock, struct bulb_obj* obj)
{
    mtx_lock(&sock->write_lock);

    // Create a new mt_socket_data_node object and link it to the socket's data
    // send queue.
    struct mt_socket_data_node* node = (struct mt_socket_data_node*)quick_malloc(
        sizeof(struct mt_socket_data_node) + obj->size);
    memcpy(node->data, (const char*)obj, obj->size);
    node->len = obj->size;
    QUEUE_ENQUEUE(node, sock->data_send_queue, sock->data_send_tail);

    // Additionally, except for received_obj, queue a timestamp node to assess
    // potential timeouts.
    if (obj->type != BULB_RECEIVED)
    {
        struct mt_socket_timeout_node* timeout = (struct mt_socket_timeout_node*)quick_malloc(
            sizeof(struct mt_socket_timeout_node));
        timespec_get(&timeout->send_timestamp, TIME_UTC);
        QUEUE_ENQUEUE(timeout, sock->data_send_timeout_queue, sock->data_send_timeout_tail);
    }

    mt_socket_flag_ready_for_send(sock);
    mtx_unlock(&sock->write_lock);
    return true;
}