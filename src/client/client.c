// floason (C) 2025
// Licensed under the MIT License.

#include <string.h>
#include <stdbool.h>
#include <ctype.h>

#include "unisock.h"
#include "bulb_version.h"
#include "client.h"
#include "cmds.h"
#include "obj_reader.h"
#include "obj_process.h"
#include "userinfo_obj.h"
#include "message_obj.h"

// A thread is created for this function once a client instance connects
// to a server successfully.
static int _client_thread(void* c)
{
    struct client_node* client = (struct client_node*)c;
    struct server_node* server = client->server_node;

    for (;;)
    {
        char error_msg[MAX_ERROR_LENGTH + 1] = { 0 };
        struct bulb_obj* obj = bulb_obj_read(&client->mt_sock, error_msg, sizeof(error_msg));
        if (obj == NULL)
        {
            if (error_msg[0] != '\0')
                fprintf(stderr, "Client failed to read message object from server: %s\n", error_msg);
            if (!client->bulb_client->disconnect_handled)
                client_throw_critical_error(client->bulb_client, CLIENT_FORCE_DISCONNECT, NULL);
            return 0;
        }

        ASSERT(bulb_process_object(obj, server, client), return 0,
            "Failed to process Bulb object of type %d\n", obj->type);

        // Periodically deallocate clients marked for deletion.
        server_free_flagged_clients(server);
    }

    return 0;
}

// Create a new client instance. Returns NULL on error.
struct bulb_client* client_init(const char* host, const char* port, enum client_error_state* error_state)
{
    struct bulb_client* client = tagged_malloc(sizeof(struct bulb_client), TAG_BULB_CLIENT);

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
    client->local_node = tagged_malloc(sizeof(struct client_node), TAG_CLIENT_NODE);
    client->local_node->bulb_client = client;
    SOCKET sock = socket(client->addr_ptr->ai_family, client->addr_ptr->ai_socktype,
        client->addr_ptr->ai_protocol);
    ASSERT(sock != INVALID_SOCKET, 
    { 
        client->error_state = CLIENT_SOCKET_FAIL;
        goto fail; 
    }, "Failed to create socket: %d\n", socket_errno());
    
    setup_mt_socket(&client->local_node->mt_sock, sock);
    server_node_init(&client->server_node);
    client->local_node->server_node = &client->server_node;

    bulb_register_shared_cmds();
    return client;

fail:
    if (error_state != NULL)
        *error_state = client->error_state;
    if (client->local_node != NULL)
        tagged_free(client->local_node, TAG_CLIENT_NODE);
    if (client->addr_ptr != NULL)
        freeaddrinfo(client->addr_ptr);
    tagged_free(client, TAG_BULB_CLIENT);
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

    int result = connect(client->local_node->mt_sock.socket, client->addr_ptr->ai_addr, 
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
    userinfo.major = MAJOR;
    userinfo.minor = MINOR;
    userinfo.patch = PATCH;
    ASSERT(userinfo_obj_write(&client->local_node->mt_sock, &userinfo), 
    { 
        client->error_state = CLIENT_AUTH_FAIL;
        return false; 
    }, "The server connection has closed unexpectedly while authenticating.\n");

    client->local_node->userinfo = tagged_malloc(sizeof(struct userinfo_obj), TAG_BULB_OBJ);
    memcpy(client->local_node->userinfo, &userinfo, sizeof(struct userinfo_obj));

    while (!client->local_node->validated);
    return true;
}

// Is the client ready for communication?
bool client_ready(struct bulb_client* client)
{
    ASSERT(client, return false);
    return client->local_node->validated;
}

// Process client input. Returns true if a command was detected, otherwise false.
bool client_input(struct bulb_client* client, const char* msg, bool* cmd_success)
{
    // Check if the first non-whitespace character of the message is a slash.
    for (int i = 0, len = strlen(msg); i < len; i++)
    {
        if (isspace(msg[i]))
            continue;
        if (msg[i] != '/')
            break;
        
        
        bool result = bulb_parse_cmd_input(&client->server_node, client->local_node, msg + i + 1);
        if (cmd_success != NULL)
            *cmd_success = result;
        return true;
    }
    message_obj_write(&client->local_node->mt_sock, client->local_node->userinfo->name, msg);
    return false;
}

// Free a client instance.
void client_free(struct bulb_client* client)
{
    ASSERT(client, return);
#if WIN32
    WSACleanup();
#endif

    client->is_connected = false;

    // If the bulb_client instance is being freed from memory, the entire client
    // connection is being terminated. Thus, every single client node should
    // also be cleaned up in the process. This also cleans up the current client's
    // client_node object respectively.
    server_disconnect_all_clients(&client->server_node);

    if (client->addr_ptr != NULL)
        freeaddrinfo(client->addr_ptr);
    tagged_free(client, TAG_BULB_CLIENT);

    bulb_cmds_cleanup();
}