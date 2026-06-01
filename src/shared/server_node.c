// floason (C) 2025
// Licensed under the MIT License.

// The server_node struct implements the backbone of the Bulb protocol and
// supplements both the client and the server.

#include <string.h>
#include <threads.h>
#include <time.h>

#include "unisock.h"
#include "networking.h"
#include "trie.h"
#include "bulb_macros.h"
#include "bulb_structs.h"
#include "shared_interface.h"
#include "server_node.h"
#include "obj_reader.h"
#include "obj_process.h"
#include "userinfo_obj.h"
#include "disconnect_obj.h"
#include "stdout_obj.h"
#include "ping_obj.h"
#include "received_obj.h"

#ifdef SERVER
#   include "bulb_server.h"
#endif

// Flag a client node for deletion.
static inline void _client_flag_for_deletion(struct client_node* client, bool server_shutdown)
{
    if (client_flagged_for_deletion(client))
        return;

    // Signal to the client that the connection is being shut down.
#ifdef SERVER
    disconnect_obj_write(client->mt_sock, "", server_shutdown);
#endif

    // Flag the client for deletion.
    client_set_status(client, CLIENT_FLAGGED_FOR_DELETION);
    cnd_broadcast(&client->client_delete_signal);
}

// Close and free a client node.
static void _client_close(struct client_node* client)
{
    // The server's client update lock must be locked so that the server does not
    // begin handling this client node's object processing while this client node
    // is being deleted.
    mtx_t* server_client_update_lock = &client->server_node->client_update_lock;
    mtx_lock(server_client_update_lock);

    if (client->mt_sock != NULL)
    {
        client->mt_sock->dealloc_func = NULL;

        // The socket manager should independently handle de-allocating the client's
        // socket object. This function serves as a hint towards the socket's
        // assigned socket manager to free the socket instance when ready.
        mt_socket_shutdown(client->mt_sock);
    }
    mtx_destroy(&client->client_status_lock);
    mtx_destroy(&client->send_thread_lock);
    mtx_destroy(&client->ping_lock);
    cnd_destroy(&client->client_delete_signal);

    if (client->userinfo != NULL)
        free(client->userinfo);
    if (client->next_obj_header != NULL)
        free(client->next_obj_header);
    free(client);

    mtx_unlock(server_client_update_lock);
}

// Read and process an object for a given client.
static bool _server_client_recv(struct server_node* server, struct client_node* client)
{
    // Conclude reading objects from a client that is flagged for deletion.
#ifdef SERVER
    if (client_flagged_for_deletion(client))
    {
        mt_socket_flag_ready_for_closure(client->mt_sock);
        return false;
    }
#endif

    char error_msg[MAX_ERROR_LENGTH + 1] = { 0 };
    bool blocked;
    struct bulb_obj* obj = bulb_obj_read(client->mt_sock, error_msg, sizeof(error_msg), &blocked);
    if (blocked)
        return false;

    if (obj == NULL)
    {
        if (!client_flagged_for_deletion(client))
        {
            if (error_msg[0] != '\0')
                server_kick(server, client, error_msg);
            else
                server_disconnect_client(server, client, true, true, false);
        }
        return false;
    }

    bool is_received_obj = (obj->type == BULB_RECEIVED);
    ASSERT(bulb_process_object(obj, server, client), return false,
        "Failed to process Bulb object of type %d\n", obj->type);

    // The sending end should be notified of the successful object transmission.
    if (!is_received_obj)
        received_obj_write(client->mt_sock);

    return true;
}

