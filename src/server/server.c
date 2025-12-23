// floason (C) 2025
// Licensed under the MIT License.

#include <string.h>

#include "unisock.h"
#include "server.h"
#include "stdout_obj.h"

// Start handling a non-critical error.
static bool _server_handle_non_critical_error(struct bulb_server* server, 
                                              enum server_error_state error, 
                                              void* data)
{
    // If no exception handler is set, just continue as normal.
    if (server->exception_handler == NULL)
        return true;

    // Invoke the exception handler. If it returns false, the server should shut down.
    if (!server->exception_handler(server, error, false, data))
    {
        server->error_state = error;
        return false;
    }
    return true;
}

// Start handling a critical error.
static void _server_handle_critical_error(struct bulb_server* server, 
                                          enum server_error_state error, 
                                          void* data)
{
    // If no exception handler is set, ignore.
    if (server->exception_handler == NULL)
        return;

    server->exception_handler(server, error, true, data);
    server->error_state = error;
}

// A thread is created for this function whenever a new client connection 
// is started for a given client instance.
static int _server_client_thread(void* c)
{
    struct client_node* client = (struct client_node*)c;
    struct server_node* server = client->server_node;

    for (;;)
    {
#if defined WIN32
        Sleep(5000);
#elif defined __UNIX__
        sleep(5);
#endif

        stdout_obj_write(client->sock, "u were kicked soz lols!!!\n");
        server_disconnect_client(server, client);
        return 0;
    }
}

// A thread is created for this function whenever a server instance starts listening
// for incoming client connections to accept.^
static int _server_listen_thread(void* s)
{
    struct bulb_server* server = (struct bulb_server*)s;

    for (;;)
    {
        struct client_node* node = quick_malloc(sizeof(struct client_node));
        node->server_node = &server->server_node;
        node->sock = accept(server->listen_sock, NULL, NULL);
        ASSERT(node->sock != INVALID_SOCKET,
        {
            free(node);
            if (!_server_handle_non_critical_error(server, SERVER_CLIENT_ACCEPT_FAIL, NULL))
                return 0;
        }, "Failed to accept new client connection: %d\n", socket_errno());

        server_connect_client(&server->server_node, node);
        thrd_create(&node->thread, _server_client_thread, (void*)node);
    }

    return 0;
}

// Create a new server instance. error_state can be NULL. Returns NULL on error.
struct bulb_server* server_init(const char* port, enum server_error_state* error_state)
{
    struct bulb_server* server = quick_malloc(sizeof(struct bulb_server));
    server->listen_sock = INVALID_SOCKET;

    // If Winsock is being used, Winsock must be initialized beforehand.
    int result = 0;
#ifdef WIN32
    result = WSAStartup(MAKEWORD(2, 2), &server->_wsa_data);
    ASSERT(result == 0, 
    {
        server->error_state = SERVER_WINSOCK_FAIL;
        goto fail; 
    }, "Winsock 2.2 failed to start:%d\n", result);
#endif

    // Resolve the hostname to listen to.
    struct addrinfo* addr_ptr = NULL;
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;    // Use reliable, segmented communication.
    hints.ai_flags = AI_PASSIVE;        // Required for binding.
    result = getaddrinfo(NULL, port, &hints, &addr_ptr);
    ASSERT(result == 0, 
    {
        server->error_state = SERVER_ADDRESS_FAIL;
        goto fail; 
    }, "getaddrinfo() failed: %d\n", result);

    // Create and bind the socket for the server to listen to client connections.
    server->listen_sock = socket(addr_ptr->ai_family, addr_ptr->ai_socktype,
        addr_ptr->ai_protocol);
    ASSERT(server->listen_sock != INVALID_SOCKET, 
    { 
        server->error_state = SERVER_LISTEN_SOCKET_FAIL;
        goto fail; 
    }, "Failed to create socket: %d\n", socket_errno());
    result = bind(server->listen_sock, addr_ptr->ai_addr, (int)addr_ptr->ai_addrlen);
    ASSERT(result != SOCKET_ERROR, 
    {
        server->error_state = SERVER_LISTEN_SOCKET_FAIL;
        goto fail; 
    }, "Failed to bind to socket localhost:%d: %d\n", port, socket_errno());
    freeaddrinfo(addr_ptr);

    server_node_init(&server->server_node);
    return server;

fail:
    if (error_state != NULL)
        *error_state = server->error_state;
    if (server->listen_sock != INVALID_SOCKET)
        closesocket(server->listen_sock);
    if (addr_ptr != NULL)
        freeaddrinfo(addr_ptr);
    free(server);
    return NULL;
}

// Set a custom exception handler.
void server_set_exception_handler(struct bulb_server* server, server_exception_func func)
{
    ASSERT(server, { return; });
    server->exception_handler = func;
}

// Start accepting new clients asynchronously. Returns false on error.
bool server_listen(struct bulb_server* server)
{
    ASSERT(server, { return false; });
    ASSERT(!server->is_listening, { return false; })

    int result = listen(server->listen_sock, SOMAXCONN);
    ASSERT(result != SOCKET_ERROR, 
    { 
        server->error_state = SERVER_LISTEN_SOCKET_FAIL;
        return false; 
    }, "Failed to start listening for connections: %d\n", socket_errno());
    server->is_listening = true;

    thrd_create(&server->listen_thread, _server_listen_thread, (void*)server);
    return true;
}

// Shut down and free a server instance.
void server_free(struct bulb_server* server)
{
    ASSERT(server, { return; });
#ifdef WIN32
    WSACleanup();
#endif

    server_disconnect_all_clients(&server->server_node);
    closesocket(server->listen_sock);
    free(server);
}