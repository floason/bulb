// floason (C) 2026
// Licensed under the MIT License.

// While unisock.h is designed as a basic universal sockets header unit,
// this translation unit is specifically designed to supplement Bulb's
// asynchronous networking architecture.

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <threads.h>
#include <time.h>

#include "unisock.h"

#ifdef __UNIX__
#   include "poll.h"
#endif

#define SOCKETS_PER_POLLING_THREAD  63                           

#define MT_SOCKET_FLAG_READY(SOCKET, ATTRIB)                                        \
    {                                                                               \
        if (!SOCKET->GLUE(ATTRIB, _queue.linked))                                   \
        {                                                                           \
            QUEUE_ENQUEUE(SOCKET, (*SOCKET->GLUE(ATTRIB, _queue.queue)),            \
                (*SOCKET->GLUE(ATTRIB, _queue.tail)), GLUE(ATTRIB, _queue));        \
            cnd_broadcast(SOCKET->ready_signal);                                    \
        }                                                                           \
    }

#define MT_SOCKET_DEQUEUE(SOCKET, NODE, ATTRIB)                                     \
    {                                                                               \
        QUEUE_DEQUEUE(NODE, (*SOCKET->GLUE(ATTRIB, _queue.queue)),                  \
            (*SOCKET->GLUE(ATTRIB, _queue.tail)), GLUE(ATTRIB, _queue));            \
    }

#define LOOP_SOCKET_MANAGERS(LIST, EXCEPT, ID, SCOPE)                               \
    {                                                                               \
        struct socket_manager* ID = LIST;                                           \
        while (ID != NULL)                                                          \
        {                                                                           \
            if (ID != EXCEPT)                                                       \
                SCOPE;                                                              \
            ID = ID->next;                                                          \
        }                                                                           \
    }

struct socket_manager;

// Stores information about recv()/send() data.
struct mt_socket_data_node
{
    struct mt_socket_data_node* next;
    struct mt_socket_data_node* prev;
    bool linked;
    int send_offset;
    size_t len;

    char data[];
};

// Stores external timeout data about data node being transmitted which does not
// apply to all data nodes.
struct mt_socket_timeout_node
{
    struct timespec send_timestamp;
    struct mt_socket_timeout_node* next;
    struct mt_socket_timeout_node* prev;
    bool linked;
};

struct mt_socket
{
    void* parent_client;
    struct socket_manager* parent_sm;

    SOCKET socket;
    mtx_t* update_lock;
    cnd_t* ready_signal;
    bool listening;

    struct timespec ping_start;
    struct timespec ping_end;

    // Used for linking mt_socket instances together.
    struct
    {
        // The queue managing this mt_socket instance is defined elsewhere, i.e. the
        // socket manager instance that manages the designated mt_socket instances.
        struct mt_socket** queue;
        struct mt_socket** tail;

        // Links this mt_socket instance to its adjacent instances in its given queue.
        struct mt_socket* prev;
        struct mt_socket* next;
        bool linked;
    } recv_queue, send_queue;

    // Used for storing pending read/write data.
    struct mt_socket_data_node* data_recv_queue;
    struct mt_socket_data_node* data_recv_tail;
    struct mt_socket_data_node* data_send_queue;
    struct mt_socket_data_node* data_send_tail;

    // Used for storing timeout information about pending write data.
    struct mt_socket_timeout_node* data_send_timeout_queue;
    struct mt_socket_timeout_node* data_send_timeout_tail;
    unsigned timeout_node_dec_count;

    // Called when the mt_socket instance is being de-allocated.
    OBJ_FUNC_P(struct mt_socket* sock, dealloc_func);

    // Modified by the socket manager.
    int _index;

#if defined WIN32
    WSAEVENT _event;
#elif defined __UNIX__
    struct pollfd _pfd;
#endif
};

// Create a new mt_socket instance.
struct mt_socket* mt_socket_new(SOCKET s);

// Configure an mt_socket instance to be non-blocking.
void mt_socket_configure_non_blocking(struct mt_socket* sock);

// Flag an mt_socket instance to begin waiting for when send() can be used again.
// This function is effectively a no-op on non-POSIX systems.
void mt_socket_flag_pending_for_send(struct mt_socket* sock);

// Shut down an mt_socket instance's connection. This hints to the socket's
// designated socket manager instance so as to handle complete closure 
// appropriately.
void mt_socket_shutdown(struct mt_socket* sock);

// Free an mt_socket instance. This should not be called if the mt_socket instanece
// is currently being managed by a socket manager. If this is the case, please see
// mt_socket_shutdown() instead.
void mt_socket_free(struct mt_socket* sock);

// Each socket manager instance is designed to handle up to 32 client connections
// simultaneously.
struct socket_manager
{
    void* parent_server;

    struct mt_socket* sockets[SOCKETS_PER_POLLING_THREAD];
    bool listening;
    thrd_t listen_thread;
    size_t active_sockets;
    mtx_t socket_add_lock;
    
    // These fields assist the merger between two socket managers.
    struct socket_manager* merge_into;
    size_t incoming_sockets;

    // Used for linking socket manager instances together.
    struct socket_manager* prev;
    struct socket_manager* next;
    bool linked;
    
    // Function pointers which can be set to independent functions so as to alter the
    // behaviour of the socket manager depending on its current state.
    OBJ_FUNC_P(struct socket_manager*, dealloc_func);   // Invoked when the socket manager is finished.
    OBJ_FUNC_P(struct socket_manager*, removed_func);   // Invoked when a socket instance is removed.

#if defined WIN32
    WSAEVENT _connection_changed_event;
#elif defined __UNIX__
    int _connection_changed_pipe[2];
    struct pollfd _connection_changed_pfd;
#endif
};

// Create a new socket manager instance.
struct socket_manager* sm_new();

// Add an mt_socket instance to a socket manager instance. Returns false if the socket
// manager instance is full.
bool sm_add(struct socket_manager* sm, struct mt_socket* sock);

// Start listening to pending socket events. Returns false if no sockets are currently
// being managed.
bool sm_listen(struct socket_manager* sm);

// Merge a socket manager instance into another. This will automatically free the socket
// manager instance's resources from memory when the merger is complete. Returns false
// if there is not enough space to accomodate all of the old socket manager's managed
// sockets, or if the socket manager instance that is requested to be merged into is 
// already completing a merger of its own.
bool sm_merge(struct socket_manager* sm, struct socket_manager* into);

// Clean-up a socket manager instance. This should not be called if the socket manager
// instance is currently running.
void sm_free(struct socket_manager* sm);