// Send all buffered object data for a given client.
static void _server_client_send(struct server_node* server, struct client_node* client)
{
    // Handle writing whatever is currently enqueued in the client socket's send
    // queue. Everything must be written before the send thread mutex is unlocked,
    // to allow for complete objects to be transmitted before any client disconnect
    // may be handled.
    mtx_lock(&client->mt_sock->write_lock);
    while (!QUEUE_EMPTY(client->mt_sock->data_send_queue))
    {
        struct mt_socket_data_node* node;
        QUEUE_DEQUEUE(node, client->mt_sock->data_send_queue, client->mt_sock->data_send_tail);
        do
        {
            int result = mt_socket_send(client->mt_sock, node->data + node->send_offset, 
                node->len - node->send_offset, MSG_NOSIGNAL);

            // Exit on any error.
            if (result <= 0)
            {
                // If the socket is blocking, the data node must be added back to the 
                // start of the queue.
                if (socket_errno() == SOCKET_AGAIN)
                {
                    node->prev = NULL;
                    node->next = client->mt_sock->data_send_queue;
                    if (node->next != NULL)
                        node->next->prev = node;
                    client->mt_sock->data_send_queue = node;
                }
                else
                    free(node);
                goto exit;
            }

            node->send_offset += result;
        } while (node->send_offset < node->len);
        free(node);
    }

    // If the data queue is now empty and the client is flagged for deletion, hint to 
    // the client socket's assigned socket manager instance that it should now be
    // removed from the socket manager.
    if (QUEUE_EMPTY(client->mt_sock->data_send_queue) && client_flagged_for_deletion(client))
        mt_socket_shutdown(client->mt_sock);
exit:
    mtx_unlock(&client->mt_sock->write_lock);
}

// Each client is managed in a single centralised thread operated by the server,
// dependent on whether a client is ready to receive & process or send an object.
static int _server_manage_thread(void* s)
{
    // TODO post-v1: consider SPSC ring buffers before IOCP/epoll overhaul?

    struct server_node* server = (struct server_node*)s;
    struct timespec next_timeout_check;
    struct timespec next_ping;
    timespec_get(&next_timeout_check, TIME_UTC);
    next_timeout_check.tv_sec += 1;
    next_ping = next_timeout_check;
    next_ping.tv_sec += 4;

    for (;;)
    {
        // Wait for any updates from any client, or after a second has elapsed for
        // managing client timeout.
        struct timespec current_timestamp;
        int timeout_sec_diff;
        mtx_lock(&server->client_update_lock);
        timespec_get(&current_timestamp, TIME_UTC);
        while (QUEUE_EMPTY(server->socket_recv_queue) && QUEUE_EMPTY(server->socket_send_queue)
            && (timeout_sec_diff = timespec_diff(&current_timestamp, &next_timeout_check, 0)) < 0
            && !server->cleanup)
        {
            cnd_timedwait(&server->client_update_signal, &server->client_update_lock, &next_timeout_check);
            timespec_get(&current_timestamp, TIME_UTC);
        }

        // If the server node object is being de-allocated from memory, terminate
        // this thread.
        if (server->cleanup)
        {
            cnd_destroy(&server->client_update_signal);
            mtx_unlock(&server->client_update_lock);
            mtx_destroy(&server->client_update_lock);
            free(server);
            return 0;
        }
        
        // Handle timeout.
        if (timeout_sec_diff >= 0)
        {
            next_timeout_check.tv_sec += timeout_sec_diff + 1;

#ifdef SERVER
            // Check for whether to ping each client, which should take place every 5 
            // seconds.
            int ping_sec_diff = timespec_diff(&current_timestamp, &next_ping, 0);
            if (ping_sec_diff >= 0)
                next_ping.tv_sec += 5 * (ping_sec_diff / 5 + 1);

            // Clients being kicked must be linked separately as disconnecting clients
            // within LOOP_CLIENTS() will cause undefined behaviour due to LOOP_CLIENTS()
            // relying on the clients trie, which would otherwise be modified during
            // iteration. Each client node's next/prev pointers should not be actively
            // used at this point. This behaviour may be amended in the future.
            struct client_node* to_kick_head = NULL;
            struct client_node* to_kick_tail = NULL;

            LOOP_CLIENTS(server, NULL, node,
            {
                // If the server has waited more than the timeout duration specified in the
                // server info's timeout_s attribute, the client node must be kicked.
                struct mt_socket_timeout_node* timeout = node->mt_sock->data_send_timeout_queue;
                if (timeout != NULL && (timespec_diff(&current_timestamp, &timeout->send_timestamp, 0) 
                    > server->info.timeout_s))
                {
                    LINKED_LIST_ADD(node, to_kick_head, to_kick_tail);
                }

                // If the client has not timed out, send a ping object if the ping timeout
                // duration has also been exceeded.
                else if (server->info.ping_clients && node->ready_to_ping && ping_sec_diff >= 0)
                {
                    ping_obj_write(node->mt_sock, false);
                    node->ready_to_ping = false;
                }
            });

            struct client_node* to_kick = to_kick_head;
            while (to_kick != NULL)
            {
                struct client_node* next = to_kick->next;
                server_kick(server, to_kick, "Exceeded server timeout duration.");

                // Immediately shut down the client's socket, as there is no successful
                // response being made with the server.
                mt_socket_shutdown(to_kick->mt_sock);
                
                to_kick = next;
            }
#else
            // Check if the local client has timed out.
            struct mt_socket_timeout_node* timeout = localclient->mt_sock->data_send_timeout_queue;
            if (timeout != NULL && localclient->userinfo != NULL
                && (timespec_diff(&current_timestamp, &timeout->send_timestamp, 0)
                    > localclient->userinfo->info.timeout_s
                ) && !client_flagged_for_deletion(localclient))
            {
                bulb_printf_type(localclient, STDOUT_KICK_MSG,
                    "Client timed out while attempting to send data to server!\n");
                server_disconnect_client(server, localclient, false, true, true);
                
                // Immediately shut down the client's socket, as there is no successful
                // response being made with the server.
                mt_socket_shutdown(localclient->mt_sock);
            }
#endif
        }

        // Dequeue a socket from the read queue and attempt to read an object from it.
        struct mt_socket* selected;
        if (!QUEUE_EMPTY(server->socket_recv_queue))
        {
            QUEUE_DEQUEUE(selected, server->socket_recv_queue, server->socket_recv_tail, recv_queue);

            // In order to trigger the next read event, the socket should be added
            // back to the recv() queue if an object was read and processed successfully.
            if (_server_client_recv(server, selected->parent_client))
                QUEUE_ENQUEUE(selected, server->socket_recv_queue, server->socket_recv_tail, recv_queue);
        }
            
        // Dequeue a socket from the write queue and attempt to write an object to it.
        if (!QUEUE_EMPTY(server->socket_send_queue))
        {
            QUEUE_DEQUEUE(selected, server->socket_send_queue, server->socket_send_tail, send_queue);
            _server_client_send(server, selected->parent_client);
        }

        // Unlock the client update lock and continue.
        mtx_unlock(&server->client_update_lock);
    }
}

