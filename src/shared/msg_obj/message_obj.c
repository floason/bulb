// floason (C) 2025
// Licensed under the MIT License.

// This object is used for sending a message to other clients, through the server.

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "unisock.h"
#include "bulb_structs.h"
#include "server_node.h"
#include "client_node.h"
#include "userinfo_obj.h"
#include "message_obj.h"

#ifdef CLIENT
#   include "client.h"
#else
#   include "server.h"
#endif

// Read a message_obj object. Returns NULL on failure.
struct bulb_obj* message_obj_read(struct mt_socket* sock, struct bulb_obj* header, size_t size)
{
    return bulb_obj_template_recv(sock, header, size);
}

// Write a message_obj object. Returns false on failure.
bool message_obj_write(struct mt_socket* sock, const char* name, const char* msg, bool from_server)
{
    struct message_obj obj;
    obj.base.type = BULB_MESSAGE;
    obj.base.size = sizeof(struct message_obj);
    obj.from_server = from_server;
    strncpy(obj.name, name, sizeof(obj.name));
    strncpy(obj.message, msg, sizeof(obj.message));
    return bulb_obj_write(sock, (struct bulb_obj*)&obj);
}

// Process a message_obj object.
void message_obj_process(struct message_obj* obj, struct server_node* server, struct client_node* client)
{
#ifdef SERVER
    // Verify the client's message before processing it.
    if (!str_isprint(obj->message))
    {
        server_kick(server, client, "Message communication must utilise displayable characters!\n");
        goto finish;
    }

    struct bulb_message msg_exception_obj;
    msg_exception_obj.name = obj->name;
    msg_exception_obj.message = obj->message;
    server_throw_exception(server->bulb_server, SERVER_RECEIVED_MESSAGE, (void*)&msg_exception_obj);

    // On the server, after printing the sender's username and their message, the message
    // object should be forwarded to all other clients. Even though the object will contain 
    // a copy of the sending client's name, the name stored in the client parameter's
    // userinfo object instead should be used in case a fraudulent username is passed
    // in the message object by the client.
    LOOP_CLIENTS(server, client, node, message_obj_write(&node->mt_sock, client->userinfo->info.name, 
        obj->message, false));
#else
    struct bulb_message msg_exception_obj;
    msg_exception_obj.name = obj->name;
    msg_exception_obj.message = obj->message;
    client_throw_exception(client->bulb_client, CLIENT_RECEIVED_MESSAGE, (void*)&msg_exception_obj);
#endif

finish:
    tagged_free(obj, TAG_BULB_OBJ);
}