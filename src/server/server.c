// floason (C) 2025
// Licensed under the MIT License.

// See userinfo_obj.c for client validation code.

#include <string.h>
#include <stdbool.h>
#include <threads.h>
#include <stdatomic.h>
#include <time.h>

#include "unisock.h"
#include "networking.h"
#include "bulb_structs.h"
#include "bulb_server.h"
#include "bulb_banlist.h"
#include "server_node.h"
#include "client_node.h"
#include "cmds.h"
#include "obj_process.h"
#include "message_obj.h"
#include "stdout_obj.h"

#ifdef WIN32
    static WSADATA wsa_data;
#endif

// Manage the connection of new clients.
static int _server_listen_thread(void* s)
{
    struct bulb_server* server = (struct bulb_server*)s;

    for (;;)
    {
        int length = sizeof(struct sockaddr_in);
        struct client_node* node = quick_malloc(sizeof(struct client_node));
        node->server_node = server->server_node;
        client_shared_node_init(node);
        
        SOCKET sock = accept(server->server_node->listen_sock, (struct sockaddr*)&node->addr, &length);
        if (sock == INVALID_SOCKET)
        {
            free(node);
            if (server->disconnecting 
                || server->server_node->listen_sock == INVALID_SOCKET
                || !server_throw_exception(server, SERVER_CLIENT_ACCEPT_FAIL, NULL))
                return 0;
            continue;
        }

        // Get the connecting IP address.
        inet_ntop(AF_INET, &node->addr.sin_addr, node->ip_addr, sizeof(node->ip_addr));
        
        // Initialize the multithreaded socket object for this client.
        node->mt_sock = mt_socket_new(sock);
        node->mt_sock->dealloc_func = client_set_ready_to_delete_from_sock;
        server_listen_client(server->server_node, node);
        
        // Periodically deallocate clients marked for deletion.
        server_free_flagged_clients(server->server_node);
    }

    return 0;
}

// Create a new server instance. error_state can be NULL. Returns NULL on error.
struct bulb_server* server_init(uint16_t port, enum server_error_state* error_state)
{
    struct bulb_server* server = quick_malloc(sizeof(struct bulb_server));

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
    SOCKET listen_sock = socket(addr_ptr->ai_family, addr_ptr->ai_socktype,
        addr_ptr->ai_protocol);
    if (listen_sock == INVALID_SOCKET)
    { 
        server->error_state = SERVER_LISTEN_SOCKET_FAIL;
        goto fail; 
    }
    result = bind(listen_sock, addr_ptr->ai_addr, (int)addr_ptr->ai_addrlen);
    if (result == SOCKET_ERROR)
    {
        server->error_state = SERVER_LISTEN_SOCKET_FAIL;
        goto fail; 
    }
    freeaddrinfo(addr_ptr);

    server->server_node = server_shared_node_alloc();
    server->server_node->bulb_server = server;
    server->server_node->listen_sock = listen_sock;
    
    bulb_cmds_init();
    bulb_register_server_cmds();
    return server;

fail:
    if (error_state != NULL)
        *error_state = server->error_state;
    if (server->server_node != NULL)
        server_disconnect_all_clients(server->server_node);
    if (listen_sock != INVALID_SOCKET)
        closesocket(listen_sock);
    if (addr_ptr != NULL)
        freeaddrinfo(addr_ptr);
    free(server);
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

    // Invoke the exception handler.
    bool result = server->exception_handler(server, error, false, data);
    server->error_state = error;
    if (result)
        return true;
    
    // If the exception was not handled, evaluate it further.
    switch (error)
    {
        case SERVER_BAN_CLIENT:
        {
            // Default to using the Bulb flat-file banlist database.
            struct bulb_ban* obj = (struct bulb_ban*)data;
            server_banlist_addip(server, obj->ip_addr, obj->reason, &obj->is_banned);
            return true;
        }
        case SERVER_UNBAN_CLIENT:
        {
            // Default to using the Bulb flat-file banlist database.
            struct bulb_ban* obj = (struct bulb_ban*)data;
            server_banlist_removeip(server, obj->ip_addr, &obj->is_banned);
            return true;
        }
        case SERVER_IS_CLIENT_BANNED:
        {
            // Read from the Bulb flat-file banlist database.
            struct bulb_ban* obj = (struct bulb_ban*)data;
            obj->is_banned = server_banlist_isbanned(server, obj->ip_addr, &obj->reason);
            return true;
        }
        case SERVER_BANLIST_SAVE:
            // Save to the Bulb flat-file banlist database.
            server_banlist_store(server);
            return true;

        default:
            // There is no exception handler available for this exception.
            // In this case, the exception should be elevated to a critical error.
            return false;
    }
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

// Set the server's identifiable information.
void server_set_userinfo(struct bulb_server* server, struct bulb_userinfo* userinfo)
{
    memcpy(&server->server_node->info, userinfo, sizeof(server->server_node->info));
}

// Start accepting new clients asynchronously. Returns false on error.
bool server_listen(struct bulb_server* server)
{
    ASSERT(server, return false);
    ASSERT(!server->is_listening, return false; );

    if (server->server_node->info.init_bulb_banlist_database && !server_banlist_load(server))
    {
        server->error_state = SERVER_BANLIST_INIT_FAIL;
        return false;
    }

    int result = listen(server->server_node->listen_sock, SOMAXCONN);
    if (result == SOCKET_ERROR)
    { 
        server->error_state = SERVER_LISTEN_SOCKET_FAIL;
        return false; 
    }
    server->is_listening = true;

    thrd_create(&server->listen_thread, _server_listen_thread, server);
    return true;
}

// Get the number of connected clients on the server. Returns -1 on failure.
BULB_API int server_num_connected(struct bulb_server* server)
{
    ASSERT(server, return -1);
    ASSERT(server->server_node, return -1);
    return server->server_node->number_connected;
}

// Get a linked list of each connected client's userinfo object. Returns NULL on
// failure, or if no clients are connected.
struct bulb_userinfo* server_get_userinfo_list(struct bulb_server* server)
{
    ASSERT(server, return NULL);
    ASSERT(server->server_node, return NULL);
    ASSERT(server->server_node->clients_info_head, return NULL);
    return server->server_node->clients_info_head->next;
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
        message_obj_write(node->mt_sock, "[SERVER]", msg, true));
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
    closesocket(server->server_node->listen_sock);
    server->server_node->listen_sock = INVALID_SOCKET;

    LOOP_CLIENTS(server->server_node, NULL, node, 
    {
        stdout_obj_write(node->mt_sock, "The server has been shut down.\n", STDOUT_SERVER_SHUTDOWN);
        server_disconnect_client(server->server_node, node, true, false, true);
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
    
    server->disconnecting = true;
    if (server->server_node->listen_sock != INVALID_SOCKET)
    {
        SOCKET sock = server->server_node->listen_sock;
        server->server_node->listen_sock = INVALID_SOCKET;
        shutdown(sock, SHUT_RDWR);
        closesocket(sock);
    }

    server_disconnect_all_clients(server->server_node);
    server_banlist_close(server);
    free(server);

    bulb_cmds_cleanup();

#ifdef WIN32
    WSACleanup();
#endif
}