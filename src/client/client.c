// floason (C) 2025
// Licensed under the MIT License.

#include <string.h>

#include "unisock.h"
#include "client.h"
#include "obj_reader.h"
#include "obj_process.h"
#include "userinfo_obj.h"
#include "message_obj.h"

// Start handling a non-critical error.
static bool _client_handle_non_critical_error(struct bulb_client* client, 
                                              enum client_error_state error, 
                                              void* data)
{
    // If no exception handler is set, just continue as normal.
    if (client->exception_handler == NULL)
        return true;

    // Invoke the exception handler. If it returns false, the server should shut down.
    if (!client->exception_handler(client, error, false, data))
    {
        if (client != NULL)
            client->error_state = error;
        return false;
    }
    return true;
}

// Start handling a critical error.
static void _client_handle_critical_error(struct bulb_client* client, 
                                          enum client_error_state error, 
                                          void* data)
{
    // If no exception handler is set, ignore.
    if (client->exception_handler == NULL)
        return;

    client->exception_handler(client, error, true, data);
    if (client != NULL)
        client->error_state = error;
}

// A thread is created for this function once a client instance connects
// to a server successfully.
static int _client_thread(void* c)
{
    struct client_node* client = (struct client_node*)c;
    struct server_node* server = client->server_node;

    for (;;)
    {
        bool socket_closed;
        struct bulb_obj* obj = bulb_obj_read(client->sock, &socket_closed);
        if (obj == NULL)
        {
            enum client_error_state state = socket_closed ? CLIENT_DISCONNECT : CLIENT_FORCE_DISCONNECT;
            if (state == CLIENT_FORCE_DISCONNECT)
                puts("The server connection has closed unexpectedly.");
            _client_handle_critical_error(client->bulb_client, state, NULL);
            return 0;
        }

        ASSERT(bulb_process_object(obj, server, client), return 0,
            "Failed to process Bulb object of type %d\n", obj->type);
    }

    return 0;
}

// Create a new client instance. Returns NULL on error.
struct bulb_client* client_init(const char* host, const char* port, enum client_error_state* error_state)
{
    struct bulb_client* client = quick_malloc(sizeof(struct bulb_client));

    // If Winsock is being used, Winsock must be initialized beforehand.
    int result = 0;
#ifdef WIN32
    result = WSAStartup(MAKEWORD(2, 2), &client->_wsa_data);
    ASSERT(result == 0, 
    { 
        client->error_state = CLIENT_WINSOCK_FAIL;
        goto fail; 
    }, "Winsock 2.2 failed to start:%d\n", result);
#endif

    // Resolve the hostname to connect to.
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;          // Use IPv4.
    hints.ai_socktype = SOCK_STREAM;    // Use reliable, segmented communication.
    result = getaddrinfo(host, port, &hints, &client->addr_ptr);
    ASSERT(result == 0, 
    { 
        client->error_state = CLIENT_ADDRESS_FAIL;
        goto fail; 
    }, "getaddrinfo() failed: %d\n", result);

    // Set up the local client node and instantiate its socket for server communication.
    client->local_node = quick_malloc(sizeof(struct client_node));
    client->local_node->bulb_client = client;
    client->local_node->sock = socket(client->addr_ptr->ai_family, client->addr_ptr->ai_socktype,
        client->addr_ptr->ai_protocol);
    ASSERT(client->local_node->sock != INVALID_SOCKET, 
    { 
        client->error_state = CLIENT_SOCKET_FAIL;
        goto fail; 
    }, "Failed to create socket: %d\n", socket_errno());
    
    server_node_init(&client->server_node);
    client->local_node->server_node = &client->server_node;

    return client;

fail:
    if (error_state != NULL)
        *error_state = client->error_state;
    if (client->local_node != NULL)
        free(client->local_node);
    if (client->addr_ptr != NULL)
        freeaddrinfo(client->addr_ptr);
    free(client);
    return NULL;
}

// Set a custom exception handler.
void client_set_exception_handler(struct bulb_client* client, client_exception_func func)
{
    client->exception_handler = func;
}

// Connect the client. Returns false on error.
bool client_connect(struct bulb_client* client)
{
    ASSERT(client, return false);
    ASSERT(!client->is_connected, return false);

    int result = connect(client->local_node->sock, client->addr_ptr->ai_addr, 
        (int)client->addr_ptr->ai_addrlen);
    ASSERT(result != SOCKET_ERROR, 
    { 
        client->error_state = CLIENT_SOCKET_FAIL;
        return false; 
    }, "Failed to connect the client: %d\n", socket_errno());
    freeaddrinfo(client->addr_ptr);
    client->addr_ptr = NULL;
    client->is_connected = true;

    server_connect_client(&client->server_node, client->local_node);
    thrd_create(&client->local_node->thread, _client_thread, client->local_node);
    return true;
}

// Authenticate the user's connection. This must be called after the client successfully
// connects to a server. Returns false on error.
bool client_authenticate(struct bulb_client* client, struct userinfo_obj userinfo)
{
    ASSERT(client, return false);
    ASSERT(client->is_connected, return false);
    
    ASSERT(strlen(userinfo.name) > 0, 
    { 
        client->error_state = CLIENT_AUTH_FAIL;
        return false; 
    }, "Your username cannot be empty!\n");

    userinfo.base.type = BULB_USERINFO;
    userinfo.base.size = sizeof(userinfo);
    ASSERT(userinfo_obj_write(client->local_node->sock, &userinfo), 
    { 
        client->error_state = CLIENT_AUTH_FAIL;
        return false; 
    }, "The server connection has closed unexpectedly.\n");

    client->local_node->userinfo = quick_malloc(sizeof(struct userinfo_obj));
    memcpy(client->local_node->userinfo, &userinfo, sizeof(struct userinfo_obj));

    return true;
}

// Is the client ready for communication?
bool client_ready(struct bulb_client* client)
{
    ASSERT(client, return false);
    return client->local_node->validated;
}

// Process client input. TODO: commands
void client_input(struct bulb_client* client, const char* msg)
{
    message_obj_write(client->local_node->sock, msg);
}

// Free a client instance.
void client_free(struct bulb_client* client)
{
    ASSERT(client, return);
#if WIN32
    WSACleanup();
#endif

    // If the bulb_client instance is being freed from memory, the entire client
    // connection is being terminated. Thus, every single client node should
    // also be cleaned up in the process. This also cleans up the current client's
    // client_node object respectively.
    server_disconnect_all_clients(&client->server_node);

    if (client->addr_ptr != NULL)
        freeaddrinfo(client->addr_ptr);
    free(client);
}