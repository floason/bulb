// floason (C) 2025
// Licensed under the MIT License.

#include "unisock.h"
#include "trie.h"
#include "server_node.h"
#include "client_node.h"

#include "bulb_macros.h"
#include "bulb_structs.h"

#ifdef CLIENT
#   include "bulb_client.h"
#else
#   include "bulb_server.h"
#endif

// Print a message to the given node by triggering a *_PRINT_STDOUT exception.
void bulb_printf(void* bulb_node, const char* buffer, ...)
{
    va_list argv;
    char message[2048];
    va_start(argv, buffer);
    vsnprintf(message, sizeof(message), buffer, argv);
    va_end(argv);

    struct bulb_stdout obj;
    obj.message = message;
    obj.type = STDOUT_GENERIC;
    
#ifdef CLIENT
    client_throw_exception(((struct client_node*)bulb_node)->bulb_client, CLIENT_PRINT_STDOUT, (void*)&obj);
#else
    server_throw_exception(((struct server_node*)bulb_node)->bulb_server, SERVER_PRINT_STDOUT, (void*)&obj);
#endif
}