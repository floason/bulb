// floason (C) 2025
// Licensed under the MIT License.

#pragma once

#include <stdbool.h>

#include "unisock.h"
#include "server_node.h"
#include "userinfo_obj.h"

struct bulb_client;

// Unless specified, data in the exception handler function is NULL by default.
enum client_error_state
{
    // OK
    CLIENT_OK,

    // Errors that do not disrupt the client instance.

    // Client disconnect that results in the client thread being ended.
    CLIENT_DISCONNECT, 
    CLIENT_FORCE_DISCONNECT,

    // Fatal errors.
    CLIENT_WINSOCK_FAIL,        // Windows only.
    CLIENT_ADDRESS_FAIL,
    CLIENT_SOCKET_FAIL,
    CLIENT_AUTH_FAIL
};

typedef bool (*client_exception_func)(struct bulb_client* client, 
                                      enum client_error_state error, 
                                      bool fatal, 
                                      void* data);

struct bulb_client
{
#ifdef WIN32
    WSADATA _wsa_data;
#endif
    struct addrinfo* addr_ptr;
    bool is_connected;
    enum client_error_state error_state;

    struct client_node* local_node;
    struct server_node server_node;

    // Errors raised outside client_init() will invoke this function. If false is returned
    // for a non-critical error, the client will terminate.
    client_exception_func exception_handler;
};

// Create a new client instance. Returns NULL on error.
struct bulb_client* client_init(const char* host, const char* port, enum client_error_state* error_state);

// Set a custom exception handler.
void client_set_exception_handler(struct bulb_client* client, client_exception_func func);

// Connect the client. Returns false on error.
bool client_connect(struct bulb_client* client);

// Authenticate the user's connection. This must be called after the client successfully
// connects to a server. Returns false on error.
bool client_authenticate(struct bulb_client* client, struct userinfo_obj userinfo);

// Free a client instance.
void client_free(struct bulb_client* client);