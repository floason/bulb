// floason (C) 2026
// Licensed under the MIT License.

#include "util.h"
#include "client_node.h"

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
    switch (flag)
    {
        case CLIENT_VALIDATED:
            if (client->status < CLIENT_VALIDATED)
                client->status = CLIENT_VALIDATED;
            break;
        case CLIENT_FLAGGED_FOR_DELETION:
            if (client->status < CLIENT_FLAGGED_FOR_DELETION)
                client->status = CLIENT_FLAGGED_FOR_DELETION;
            break;
        default:
            client->status = flag;
    }
}