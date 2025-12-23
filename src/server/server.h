// floason (C) 2025
// Licensed under the MIT License.

#include <stdbool.h>
#include <threads.h>

#include "unisock.h"
#include "server_node.h"

struct bulb_server;

// Unless specified, data in the exception handler function is NULL by default.
enum server_error_state
{
    // OK
    SERVER_OK,

    // Errors that do not disrupt the server instance.
    SERVER_CLIENT_ACCEPT_FAIL,

    // Server exit that results in the server thread being ended.
    SERVER_FINISH,

    // Fatal errors.
    SERVER_WINSOCK_FAIL,        // Windows only.
    SERVER_ADDRESS_FAIL,
    SERVER_LISTEN_SOCKET_FAIL,
};

typedef bool (*server_exception_func)(struct bulb_server* server, 
                                      enum server_error_state error, 
                                      bool fatal, 
                                      void* data);

struct bulb_server
{
#ifdef WIN32
    WSADATA _wsa_data;
#endif
    SOCKET listen_sock;
    thrd_t listen_thread;
    bool is_listening;
    enum server_error_state error_state;

    struct server_node server_node;

    // Errors raised outside server_init() will invoke this function. If false is returned
    // for a non-critical error, the server will terminate.
    server_exception_func exception_handler;
};

// Create a new server instance. error_state can be NULL. Returns NULL on error.
struct bulb_server* server_init(const char* port, enum server_error_state* error_state);

// Set a custom exception handler.
void server_set_exception_handler(struct bulb_server* server, server_exception_func func);

// Start accepting new clients asynchronously. Returns false on error.
bool server_listen(struct bulb_server* server);

// Free a server instance.
void server_free(struct bulb_server* server);