// Manage the removal of a socket from a socket manager instance.
void _server_sm_manage_socket_removal(struct socket_manager* sm)
{
    struct server_node* server = (struct server_node*)sm->parent_server;

    // Loop through each socket manager instance that isn't the current instance, and
    // attempt to complete a merger.
    LOOP_SOCKET_MANAGERS(server->sm_head, sm, other,
    {
        if (sm_merge(sm, other))
            return;
    });
}

// Manage the deletion of a socket manager instance.
void _server_sm_manage_deallocation(struct socket_manager* sm)
{
    struct server_node* server = (struct server_node*)sm->parent_server;
    LINKED_LIST_REMOVE(sm, server->sm_head, server->sm_tail);
}

// Initialise the server node.
struct server_node* server_shared_node_alloc()
{
    struct server_node* server = quick_malloc(sizeof(struct server_node));
    mtx_init(&server->connection_update_mutex, (mtx_plain | mtx_recursive));
    mtx_init(&server->server_emptied_mutex, mtx_plain);
    cnd_init(&server->server_emptied_signal);

    mtx_init(&server->client_update_lock, mtx_plain | mtx_recursive);
    cnd_init(&server->client_update_signal);
    thrd_create(&server->client_manage_thread, _server_manage_thread, server);
    
    server->clients = trie_new();
    server->clients_info_head = server->clients_info_tail = &server->info;
    return server;
}

