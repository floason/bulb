// floason (C) 2026
// Licensed under the MIT License.

// This object is used to dequeue the next timestamp node from the a given
// client's timestamps queue, to mark that the next object has been received
// successfully on the other end.

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "unisock.h"
#include "networking.h"
#include "bulb_obj.h"
#include "received_obj.h"
#include "server_node.h"
#include "client_node.h"

// Read a received_obj object. Returns NULL on failure.
struct bulb_obj* received_obj_read(struct mt_socket* sock, struct bulb_obj* header, size_t size)
{
    return bulb_obj_template_recv(sock, header, size);
}

// Write a received_obj object. userinfo can be NULL if validate_only is true. 
// Returns false on failure.
bool received_obj_write(struct mt_socket* sock)
{
    struct received_obj obj = { .base.type = BULB_RECEIVED, 
                                .base.size = sizeof(struct received_obj) };
    return bulb_obj_write(sock, (struct bulb_obj*)&obj);
}

// Process a received_obj object.
void received_obj_process(struct received_obj* obj, struct server_node* server, struct client_node* client)
{
    if (QUEUE_EMPTY(client->mt_sock))
    {
#ifdef SERVER
        server_kick(client->server_node, client,
            "Client attempted to dequeue from empty timeout nodes queue");
        return;
#else
        ASSERT(false, return, "Client attempted to dequeue from empty timeout nodes queue\n");
#endif
    }
    
    struct mt_socket_timeout_node* node;
    QUEUE_DEQUEUE(node, client->mt_sock->data_send_timeout_queue,
        client->mt_sock->data_send_timeout_tail);
    tagged_free(node, TAG_TEMP);

    tagged_free(obj, TAG_BULB_OBJ);
}