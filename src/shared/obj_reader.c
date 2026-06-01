// floason (C) 2025
// Licensed under the MIT License.

#include <string.h>

#include "unisock.h"
#include "networking.h"
#include "obj_reader.h"
#include "stdout_obj.h"
#include "userinfo_obj.h"
#include "connect_obj.h"
#include "disconnect_obj.h"
#include "message_obj.h"
#include "ping_obj.h"
#include "update_userinfo_obj.h"
#include "received_obj.h"

#define EVALUATE_READ_FAIL()                                                                \
    {                                                                                       \
        /* Test for whether any data was read to begin with. */                             \
        if (read == 0)                                                                      \
        {                                                                                   \
            /* Hint to the socket's assigned socket manager that this socket is no */       \
            /* longer listening. */                                                         \
            mt_socket_shutdown(sock);                                                       \
            return NULL;                                                                    \
        }                                                                                   \
                                                                                            \
        /* If the socket is blocking, exit.*/                                               \
        if (socket_errno() == SOCKET_AGAIN)                                                 \
        {                                                                                   \
            *try_again = true;                                                              \
            return NULL;                                                                    \
        }                                                                                   \
                                                                                            \
        /* As the header object is attempted to be read repeatedly, this should only */     \
        /* exit on genuine read failure. */                                                 \
        if (read == -1)                                                                     \
            return NULL;                                                                    \
    }

// Read a Bulb object from a socket. The object is dynamically allocated and thus
// must be released from memory afterwards. This is a non-blocking function.
struct bulb_obj* bulb_obj_read(struct mt_socket* sock, char* error_msg, size_t len, bool* try_again)
{
    // The purpose of this function is to read a single object that's currently
    // buffered. Streamed data buffered by multiple send() calls may be read in
    // only a single recv() call, so the very first read this function does will
    // only peek into the socket stream. The final number of bytes that will be
    // read will only correspond to the data size of the final object.
    struct client_node* client = (struct client_node*)sock->parent_client;
    memset(error_msg, 0, len);
    *try_again = false;

    // Peek into the socket stream to read the object header, if not already obtained. 
    // In the case that  significant data fragmentation occurs (although this should 
    // be very unlikely), due to incomplete data transmission, this function will 
    // terminate pre-maturely.
    char buffer[RECV_BUFFER_SIZE];
    int read;
    if (client->next_obj_header == NULL)
    {
        while ((read = mt_socket_recv(sock, buffer, sizeof(struct bulb_obj), MSG_PEEK)) 
            < (int)sizeof(struct bulb_obj))
            EVALUATE_READ_FAIL();
        client->next_obj_header = (struct bulb_obj*)quick_malloc(sizeof(struct bulb_obj));
        memcpy(client->next_obj_header, buffer, sizeof(struct bulb_obj));
    }

    // Attempt to read the entire requested byte stream. If the object has not been
    // fully transmitted, the function will also terminate early.
    do
    {
        if ((read = mt_socket_recv(sock, buffer, 
                MIN(client->next_obj_header->size - client->read_offset, RECV_BUFFER_SIZE), 0)) 
            <= 0)
            EVALUATE_READ_FAIL();

        struct mt_socket_data_node* node = (struct mt_socket_data_node*)quick_malloc(
            sizeof(struct mt_socket_data_node) + read);
        memcpy(node->data, buffer, read);
        node->len = read;
        QUEUE_ENQUEUE(node, sock->data_recv_queue, sock->data_recv_tail);
            
        client->read_offset += read;
    } while (client->next_obj_header->size > client->read_offset);
    
    // A switch table is used to select the exact read function to use for reading the
    // given object from the given socket stream. The size of each object is passed
    // as a parameter to each non-default object, in order to validate against
    // objects that could crash the server from invalid clients.
    struct bulb_obj* return_obj = NULL;
    switch (client->next_obj_header->type)
    {
        case BULB_OBJ:
            // This object should not be received whatsoever.
#ifdef CLIENT
            ASSERT(false, return NULL, "Test object was found in stream")
#else
            snprintf(error_msg, len, "Client attempted to send test object");
#endif
            return NULL;
        case BULB_STDOUT:
#ifdef SERVER
            // Because this object is of variable length and incorporates zero character 
            // filtering, this object must NOT be sent by client code as it is inherently 
            // dangerous.
            snprintf(error_msg, len, "Client attempted to send stdout_obj");
            return NULL;
#endif

            // It is difficult to perform size validations due to the variadic size of 
            // this object.
            return_obj = stdout_obj_read(sock, client->next_obj_header, client->next_obj_header->size);
            break;
        case BULB_USERINFO:
            return_obj = userinfo_obj_read(sock, client->next_obj_header, sizeof(struct userinfo_obj));
            break;
        case BULB_CONNECT:
            return_obj = connect_obj_read(sock, client->next_obj_header, sizeof(struct connect_obj));
            break;
        case BULB_DISCONNECT:
            return_obj = disconnect_obj_read(sock, client->next_obj_header, sizeof(struct disconnect_obj));
            break;
        case BULB_MESSAGE:
            return_obj = message_obj_read(sock, client->next_obj_header, sizeof(struct message_obj));
            break;
        case BULB_PING:
            return_obj = ping_obj_read(sock, client->next_obj_header, sizeof(struct ping_obj));
            break;
        case BULB_UPDATE_USERINFO:
            return_obj = update_userinfo_obj_read(sock, client->next_obj_header, 
                sizeof(struct update_userinfo_obj));
            break;
        case BULB_RECEIVED:
            return_obj = received_obj_read(sock, client->next_obj_header, sizeof(struct received_obj));
            break;
        default:
#ifdef CLIENT
            ASSERT(false, return NULL, "Invalid obj type %d\n", client->next_obj_header->type);
#else
            snprintf(error_msg, len, "Client attempted to send invalid obj type %d", 
                client->next_obj_header->type);
#endif
            return NULL;
    }

    client->next_obj_header = NULL;
    client->read_offset = 0;
    return return_obj;
}