// floason (C) 2026
// Licensed under the MIT License.

// This object is used for measuring latency a client and server and vice-versa.

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "unisock.h"
#include "bulb_obj.h"
#include "ping_obj.h"
#include "userinfo_obj.h"
#include "update_userinfo_obj.h"

// Read a ping_obj object. Returns NULL on failure.
struct bulb_obj* ping_obj_read(struct mt_socket* sock, struct bulb_obj* header, size_t size)
{
    return bulb_obj_template_recv(sock, header, size);
}

// Write a ping_obj object. Returns false on failure.
bool ping_obj_write(struct mt_socket* sock, bool final_destination)
{
    struct ping_obj obj = { .base.type = BULB_PING,
                            .base.size = sizeof(struct ping_obj),
                            .final_destination = final_destination };
    if (!final_destination)
        timespec_ns_get(&sock->ping_start);

    return bulb_obj_write(sock, (struct bulb_obj*)&obj);
}

// Process a ping_obj object.
void ping_obj_process(struct ping_obj* obj, struct server_node* server, struct client_node* client)
{
    if (obj->final_destination)
    {
        timespec_ns_get(&client->mt_sock.ping_end);
        client->userinfo->info.ping_ms = timespec_diff(&client->mt_sock.ping_end, 
            &client->mt_sock.ping_start, 3);
        client->ready_to_ping = true;
        LOOP_CLIENTS(server, NULL, node, 
        {
            update_userinfo_obj_write(&node->mt_sock, &client->userinfo->info, 
                client->userinfo->info.name);
        });
    }
    else
        ping_obj_write(&client->mt_sock, true);
    tagged_free(obj, TAG_BULB_OBJ);
}