// floason (C) 2026
// Licensed under the MIT License.

// While unisock.h is designed as a basic universal sockets header unit,
// this translation unit is specifically designed to supplement Bulb's
// asynchronous networking architecture.

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <threads.h>

#include "unisock.h"
#include "networking.h"

#if defined __UNIX__
#   include "fcntl.h"
#endif

#define MAX_EVENT_COUNT SOCKETS_PER_POLLING_THREAD + 1

#if defined WIN32
#   define SOCK_EVENT                       WSAEVENT
#   define SOCK_EVENT_GET(MT_SOCK)          MT_SOCK->_event
#   define CONNECTION_CHANGED_EVENT_GET(SM) SM->_connection_changed_event
#elif defined __UNIX__
#   define SOCK_EVENT                       struct pollfd
#   define SOCK_EVENT_GET(MT_SOCK)          MT_SOCK->_pfd
#   define CONNECTION_CHANGED_EVENT_GET(SM) SM->_connection_changed_pfd
#else
#   define SOCK_EVENT                       void*
#   define SOCK_EVENT_GET(MT_SOCK)          NULL
#   define CONNECTION_CHANGED_EVENT_GET(SM) NULL
#endif

#define MTX_OP_NULLABLE(MTX, FUNC)          \
    if (MTX != NULL)                        \
        FUNC(MTX);                          \

// Extract each socket event object from each socket manager's active
// sockets.
static inline void _sm_extract_events(struct socket_manager* sm, SOCK_EVENT* events)
{
    ASSERT(sm != NULL, return);
    memset(events, 0, sizeof(SOCK_EVENT) * MAX_EVENT_COUNT);

    int i = 0;
    for (; i < sm->active_sockets; i++)
    {
        sm->sockets[i]->_index = i;
        events[i] = SOCK_EVENT_GET(sm->sockets[i]);
    }
    events[i] = CONNECTION_CHANGED_EVENT_GET(sm);
}

// Remove a disused socket from a socket manager's array of sockets.
static inline void _sm_remove_socket(struct socket_manager* sm, struct mt_socket* sock)
{
    ASSERT(sm != NULL, return);
    mtx_lock(&sm->socket_add_lock);

    // Decrement the active socket count and move any connected sockets after the
    // socket being removed if it is not at the tail of the sockets array.
    if (sock->_index + 1 < sm->active_sockets--)
    {
        memmove(&sm->sockets[sock->_index], &sm->sockets[sock->_index + 1], 
            sizeof(struct mt_socket*) * (sm->active_sockets - sock->_index));
    }
    sm->sockets[sm->active_sockets] = NULL;

    struct mt_socket* throwaway;
    if (!QUEUE_DEQUEUED((&sock->recv_queue)))
        MT_SOCKET_DEQUEUE(sock, throwaway, recv);
    if (!QUEUE_DEQUEUED((&sock->send_queue)))
        MT_SOCKET_DEQUEUE(sock, throwaway, send);

    // Call the socket manager's outsourced socket removal function.
    if (sm->removed_func != NULL)
        sm->removed_func(sm);
    
    mt_socket_free(sock);
    mtx_unlock(&sm->socket_add_lock);
}

// Signal the change of a socket connection status.
static inline void _sm_signal_socket_change(struct socket_manager* sm)
{
#if defined WIN32
    WSASetEvent(sm->_connection_changed_event);
#elif defined __UNIX__
    char buf = '\0';
    write(sm->_connection_changed_pipe[PIPE_WRITE], &buf, sizeof(buf));
#else
    ASSERT(false, return NULL, "Target platform not supported by mt_socket!");
#endif
}

