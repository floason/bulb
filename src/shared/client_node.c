// floason (C) 2026
// Licensed under the MIT License.

#include <stdatomic.h>
#include <threads.h>

#include "util.h"
#include "client_node.h"
#include "server_node.h"

#ifdef CLIENT
#   include "bulb_client.h"

    struct client_node* localclient;
#endif

// Flag a client for deletion.
void _client_flag_for_deletion(struct client_node* client)
{
    client->status = CLIENT_FLAGGED_FOR_DELETION;
    client->server_node->number_pending_deletion++;
}

// Initialize a managed (i.e. not other client nodes for client code)
// client node's shared variables in place.
void client_shared_node_init(struct client_node* client)
{
    mtx_init(&client->send_thread_lock, mtx_plain);
    mtx_init(&client->client_status_lock, mtx_plain);
    mtx_init(&client->ping_lock, mtx_plain);
    cnd_init(&client->client_delete_signal);
}

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
                _client_flag_for_deletion(client);
            break; 
        case CLIENT_READY_TO_DELETE:
            // Flagging for deletion requires that the client status has not yet been
            // elevated to CLIENT_FLAGGED_FOR_DELETION. The same check is not
            // implemented for flagging a client as ready to delete, as the sole
            // determinant for whether a client is now ready to delete is if its socket
            // instance is NULL. This is likely to be the case if a client disconnects
            // gracefully, meaning the socket manager code is likely to start the client
            // disconnect sequence.
            if (client->status < CLIENT_FLAGGED_FOR_DELETION)
            {
                _client_flag_for_deletion(client);
                server_disconnect_client(client->server_node, client, true, true, false);
            }

            // This client node should only finally be flagged for deletion when its socket
            // has been disassociated.
            if (client->mt_sock == NULL)
            {
                // Flag the client as ready to delete and shutdown its connection.
                client->status = CLIENT_READY_TO_DELETE;
                
#ifdef SERVER
                // If this was the final remaining client connection, signal the server
                // emptying back to the server code.
                mtx_lock(&client->server_node->server_emptied_mutex);
                if (--client->server_node->number_pending_deletion == 0)
                    cnd_broadcast(&client->server_node->server_emptied_signal);
                mtx_unlock(&client->server_node->server_emptied_mutex);
#else
                // If this was the local client, exit.
                if (client == localclient && !client->bulb_client->disconnecting)
                {
                    client_throw_exception(client->bulb_client, 
                        (client->exit_is_orderly ? CLIENT_DISCONNECT : CLIENT_FORCE_DISCONNECT), NULL);
                }
#endif
            }
            break;
        default:
            client->status = flag;
            break;
    }
    mtx_unlock(&client->client_status_lock);
}

// Flag a client node as ready to delete, given its socket instance.
void client_set_ready_to_delete_from_sock(struct mt_socket* sock)
{
    struct client_node* client = (struct client_node*)sock->parent_client;
    client->mt_sock = NULL;
    client_set_status(client, CLIENT_READY_TO_DELETE);
}