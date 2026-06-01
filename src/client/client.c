// floason (C) 2025
// Licensed under the MIT License.

#include <string.h>
#include <stdbool.h>
#include <ctype.h>

#include "unisock.h"
#include "networking.h"
#include "bulb_version.h"
#include "bulb_client.h"
#include "cmds.h"
#include "userinfo_obj.h"
#include "message_obj.h"

#ifdef WIN32
    static WSADATA wsa_data;
#endif

// Create a new client instance. Returns NULL on error.
struct bulb_client* client_init(const char* host, 
                                const char* port, 
                                enum client_error_state* error_state)
{
    struct bulb_client* client = quick_malloc(sizeof(struct bulb_client));

    // If Winsock is being used, Winsock must be initialized beforehand.
    int result = 0;
#ifdef WIN32
    result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
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
    if (result != 0)
    { 
        client->error_state = CLIENT_ADDRESS_FAIL;
        goto fail; 
    }

    // Set up the local client node and instantiate its socket for server communication.
    client->local_node = localclient = quick_malloc(sizeof(struct client_node));
    client->local_node->bulb_client = client;
    client_shared_node_init(client->local_node);
    SOCKET sock = socket(client->addr_ptr->ai_family, client->addr_ptr->ai_socktype,
        client->addr_ptr->ai_protocol);
    if (sock == INVALID_SOCKET)
    { 
        client->error_state = CLIENT_SOCKET_FAIL;
        goto fail; 
    }
    
    client->local_node->mt_sock = mt_socket_new(sock);
    client->local_node->mt_sock->dealloc_func = client_set_ready_to_delete_from_sock;
    client->server_node = client->local_node->server_node = server_shared_node_alloc();

    bulb_cmds_init();
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

// Start handling a non-critical exception. If the exception returns false,
// it will be re-evaluated as a critical error and this function will return
// false.
bool client_throw_exception(struct bulb_client* client, 
                            enum client_error_state error, 
                            void* data)
{
    ASSERT(client, return false);
    ASSERT(client->exception_handler != NULL, return false, "Client exception handler is not set.\n");

    // Invoke the exception handler. If it returns false, the server should shut down.
    bool result = client->exception_handler(client, error, false, data);
    client->error_state = error;
    if (!result)
        return false;
    return true;
}

// Start handling a critical error.
void client_throw_critical_error(struct bulb_client* client, 
                                 enum client_error_state error, 
                                 void* data)
{
    ASSERT(client, return);
    ASSERT(client->exception_handler != NULL, return, "Client exception handler is not set.\n");

    // Invoke the exception handler.
    client->exception_handler(client, error, true, data);
    client->error_state = error;
}

// Connect the client. Returns false on error.
bool client_connect(struct bulb_client* client)
{
    ASSERT(client, return false);
    ASSERT(!client->is_connected, return false);

    int result = connect(client->local_node->mt_sock->socket, client->addr_ptr->ai_addr, 
        (int)client->addr_ptr->ai_addrlen);
    if (result == SOCKET_ERROR)
    { 
        client->error_state = CLIENT_SOCKET_FAIL;
        return false; 
    }

    server_listen_client(client->server_node, client->local_node);
    freeaddrinfo(client->addr_ptr);
    client->addr_ptr = NULL;
    client->is_connected = true;
    return true;
}

// Authenticate the user's connection. This must be called after the client successfully
// connects to a server. On successful validation, the exception CLIENT_CONNECTED will
// be thrown. On unsuccessful validation, the client will be disconnected. Returns false 
// on error.
bool client_authenticate(struct bulb_client* client, struct bulb_userinfo* userinfo)
{
    ASSERT(client, return false);
    ASSERT(client->is_connected, return false);
    
    if (strlen(userinfo->name) == 0)
    { 
        client->error_state = CLIENT_AUTH_FAIL;
        return false; 
    }

    struct userinfo_obj obj;
    memcpy(&obj.info, userinfo, sizeof(obj.info));
    obj.base.type = BULB_USERINFO;
    obj.base.size = sizeof(obj);
    obj.info.major = MAJOR;
    obj.info.minor = MINOR;
    obj.info.patch = PATCH;
    if (!userinfo_obj_write(client->local_node->mt_sock, &obj))
    { 
        client->error_state = CLIENT_AUTH_FAIL;
        return false; 
    }

    client->local_node->userinfo = quick_malloc(sizeof(struct userinfo_obj));
    memcpy(client->local_node->userinfo, &obj, sizeof(struct userinfo_obj));

    return true;
}

// Is the client ready for communication?
bool client_ready(struct bulb_client* client)
{
    ASSERT(client, return false);
    return client->local_node->status == CLIENT_VALIDATED;
}

// Get the number of connected clients on the server. Returns -1 on failure.
BULB_API int client_num_connected(struct bulb_client* client)
{
    ASSERT(client, return -1);
    ASSERT(client->local_node, return -1);
    ASSERT(client->local_node->server_node, return -1);
    return client->local_node->server_node->number_connected;
}

// Get a linked list of each connected client's userinfo object. Returns NULL on
// failure, or if no clients are connected.
BULB_API struct bulb_userinfo* client_get_userinfo_list(struct bulb_client* client)
{
    ASSERT(client, return NULL);
    ASSERT(client->local_node, return NULL);
    ASSERT(client->local_node->server_node, return NULL);
    ASSERT(client->local_node->server_node->clients_info_head, return NULL);
    return client->local_node->server_node->clients_info_head->next;
}

// Is the client disconnecting? The connection may not have been completely
// severed at this point.
bool client_disconnecting(struct bulb_client* client)
{
    ASSERT(client, return false);
    return client_flagged_for_deletion(client->local_node);
}

// Process client input. Returns true if a command was detected, otherwise false.
bool client_input(struct bulb_client* client, const char* msg, bool* cmd_success)
{
    ASSERT(client, return false);

    // Check if the first non-whitespace character of the message is a slash.
    for (int i = 0, len = strlen(msg); i < len; i++)
    {
        if (isspace(msg[i]))
            continue;
        if (msg[i] != '/')
            break;
        
        bool result = bulb_parse_cmd_input(client->server_node, msg + i + 1);
        if (cmd_success != NULL)
            *cmd_success = result;
        return true;
    }
    
    message_obj_write(client->local_node->mt_sock, client->local_node->userinfo->info.name, msg, false);
    return false;
}

// Free a client instance.
void client_free(struct bulb_client* client)
{
    ASSERT(client, return);

    client->disconnecting = true;
    client->is_connected = false;

    // If the bulb_client instance is being freed from memory, the entire client
    // connection is being terminated. Thus, every single client node should
    // also be cleaned up in the process. This also cleans up the current client's
    // client_node object respectively.
    server_disconnect_all_clients(client->server_node);

    if (client->addr_ptr != NULL)
        freeaddrinfo(client->addr_ptr);
    free(client);

    bulb_cmds_cleanup();

#if WIN32
    WSACleanup();
#endif
}