// Merge a socket manager instance into another.
static inline void _sm_merge_into(struct socket_manager* sm, struct socket_manager* into)
{
    mtx_lock(&into->socket_add_lock);

    into->active_sockets += sm->active_sockets;
    into->incoming_sockets -= sm->active_sockets;

    for (int i = into->active_sockets - sm->active_sockets; i < into->active_sockets; i++)
    {
        struct mt_socket* sock = sm->sockets[i - into->active_sockets];
        MTX_OP_NULLABLE(sock->update_lock, mtx_lock);

        into->sockets[i] = sock;
        into->sockets[i]->parent_sm = into;

        MTX_OP_NULLABLE(sock->update_lock, mtx_unlock);
    }
    memcpy(&into->sockets[into->active_sockets], sm->sockets, 
        sizeof(struct mt_socket*) * sm->active_sockets);
    sm_free(sm);

    mtx_unlock(&into->socket_add_lock);
    _sm_signal_socket_change(into);
}

// Calculate the effective reserved socket count of a socket manager instance.
static inline size_t _sm_reserved_sockets(struct socket_manager* sm)
{
    return sm->active_sockets + sm->incoming_sockets;
}

// Update a socket's status if it threw an event in its correlated socket manager
// instance. Returns false if the socket manager instance has been de-allocated.
static bool _sm_update_socket(struct socket_manager* sm, struct mt_socket* selected, bool* updated)
{
#if defined WIN32
    // Query the socket event's flags and reset the socket event.
    WSANETWORKEVENTS flags = { 0 };
    WSAEnumNetworkEvents(selected->socket, selected->_event, &flags);

    bool flag_read = (flags.lNetworkEvents & FD_READ);
    bool flag_write = (flags.lNetworkEvents & FD_WRITE);
    bool flag_closed = ((flags.lNetworkEvents & FD_CLOSE) || !selected->listening);
#elif defined __UNIX__
    bool flag_read = (selected->_pfd.revents & POLLIN);
    bool flag_write = (selected->_pfd.revents & POLLOUT);
    bool flag_closed = ((selected->_pfd.revents & (POLLERR | POLLHUP) || !selected->listening));
#endif

    // If this socket closed, remove it from the socket manager's sockets
    // array.
    if (updated != NULL)
        *updated = flag_closed || flag_read || flag_closed;
    bool exit = false;
    mtx_t* client_update_lock = selected->update_lock;
    MTX_OP_NULLABLE(client_update_lock, mtx_lock);
    if (flag_closed)
    {
        if (updated != NULL)
            *updated = true;
        _sm_remove_socket(sm, selected);

        // If this socket manager instance has no sockets remaining, it
        // should be de-allocated from memory and this loop should
        // consequentially terminate.
        if (sm->active_sockets == 0)
        {
            sm_free(sm);
            exit = true;
        }
        
        goto finish;
    }

    // Signal whether any read/write events took place.
    if (flag_read)
        MT_SOCKET_FLAG_READY(selected, recv);
    if (flag_closed)
    {
        MT_SOCKET_FLAG_READY(selected, send);

#if defined __UNIX__
        // On Windows, FD_WRITE is raised for a socket only when its internal
        // buffer now has space available for sending new data. POLLOUT on POSIX
        // systems is instead level triggered if data is actually ready to send.
        // To prevent poll() from repeatedly returning, the POLLOUT flag should
        // be removed from the selected poll file descriptor.
        selected->_pfd.events &= ~POLLOUT;
#endif
    }
    
finish:
    MTX_OP_NULLABLE(client_update_lock, mtx_unlock);
    return !exit;
}

