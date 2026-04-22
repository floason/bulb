// floason (C) 2025
// Licensed under the MIT License.

#pragma once

#include <stdbool.h>
#include <threads.h>

#include "bulb_macros.h"
#include "bulb_structs.h"

#ifdef SERVER
#   include "unisock.h"
#   include "server_node.h"
#endif

struct bulb_server;

// Unless specified, data in the exception handler function is NULL by default.
enum server_error_state
{
    // OK
    SERVER_OK,

    // Exceptions which do not disrupt the server instance.
    SERVER_CLIENT_ACCEPT_FAIL,
    SERVER_PRINT_STDOUT,        // data is const char*
    SERVER_RECEIVED_MESSAGE,    // data is bulb_message*

    // Server exit that results in the server thread being ended.
    SERVER_FINISH,

    // Fatal errors.
    SERVER_WINSOCK_FAIL,        // Windows only.
    SERVER_ADDRESS_FAIL,
    SERVER_LISTEN_SOCKET_FAIL,
};

// Return false to hint critical fault to the server.
typedef bool (*server_exception_func)(struct bulb_server* server, 
                                      enum server_error_state error, 
                                      bool fatal, 
                                      void* data);

struct bulb_server
{
    thrd_t listen_thread;
    bool is_listening;
    bool disconnect_handled;    // Should be toggled by the exception handler on 
                                // SERVER_FINISH.
    enum server_error_state error_state;

    // Errors raised outside server_init() will invoke this function. If false is returned
    // for a non-critical error, the server will terminate.
    server_exception_func exception_handler;

#ifdef SERVER
    SOCKET listen_sock;
    struct server_node* server_node;
#endif
};

// Create a new server instance. error_state can be NULL. Returns NULL on error.
BULB_API struct bulb_server* server_init(uint16_t port, enum server_error_state* error_state);

// Start handling a non-critical exception. If the exception returns false,
// it will be re-evaluated as a critical error and this function will return
// false.
BULB_API bool server_throw_exception(struct bulb_server* server, 
                                     enum server_error_state error, 
                                     void* data);

// Start handling a critical error.
BULB_API void server_throw_critical_error(struct bulb_server* server, 
                                          enum server_error_state error, 
                                          void* data);

// Set a custom exception handler.
BULB_API void server_set_exception_handler(struct bulb_server* server, server_exception_func func);

// Set the server's identifiable information.
BULB_API void server_set_userinfo(struct bulb_server* server, struct bulb_userinfo* userinfo);

// Start accepting new clients asynchronously. Returns false on error.
BULB_API bool server_listen(struct bulb_server* server);

// Process server input. Returns true if a command was detected, otherwise false.
BULB_API bool server_input(struct bulb_server* server, const char* msg, bool* cmd_success);

// Shut the server down in an orderly way. It is the caller's responsibility to
// free the server afterwards, as this function does not guarantee the prompt
// deletion of each previously-connected client.
BULB_API void server_shutdown(struct bulb_server* server, int timeout);

// Free a server instance.
BULB_API void server_free(struct bulb_server* server);