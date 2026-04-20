// floason (C) 2025
// Licensed under the MIT License.

// See userinfo_obj.c for client validation code.

#include <string.h>
#include <stdbool.h>
#include <threads.h>
#include <stdatomic.h>
#include <time.h>

#include "unisock.h"
#include "server.h"
#include "cmds.h"
#include "obj_reader.h"
#include "obj_process.h"
#include "message_obj.h"
#include "stdout_obj.h"

#ifdef WIN32
    static WSADATA wsa_data;
#endif

// Start reading and processing message objects in a new thread for a given client
// connection.
static int _server_client_recv_thread(void* c)
{
    struct client_node* client = (struct client_node*)c;
    struct server_node* server = client->server_node;

    char error_msg[MAX_ERROR_LENGTH + 1] = { 0 };
    for (;;)
    {
        struct bulb_obj* obj = bulb_obj_read(&client->mt_sock, error_msg, sizeof(error_msg));
        if (obj == NULL && !client_flagged_for_deletion(client))
        {
            if (error_msg[0] != '\0')
                server_kick(server, client, error_msg);
            else
                server_disconnect_client(server, client, true, true);
        }

        if (client_flagged_for_deletion(client))
            goto flagged_for_deletion;

        ASSERT(bulb_process_object(obj, server, client), goto flagged_for_deletion,
            "Failed to process Bulb object of type %d\n", obj->type);
    }

flagged_for_deletion:
    cnd_signal(&client->mt_sock.send_signal);
    client_set_status(client, CLIENT_READY_TO_DELETE);
    return 0;
}

// Handle writing objects in a new thread for a given client connection asynchronously
// from the main client object listen thread, so as to not disrupt the backbone client
// thread if it attempts to communicate with other clients (message_obj in particular).
static int _server_client_send_thread(void* c)
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

            int written = send(client->mt_sock.socket, node->data, node->len, MSG_NOSIGNAL);
            tagged_free(node, TAG_TEMP);
            if (written == SOCKET_ERROR)
                goto flagged_for_deletion;
        }
        while (!QUEUE_EMPTY(client->mt_sock.send_queue));

        // If the client is flagged for deletion and all pending objects have finally been
        // written, exit.
        if (client_flagged_for_deletion(client))
            goto flagged_for_deletion;

        mtx_unlock(&client->send_thread_lock);
    }

flagged_for_deletion:
    client_set_status(client, CLIENT_READY_TO_DELETE);
    return 0;
}

// A thread is created for this function whenever a server instance starts listening
// for incoming client connections to accept.
static int _server_listen_thread(void* s)
{
    struct bulb_server* server = (struct bulb_server*)s;

    for (;;)
    {
        int length = sizeof(struct sockaddr_in);
        struct client_node* node = tagged_malloc(sizeof(struct client_node), TAG_CLIENT_NODE);
        node->server_node = server->server_node;
        
        SOCKET sock = accept(server->listen_sock, (struct sockaddr*)&node->addr, &length);
        if (sock == INVALID_SOCKET)
        {
            tagged_free(node, TAG_CLIENT_NODE);
            if (server->disconnect_handled 
                || server->listen_sock == INVALID_SOCKET
                || !server_throw_exception(server, SERVER_CLIENT_ACCEPT_FAIL, NULL))
                return 0;
            continue;
        }

        node->thread_ref_count = 2;
        setup_mt_socket(&node->mt_sock, sock);
        mtx_init(&node->send_thread_lock, mtx_plain);
        mtx_init(&node->client_status_lock, mtx_plain);
        thrd_create(&node->recv_thread, _server_client_recv_thread, (void*)node);
        thrd_create(&node->send_thread, _server_client_send_thread, (void*)node);

        // Periodically deallocate clients marked for deletion.
        server_free_flagged_clients(server->server_node);
    }

    return 0;
}

// Create a new server instance. error_state can be NULL. Returns NULL on error.
struct bulb_server* server_init(uint16_t port, enum server_error_state* error_state)
{
    struct bulb_server* server = tagged_malloc(sizeof(struct bulb_server), TAG_BULB_SERVER);
    server->listen_sock = INVALID_SOCKET;

    // If Winsock is being used, Winsock must be initialized beforehand.
    int result = 0;
#ifdef WIN32
    result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
    ASSERT(result == 0, 
    {
        server->error_state = SERVER_WINSOCK_FAIL;
        goto fail; 
    }, "Winsock 2.2 failed to start:%d\n", result);
#endif

    char port_buffer[6] = { 0 }; // 0-65535 + \0
    snprintf(port_buffer, sizeof(port_buffer), "%hu", port);

    // Resolve the hostname to listen to.
    struct addrinfo* addr_ptr = NULL;
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;          // Use IPv4.
    hints.ai_socktype = SOCK_STREAM;    // Use reliable, segmented communication.
    hints.ai_flags = AI_PASSIVE;        // Required for binding.
    result = getaddrinfo(NULL, port_buffer, &hints, &addr_ptr);
    if (result != 0)
    {
        server->error_state = SERVER_ADDRESS_FAIL;
        goto fail; 
    }