// Begin listening to a client's socket. The client's socket object will be
// automatically released from memory as soon as it is disused.
void server_listen_client(struct server_node* server, struct client_node* client)
{
    // Link the client's mt_socket instance to the client and its queues.
    client->mt_sock->parent_client = client;
    client->mt_sock->recv_queue.queue = &server->socket_recv_queue;
    client->mt_sock->recv_queue.tail = &server->socket_recv_tail;
    client->mt_sock->send_queue.queue = &server->socket_send_queue;
    client->mt_sock->send_queue.tail = &server->socket_send_tail;
    client->mt_sock->ready_signal = &server->client_update_signal;
    client->mt_sock->update_lock = &server->client_update_lock;

    // Configure the client's socket to be non-blocking and start provoking
    // recv()/send() operations by adding the client socket to the socket
    // queue.
    mt_socket_configure_non_blocking(client->mt_sock);
    
    // Prior to requesting a socket manager instance to listen to this client's
    // socket, some initial validation checks must be performed. Even if the 
    // client is disconnected at this point, there must still be brief
    // communication for e.g. alerting the user as to why their connection
    // attempt was rejected.
    server->number_connected++;

    // Is the server currently at its max capacity?
    if (server->info.max_clients > 0 && server->number_connected > server->info.max_clients)
    {
        char message[256];
        snprintf(message, sizeof(message), "The server is currently full (max clients: %u).\n",
            server->info.max_clients);
        stdout_obj_write(client->mt_sock, message, STDOUT_KICK_MSG);
        server_disconnect_client(server, client, true, true, true);
    }

    // Configure a socket manager instance to listen to the client's socket.
    struct socket_manager* sm = server->sm_head;
    bool added = false;
    while (sm != NULL)
    {
        if ((added = sm_add(sm, client->mt_sock)))
            break;
        sm = sm->next;
    }

    // If no socket manager instance with spare slots was found, create a new
    // socket manager instance.
    if (!added)
    {
        sm = sm_new();
        sm->parent_server = server;
        sm->update_lock = &server->client_update_lock;
        sm->removed_func = _server_sm_manage_socket_removal;
        sm->dealloc_func = _server_sm_manage_deallocation;
        sm_add(sm, client->mt_sock);
        sm_listen(sm);
        LINKED_LIST_ADD(sm, server->sm_head, server->sm_tail);
    }

    // Flag the socket as ready for recv(). This is required so as to immediately
    // begin reading any objects on the socket.
    mt_socket_flag_ready_for_recv(client->mt_sock);
}

// Connect a new client to a server node's clients list.
void server_connect_client(struct server_node* server, struct client_node* client)
{
    // The client node should already have its socket and userinfo configured.

    LINKED_LIST_ADD(&client->userinfo->info, server->clients_info_head, server->clients_info_tail);
    trie_add(server->clients, client->userinfo->info.name, client);

#ifdef CLIENT
    // Clients are not responsible for managing the socket of each connected client,
    // thus the server_listen_client() function is never called. The server node's
    // number_connected attribute must therefore be incremented here instead.
    if (client != localclient)
        server->number_connected++;
#endif

    // See further client connection (i.e. authentication) code in msg_obj/userinfo_obj.c.
}   

// Start disconnecting a client from a server node's clients list.
void server_disconnect_client(struct server_node* server, 
                              struct client_node* client, 
                              bool print_msg, 
                              bool unlink,
                              bool server_shutdown)
{
    mtx_lock(&server->connection_update_mutex);

#ifdef SERVER
    // If the client did not fail server authentication checks, log whether it disconnected 
    // or if the client attempted to connect but a connection could not be established to 
    // begin with.
    if (print_msg)
    {
        if (client->userinfo != NULL)
        {
            bulb_printf(server, "Client \"%s\" (%s) has disconnected\n", client->userinfo->info.name, 
                client->ip_addr);

            // The server reports this to clients, rather than simply printing this on each
            // client, so that I don't have to re-implement the same code used with stdout_obj
            // that allows client code to format console output properly.
            char buffer[64 + MAX_NAME_LENGTH];
            snprintf(buffer, sizeof(buffer), "Client \"%s\" has disconnected\n",
                client->userinfo->info.name);
            LOOP_CLIENTS(server, client, node, stdout_obj_write(node->mt_sock, buffer, STDOUT_GENERIC));
        }
        else
            bulb_printf(server, "Client from address %s failed to connect\n", 
                client->ip_addr);
    }

    // Synchronise the client's departure with all other clients.
    if (client->status >= CLIENT_VALIDATED)
    {
        LOOP_CLIENTS(server, client, node, 
            disconnect_obj_write(node->mt_sock, client->userinfo->info.name, server_shutdown));
    }
#endif

    // By default, unlink should be toggled as server_disconnect_client should only be
    // called for single clients. However, if the server shuts down, it must loop through
    // every client in order to disconnect them. Unlinking them while traversing the tree
    // is thus likely to cause a segmentation fault.
    if (unlink)
    {
        if (client->userinfo)
        {
            trie_delete(server->clients, client->userinfo->info.name);
            LINKED_LIST_REMOVE(&client->userinfo->info, server->clients_info_head, 
                server->clients_info_tail);
        }
        LINKED_LIST_ADD(client, server->flagged_clients_list, server->flagged_clients_list_tail);
    }

    server->number_connected--;
    if (server_shutdown)
        client->exit_is_orderly = true;
    _client_flag_for_deletion(client, server_shutdown);

    mtx_unlock(&server->connection_update_mutex);
}

