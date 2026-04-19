// floason (C) 2026
// Licensed under the MIT License.

#include <stdatomic.h>
#include <threads.h>

#include "util.h"
#include "client_node.h"
#include "server_node.h"

#ifdef CLIENT
    struct client_node* localclient;
#endif

// Is a client being prepared for deletion?
bool client_flagged_for_deletion(struct client_node* client)
{
    return client->status == CLIENT_FLAGGED_FOR_DELETION || client->status == CLIENT_READY_TO_DELETE;
}

// Update a client's status, if appropriate.
void client_set_status(struct client_node* client, enum client_status flag)
{
    mtx_lock(&client->client_status_lock);
    switch (flag)
    {
        case CLIENT_VALIDATED:
            if (client->status < CLIENT_VALIDATED)
                client->status = CLIENT_VALIDATED;
            
            break;
        case CLIENT_FLAGGED_FOR_DELETION:
            if (client->status < CLIENT_FLAGGED_FOR_DELETION)
            {
                client->status = CLIENT_FLAGGED_FOR_DELETION;
                client->server_node->number_pending_deletion++;
            }
            break;
#ifdef SERVER  
        // The server code currently employs multiple threads for each client node,
        // thus the client should only be marked ready to delete once all threads
        // have been exited.
        case CLIENT_READY_TO_DELETE:
            if (--client->thread_ref_count == 0)
            {
                client->status = CLIENT_READY_TO_DELETE;
                if (--client->server_node->number_pending_deletion == 0)
                    cnd_signal(&client->server_node->server_emptied_signal);
            }
            break;
#endif
        default:
            client->status = flag;
            break;
    }
    mtx_unlock(&client->client_status_lock);
}