// Socket manager thread function which is responsible for listening to
// its assigned sockets for any relevant socket events.
static int _sm_listen_function(void* obj)
{
    struct socket_manager* sm = (struct socket_manager*)obj;
    SOCK_EVENT events[MAX_EVENT_COUNT];

    for (;;)
    {
        // If this socket manager instance is being requested to merge
        // into another, handle the merger and terminate the loop.
        if (sm->merge_into != NULL)
        {
            _sm_merge_into(sm, sm->merge_into);
            return 0;
        }

        // Extract all event objects to listen to.
        _sm_extract_events(sm, events);

#if defined WIN32
        // Poll for any socket event.
        DWORD result = WSAWaitForMultipleEvents(sm->active_sockets + 1, events, FALSE, -1, FALSE);
        if (result >= WSA_WAIT_EVENT_0 + sm->active_sockets)
        {
            // If the signalling event object is the connection changed event,
            // manually reset it and continue. This interrupt means that any
            // new sockets will now be polled.
            if (result - WSA_WAIT_EVENT_0 == sm->active_sockets)
                WSAResetEvent(sm->_connection_changed_event);
            continue;
        }

        // Select the socket whose event object was signalled.
        if (!_sm_update_socket(sm, sm->sockets[result - WSA_WAIT_EVENT_0], NULL))
            return 0;
        
#elif defined __UNIX__
        // Poll for any socket event.
        int count = poll(events, sm->active_sockets + 1, TIMEOUT_INDEFINITE);
        ASSERT(count != -1, return 0, "poll() failed!\n");

        // If the signalling event object is the connection changed event,
        // manually reset it. This allows for new sockets to be immediately 
        // polled on the next iteration.
        char buf;
        bool read_connection_changed_event = false;
        while (read(sm->_connection_changed_pipe[PIPE_READ], &buf, sizeof(buf)) > 0)
            read_connection_changed_event = true;
        if (count == 1 && read_connection_changed_event)
            continue;

        // Go through each socket until the first socket that signalled an event
        // has been processed. Not every single is looped through, even if
        // multiple sockets have signalled an event, as the sockets array may
        // change.
        for (int i = 0; i < sm->active_sockets; i++)
        {
            bool updated = false;
            struct mt_socket* selected = sm->sockets[i];
            selected->_pfd.revents = events[i].revents;

            if (!_sm_update_socket(sm, selected, &updated))
                return 0;
            if (updated)
                break;
        }

#else
        ASSERT(false, return 0, "Target platform not supported by socket_manager!");
#endif
    }
}

// Create a new mt_socket instance.
struct mt_socket* mt_socket_new(SOCKET s)
{
    struct mt_socket* sock = (struct mt_socket*)tagged_malloc(sizeof(struct mt_socket),
        TAG_MT_SOCKET);
    sock->socket = s;

#if defined WIN32
    sock->_event = WSACreateEvent();
#elif defined __UNIX__
    sock->_pfd.fd = s;
    sock->_pfd.events = POLLIN;
#else
    tagged_free(sock, TAG_MT_SOCKET);
    ASSERT(false, return NULL, "Target platform not supported by mt_socket!");
#endif
    
    return sock;
}

// Configure an mt_socket instance to be non-blocking.
void mt_socket_configure_non_blocking(struct mt_socket* sock)
{
    ASSERT(sock != NULL, return);

#if defined WIN32
    WSAEventSelect(sock->socket, sock->_event, FD_READ | FD_WRITE | FD_CLOSE);
#elif defined __UNIX__
    fcntl(sock->socket, F_SETFL, fcntl(sock->socket, F_GETFL) | O_NONBLOCK);
#else
    ASSERT(false, return NULL, "Target platform not supported by mt_socket!");
#endif
}

// Flag an mt_socket instance to begin waiting for when send() can be used again.
// This function is effectively a no-op on non-POSIX systems.
void mt_socket_flag_pending_for_send(struct mt_socket* sock)
{
#ifdef __UNIX__
    sock->_pfd.events |= POLLOUT;
    if (sock->parent_sm != NULL)
        _sm_signal_socket_change(sock->parent_sm);
#endif
}

// Shut down an mt_socket instance's connection. This hints to the socket's
// designated socket manager instance so as to handle complete closure 
// appropriately.
void mt_socket_shutdown(struct mt_socket* sock)
{
    sock->listening = false;
    shutdown(sock->socket, SHUT_RDWR);

#if defined WIN32
    WSASetEvent(sock->_event);
#elif defined __UNIX__
    // poll() should immediately return once a socket is shut down.
#else
    ASSERT(false, return NULL, "Target platform not supported by mt_socket!");
#endif
}