// Check if a client is connected without iterating through the entire list of clients.
// Returns the client node if found, othewrise NULL.
struct client_node* server_find_by_name(struct server_node* server, const char* name)
{
    if (strlen(name) > MAX_NAME_LENGTH)
        return NULL;
    return trie_find(server->clients, name);
}

// Kick a client. This should be called from server code only.
void server_kick(struct server_node* server, struct client_node* client, const char* msg)
{
#ifdef SERVER
    // Log the client's departure in the server console.
    bulb_printf(server, "Client \"%s\" (%s) has been kicked from the server%s%s\n", 
        client->userinfo->info.name, client->ip_addr, (strlen(msg) > 0 ? ": " : "."), msg);
    
    // Write to the user itself that they have been kicked.
    char buffer[1024 + MAX_NAME_LENGTH];
    snprintf(buffer, sizeof(buffer), "You have been kicked from the server%s%s\n",
        (strlen(msg) > 0 ? ": " : "."), msg);
    stdout_obj_write(client->mt_sock, buffer, STDOUT_KICK_MSG);

    // Write to all other clients that this user has been kicked.
    snprintf(buffer, sizeof(buffer), "Client \"%s\" has been kicked from the server%s%s\n",
        client->userinfo->info.name, (strlen(msg) > 0 ? ": " : "."), msg);
    LOOP_CLIENTS(server, client, node, stdout_obj_write(node->mt_sock, buffer, STDOUT_GENERIC));

    // Start disconnecting the client.
    server_disconnect_client(server, client, false, true, true);
    return;
#endif

    ASSERT(false, return);
}

// Ban an address. As the client is not always assumed to be in the server, an immediate 
// kick should be invoked separately. This should be called from server code only. Returns 
// false if the client was already banned.
bool server_ban(struct server_node* server, const char* ip_addr, const char* reason)
{
#ifdef SERVER
    struct bulb_ban obj = { .ip_addr = ip_addr, .reason = reason };
    server_throw_exception(server->bulb_server, SERVER_BAN_CLIENT, (void*)&obj);
    return !obj.is_banned;
#endif

    ASSERT(false, return false);
}

// Unbans an address. Returns false if the address was not in the ban database.
bool server_unban(struct server_node* server, const char* ip_addr)
{
#ifdef SERVER
    struct bulb_ban obj = { .ip_addr = ip_addr };
    server_throw_exception(server->bulb_server, SERVER_UNBAN_CLIENT, (void*)&obj);
    return obj.is_banned;
#endif

    ASSERT(false, return false);
}

// Disconnect a client that is flagged for deletion.
void server_free_flagged_client(struct server_node* server, struct client_node* client)
{
    ASSERT(client->status == CLIENT_READY_TO_DELETE, return, 
        "Attempted to delete client \"%s\", which is not ready to delete!", client->userinfo->info.name);

    LINKED_LIST_REMOVE(client, server->flagged_clients_list, server->flagged_clients_list_tail);
    _client_close(client);
}

// Disconnect any clients that are flagged for deletion.
void server_free_flagged_clients(struct server_node* server)
{
    struct client_node* node = server->flagged_clients_list;
    while (node != NULL)
    {
        struct client_node* next = node->next;
        if (node->status == CLIENT_READY_TO_DELETE)
            server_free_flagged_client(server, node);
        node = next;
    }
}

// Disconnect all connected clients from a server node's clients list. This will free
// all client nodes from memory. This should only be called when the Bulb protocol is
// being terminated as it frees the server node from memory.
void server_disconnect_all_clients(struct server_node* server)
{
    mtx_lock(&server->client_update_lock);

    TRIE_DFS(server->clients, node,
    {
        struct client_node* client = (struct client_node*)node;
        _client_flag_for_deletion(client, false);
        _client_close(client);
    });
    trie_free(server->clients);

    mtx_destroy(&server->connection_update_mutex);
    mtx_destroy(&server->server_emptied_mutex);
    cnd_destroy(&server->server_emptied_signal);

    // Signal to the central client management thread that the server must be
    // de-allocated.
    server->cleanup = true;
    cnd_broadcast(&server->client_update_signal);
    mtx_unlock(&server->client_update_lock);
}