    // Create and bind the socket for the server to listen to client connections.
    server->listen_sock = socket(addr_ptr->ai_family, addr_ptr->ai_socktype,
        addr_ptr->ai_protocol);
    if (server->listen_sock == INVALID_SOCKET)
    { 
        server->error_state = SERVER_LISTEN_SOCKET_FAIL;
        goto fail; 
    }
    result = bind(server->listen_sock, addr_ptr->ai_addr, (int)addr_ptr->ai_addrlen);
    if (result == SOCKET_ERROR)
    {
        server->error_state = SERVER_LISTEN_SOCKET_FAIL;
        goto fail; 
    }
    freeaddrinfo(addr_ptr);

    server->server_node = server_shared_node_alloc();
    server->server_node->bulb_server = server;
    
    bulb_register_shared_cmds();
    return server;

fail:
    if (error_state != NULL)
        *error_state = server->error_state;
    if (server->listen_sock != INVALID_SOCKET)
        closesocket(server->listen_sock);
    if (addr_ptr != NULL)
        freeaddrinfo(addr_ptr);
    tagged_free(server, TAG_BULB_SERVER);
    return NULL;
}

// Set a custom exception handler.
void server_set_exception_handler(struct bulb_server* server, server_exception_func func)
{
    ASSERT(server, return);
    server->exception_handler = func;
}

// Start handling a non-critical exception. If the exception returns false,
// it will be re-evaluated as a critical error and this function will return
// false.
bool server_throw_exception(struct bulb_server* server, 
                            enum server_error_state error, 
                            void* data)
{
    ASSERT(server, return false);
    ASSERT(server->exception_handler != NULL, return false, "Server exception handler is not set.\n");

    // Invoke the exception handler. If it returns false, the server should shut down.
    bool result = server->exception_handler(server, error, false, data);
    server->error_state = error;
    if (!result)
        return false;
    return true;
}

// Start handling a critical error.
void server_throw_critical_error(struct bulb_server* server, 
                                        enum server_error_state error, 
                                        void* data)
{
    ASSERT(server, return);
    ASSERT(server->exception_handler != NULL, return, "Server exception handler is not set.\n");

    // Invoke the exception handler.
    server->exception_handler(server, error, true, data);
    server->error_state = error;
}

// Start accepting new clients asynchronously. Returns false on error.
bool server_listen(struct bulb_server* server)
{
    ASSERT(server, return false);
    ASSERT(!server->is_listening, return false; );

    int result = listen(server->listen_sock, SOMAXCONN);
    if (result == SOCKET_ERROR)
    { 
        server->error_state = SERVER_LISTEN_SOCKET_FAIL;
        return false; 
    }
    server->is_listening = true;

    thrd_create(&server->listen_thread, _server_listen_thread, (void*)server);
    return true;
}

// Process server input. Returns true if a command was detected, otherwise false.
bool server_input(struct bulb_server* server, const char* msg, bool* cmd_success)
{
    ASSERT(server, return false);
    ASSERT(server->is_listening, return false; );

    // Check if the first non-whitespace character of the message is a slash.
    for (int i = 0, len = strlen(msg); i < len; i++)
    {
        if (isspace(msg[i]))
            continue;
        if (msg[i] != '/')
            break;
        
        bool result = bulb_parse_cmd_input(server->server_node, msg + i + 1);
        if (cmd_success != NULL)
            *cmd_success = result;
        return true;
    }
    
    LOOP_CLIENTS(server->server_node, NULL, node, 
        message_obj_write(&node->mt_sock, "[SERVER]", msg, true));
    return false;
}

// Shut the server down in an orderly way. It is the caller's responsibility to
// free the server afterwards, as this function does not guarantee the prompt
// deletion of each previously-connected client.
void server_shutdown(struct bulb_server* server, int timeout)
{
    ASSERT(server, return);
    ASSERT(server->is_listening, return; );
    
    // Shutdown the server's listen socket to prevent any new clients from joining.
    closesocket(server->listen_sock);
    server->listen_sock = INVALID_SOCKET;

    LOOP_CLIENTS(server->server_node, NULL, node, 
    {
        stdout_obj_write(&node->mt_sock, "The server has been shut down.\n");
        server_disconnect_client(server->server_node, node, true, false);
    });

    struct timespec timestamp;
    timespec_get(&timestamp, TIME_UTC);

    // Wait for all final client objects to be written before the server closes. 
    // The server should timeout after some time if some clients inevitably don't 
    // respond.
    struct timespec current = timestamp;
    timestamp.tv_sec += timeout;
    mtx_lock(&server->server_node->server_emptied_mutex);
    while (server->server_node->number_pending_deletion > 0 && timespec_cmp(&timestamp, &current) == 1)
    {
        cnd_timedwait(&server->server_node->server_emptied_signal, 
            &server->server_node->server_emptied_mutex, &timestamp);
        timespec_get(&current, TIME_UTC);
    }

    server_throw_exception(server, SERVER_FINISH, NULL);
}

// Shut down and free a server instance.
void server_free(struct bulb_server* server)
{
    ASSERT(server, return);

#ifdef WIN32
    WSACleanup();
#endif

    if (server->listen_sock != INVALID_SOCKET)
    {
        closesocket(server->listen_sock);
        server->listen_sock = INVALID_SOCKET;
    }

    server_disconnect_all_clients(server->server_node);
    tagged_free(server, TAG_BULB_SERVER);

    bulb_cmds_cleanup();
}