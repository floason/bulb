// floason (C) 2025
// Licensed under the MIT License.

#pragma once

#include <stdbool.h>

#include "bulb_macros.h"
#include "bulb_structs.h"

#ifdef CLIENT
#   include "client_node.h"
#   include "server_node.h"
#endif

struct bulb_client;

// Unless specified, data in the exception handler function is NULL by default.
enum client_error_state
{
    // OK
    CLIENT_OK,

    // Exceptions which do not disrupt the client instance.
    CLIENT_PRINT_STDOUT,        // data is const char*
    CLIENT_RECEIVED_MESSAGE,    // data is bulb_message*

    // Client disconnect that results in the client thread being ended.
    CLIENT_DISCONNECT, 
    CLIENT_FORCE_DISCONNECT,

    // Fatal errors.
    CLIENT_WINSOCK_FAIL,        // Windows only.
    CLIENT_ADDRESS_FAIL,
    CLIENT_SOCKET_FAIL,
    CLIENT_AUTH_FAIL
};

// Return false to hint critical fault to the client.
typedef bool (*client_exception_func)(struct bulb_client* client, 
                                      enum client_error_state error, 
                                      bool fatal, 
                                      void* data);

struct bulb_client
{
    struct addrinfo* addr_ptr;
    bool is_connected;
    bool disconnect_handled;    // Should be toggled by the exception handler on either
                                // disconnect events.
    enum client_error_state error_state;

    // Errors raised outside client_init() will invoke this function. If false is returned
    // for a non-critical error, the client will terminate.
    client_exception_func exception_handler;

#ifdef CLIENT
    struct client_node* local_node;
    struct server_node* server_node;
#endif
};

// Create a new client instance. Returns NULL on error.
BULB_API struct bulb_client* client_init(const char* host, 
                                         const char* port, 
                                         enum client_error_state* error_state);

// Set a custom exception handler.
BULB_API void client_set_exception_handler(struct bulb_client* client, client_exception_func func);

// Start handling a non-critical exception. If the exception returns false,
// it will be re-evaluated as a critical error and this function will return
// false.
BULB_API bool client_throw_exception(struct bulb_client* client, 
                                     enum client_error_state error, 
                                     void* data);

// Start handling a critical error.
BULB_API void client_throw_critical_error(struct bulb_client* client, 
                                          enum client_error_state error, 
                                          void* data);

// Connect the client. Returns false on error.
BULB_API bool client_connect(struct bulb_client* client);

// Authenticate the user's connection. This must be called after the client successfully
// connects to a server. Returns false on error.
BULB_API bool client_authenticate(struct bulb_client* client, struct bulb_userinfo userinfo);

// Is the client ready for communication?
BULB_API bool client_ready(struct bulb_client* client);

// Process client input. Returns true if a command was detected, otherwise false.
BULB_API bool client_input(struct bulb_client* client, const char* msg, bool* cmd_success);

// Free a client instance.
BULB_API void client_free(struct bulb_client* client);