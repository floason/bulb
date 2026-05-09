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
            // implemented on client code and the client disconnect sequence is instead
            // instantiated here. A terminated socket may still result in all client-
            // associated threads being terminated, thus the client's operations have
            // technically been terminated and should be de-allocated.
            //
            // On the server-side, this should not be happening as the appropriate client
            // disconnection checks should already be made.
            if (client->status < CLIENT_FLAGGED_FOR_DELETION)
            {
#ifdef SERVER
                ASSERT(false, { }, "Client thread terminated when client has not yet been flagged " \
                    "for deletion\n");
#endif
                _client_flag_for_deletion(client);
                server_disconnect_client(client->server_node, client, false, true, false);
            }

            if (--client->thread_ref_count == 0)
            {
                // Flag the client as ready to delete and shutdown its connection.
                client->status = CLIENT_READY_TO_DELETE;
                if (client->mt_sock.socket != INVALID_SOCKET)
                    cleanup_mt_socket(&client->mt_sock);
                
#ifdef SERVER
                // If this was the final remaining client connection, signal the server
                // emptying back to the server code.
                mtx_lock(&client->server_node->server_emptied_mutex);
                if (--client->server_node->number_pending_deletion == 0)
                    cnd_broadcast(&client->server_node->server_emptied_signal);
                mtx_unlock(&client->server_node->server_emptied_mutex);
#else
                // If this was the local clinet, exit.
                if (client == localclient)
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

// Handle writing objects in a new thread for the given client connection asynchronously
// from the main client object listen thread, so as to not disrupt the backbone client
// thread if it attempts to communicate with other clients (message_obj in particular).
int server_client_send_thread(void* c)
{
    struct client_node* client = (struct client_node*)c;
    struct server_node* server = client->server_node;

    for (;;)
    {
        mtx_lock(&client->send_thread_lock);
        while (!client_flagged_for_deletion(client) && QUEUE_EMPTY(client->mt_sock.send_queue))
            cnd_wait(&client->mt_sock.send_signal, &client->send_thread_lock);

        // If the client is flagged for deletion, exit only once there's nothing to send.
        if (client_flagged_for_deletion(client) && QUEUE_EMPTY(client->mt_sock.send_queue))
            goto flagged_for_deletion;

        // Handle writing whatever is currently enqueued in the client socket's send
        // queue. Everything must be written before the send thread mutex is unlocked,
        // to allow for complete objects to be transmitted before any client disconnect
        // may be handled.
        do 
        {
            QUEUE_DEQUEUE(struct mt_socket_write_node* node, client->mt_sock.send_queue, 
                client->mt_sock.send_queue_tail);

            int written; 
            while ((written = send(client->mt_sock.socket, node->data, node->len, MSG_NOSIGNAL))
                == SOCKET_ERROR)
            {
                // If the socket is blocking, poll and then try again.
                if (socket_errno() == SOCKET_AGAIN && mt_socket_poll(&client->mt_sock, true))
                    continue;
                tagged_free(node, TAG_TEMP);
                goto flagged_for_deletion;
            }
            tagged_free(node, TAG_TEMP);
        }
        while (!QUEUE_EMPTY(client->mt_sock.send_queue));

        // If the client is flagged for deletion and all pending objects have finally been
        // written, exit.
        if (client_flagged_for_deletion(client))
            goto flagged_for_deletion;

        mtx_unlock(&client->send_thread_lock);
    }

flagged_for_deletion:
    if (client->mt_sock.send_timeout)
    {
        // If a socket timeout occurred, the given client node will actually still be
        // connected to the server. Thus, its disconnect sequence must be initiated.
#ifdef CLIENT
        bulb_printf(client, "\nExceeded client timeout duration!\n");
        server_disconnect_client(server, client, false, false, false);
#else
        server_kick(server, client, "Server timeout duration exceeded");
#endif
    }
    client_set_status(client, CLIENT_READY_TO_DELETE);
    return 0;
}