// Free an mt_socket instance. This should not be called if the mt_socket instanece
// is currently being managed by a socket manager. If this is the case, please see
// mt_socket_shutdown() instead.
void mt_socket_free(struct mt_socket* sock)
{
    ASSERT(sock != NULL, return);

    // Free any queued data nodes.
    struct mt_socket_data_node* node = sock->data_recv_queue;
    while (node != NULL)
    {
        struct mt_socket_data_node* temp = node;
        node = node->next;
        tagged_free(temp, TAG_TEMP);
    }
    node = sock->data_send_queue;
    while (node != NULL)
    {
        struct mt_socket_data_node* temp = node;
        node = node->next;
        tagged_free(temp, TAG_TEMP);
    }

    // Call the socket's de-allocation function. This is intentionally designed such
    // that this code can easily be decoupled from Bulb for future use. Per Bulb's
    // use case, this is intended to flag the relevant client node for deletion.
    if (sock->dealloc_func != NULL)
        sock->dealloc_func(sock);

    shutdown(sock->socket, SHUT_RDWR);
    closesocket(sock->socket);

    tagged_free(sock, TAG_MT_SOCKET);
}

// Create a new socket manager instance.
struct socket_manager* sm_new()
{
    struct socket_manager* sm = (struct socket_manager*)tagged_malloc(sizeof(struct socket_manager),
        TAG_SOCKET_MANAGER);
    mtx_init(&sm->socket_add_lock, mtx_plain);

    // There must be a way to alert a polling socket manager instance whether a socket
    // connection has been added/removed. However, this implementation is platform-
    // specific.
#if defined WIN32
    sm->_connection_changed_event = WSACreateEvent();
#elif defined __UNIX__
    pipe(sm->_connection_changed_pipe);
    fcntl(sm->_connection_changed_pipe[PIPE_READ], F_SETFL, 
        fcntl(sm->_connection_changed_pipe[PIPE_READ], F_GETFL) | O_NONBLOCK);
    sm->_connection_changed_pfd.fd = sm->_connection_changed_pipe[PIPE_READ];
    sm->_connection_changed_pfd.events = POLLIN;
#else
    ASSERT(false, return NULL, "Target platform not supported by socket_manager!");
#endif

    return sm;
}

// Add an mt_socket instance to a socket manager instance.
bool sm_add(struct socket_manager* sm, struct mt_socket* sock)
{
    ASSERT(sock != NULL, return false);
    if (sm->active_sockets == SOCKETS_PER_POLLING_THREAD)
        return false;
    
    sock->listening = true;
    sock->parent_sm = sm;
    sm->sockets[sm->active_sockets++] = sock;

    if (sm->listening)
        _sm_signal_socket_change(sm);
    return true;
}

// Start listening to pending socket events. Returns false if no sockets are currently
// being managed.
bool sm_listen(struct socket_manager* sm)
{
    ASSERT(sm != NULL, return false);
    ASSERT(!sm->listening, return false);
    if (sm->active_sockets == 0)
        return false;

    thrd_create(&sm->listen_thread, _sm_listen_function, sm);
    sm->listening = true;
    return true;
}

// Merge a socket manager instance into another. This will automatically free the socket
// manager instance's resources from memory when the merger is complete. Returns false
// if there is not enough space to accomodate all of the old socket manager's managed
// sockets, or if the socket manager instance that is requested to be merged into is 
// already completing a merger of its own.
bool sm_merge(struct socket_manager* sm, struct socket_manager* into)
{
    ASSERT(sm != NULL, return false);
    ASSERT(into != NULL, return false);

    // Refuse the merger if the new socket manager instance cannot accommodate the
    // old socket manager instance's number of sockets.
    if (sm->active_sockets > SOCKETS_PER_POLLING_THREAD - _sm_reserved_sockets(into))
        return false;
    
    // Refuse the merger if the new socket manager instance is also completing a merger
    // of its own.
    if (into->merge_into != NULL)
        return false;

    sm->merge_into = into;
    _sm_signal_socket_change(into);
    return true;
}

// Clean-up a socket manager instance. This should not be called if the socket manager
// instance is currently running.
void sm_free(struct socket_manager* sm)
{
    ASSERT(sm != NULL, return);
    
    if (sm->dealloc_func != NULL)
        sm->dealloc_func(sm);

    mtx_destroy(&sm->socket_add_lock);
    tagged_free(sm, TAG_SOCKET_MANAGER);
}