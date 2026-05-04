// floason (C) 2026
// Licensed under the MIT License.

#include <stdatomic.h>

#include "unisock.h"

#ifdef POSIX
#   include <sys/eventfd.h>

    static int shutdown_pipe[2];
    static atomic_size_t shutdown_pipe_ref_count;
#endif

// Configure an mt_socket instance using a pre-initialised socket.
void setup_mt_socket(struct mt_socket* obj, SOCKET sock)
{
    ASSERT(sock, return);
    obj->socket = sock;
    mtx_init(&obj->read_lock, mtx_plain);
    mtx_init(&obj->write_lock, mtx_plain);
    cnd_init(&obj->send_signal);

#if defined WIN32
    obj->_shut_rd_event = WSACreateEvent();
    obj->_socket_event = WSACreateEvent();
#elif defined POSIX
    if (atomic_fetch_add(&shutdown_pipe_ref_count, 1) == 0)
    {
        ASSERT(pipe(&shutdown_pipe) != -1, 
        {
            atomic_fetch_sub(&shutdown_pipe_ref_count, 1);
            return false;
        }, "Could not create shutdown event file descriptor: %d\n", errno);
    }

    obj->_pfd_socket.fd = obj->socket;
    obj->_pfd_shutdown.fd = shutdown_pipe[0];
    obj->_pfd_socket.events = obj->_pfd_shutdown.events = POLLIN;
    obj->_pfd_socket.events |= POLLOUT;
    obj->_shutdown_sequence_started = false;
#endif
}

// Set an mt_socket instance as non-blocking.
void mt_socket_set_non_blocking(struct mt_socket* obj)
{
#if defined WIN32
    WSAEventSelect(obj->socket, obj->_socket_event, FD_READ | FD_WRITE | FD_CLOSE);
#elif defined POSIX
    fcntl(sock, O_NONBLOCK);
#endif
}

// Wait for recv()/send() to be ready. Returns false on socket close.
bool mt_socket_poll(struct mt_socket* obj, bool write)
{
    /*
    *   NOTE!!!
    *
    *   This is *TEMPORARY* code that is subject to change within the short-term,
    *   as Winsock2's event architecture is fundamentally incompatible with the
    *   original networking structure of the Bulb protocol. While POSIX's poll()
    *   could have sufficed, it turns out that WSAPoll() is notoriously buggy
    *   and is not strongly reliable.
    *
    *   In the future, this code will be replaced completely by a Bulb-specific
    *   centralised event handler (which will continue to use poll() on POSIX
    *   systems) which will only invoke client-specific recv()/send() threads
    *   when communication has been made or the socket has been shut down. I'm
    *   hoping for this to occur before v1.0.0.
    */

    if (obj->_shutdown_sequence_started)
        return false;

#if defined WIN32
    WSAEVENT wsaevents[] = { obj->_socket_event, obj->_shut_rd_event };
    size_t no_events = sizeof(wsaevents) / sizeof(wsaevents[0]);
    WSANETWORKEVENTS event_flags;

    for (;;)
    {
        // Poll for any events.
        DWORD index = WSAWaitForMultipleEvents(no_events, wsaevents, FALSE, 
            (write ? (obj->timeout_duration * MILLISECONDS) : WSA_INFINITE), FALSE);
        if (index != WSA_WAIT_EVENT_0)
        {
            if (index == WSA_WAIT_TIMEOUT)
                obj->send_timeout = true;
            return false;
        }

        // Once an event is polled, retrieve the specific network events that
        // took place. If none occurred, discard it as a spurious wakeup and
        // re-iterate.
        memset(&event_flags, 0, sizeof(event_flags));
        WSAEnumNetworkEvents(obj->socket, wsaevents[index - WSA_WAIT_EVENT_0], &event_flags);

        // Exit now if the socket connection was closed.
        if (event_flags.lNetworkEvents & FD_CLOSE)
            return false;

        // Ignore spurious events.
        // TODO - SWITCH TO POLLING-CENTRIC READ/WRITE ARCHITECTURE IN THE FUTURE
        // AS THIS CODE IS NOT THREAD-SAFE!
        if (event_flags.lNetworkEvents == 0
            || (write && (event_flags.lNetworkEvents & FD_READ))
            || (!write && (event_flags.lNetworkEvents & FD_WRITE)))
            continue;

        // Return true if the socket is up and ready.
        return !!(event_flags.lNetworkEvents & (write ? FD_WRITE : FD_READ));
    }
#elif defined POSIX
    struct pollfd pfds = { obj->_pfd_socket, obj->_pfd_shutdown }
    size_t no_events = sizeof(pfds) / sizeof(pfds[0]);

    for (;;)
    {
        // Poll for any events.
        poll(pfds, no_events, (write ? obj->timeout_duration * MILLISECONDS : -1));

        // Check for whether shutdown() was called.
        if (obj->_pfd_shutdown.revents & POLLIN)
        {
            // Because this is a global pipe, this should only return if this
            // socket was uniquely affected.
            if (obj->_shutdown_sequence_started)
            {
                char buf;
                read(shutdown_pipe[0], &buf, sizeof(buf));
                return false;
            }
            continue;
        }

        // Exit now if the socket connection was closed.
        if (obj->_pfd_socket.revents & (POLLERR | POLLHUP))
            return false;

        // Ignore spurious events.
        // TODO - SWITCH TO POLLING-CENTRIC READ/WRITE ARCHITECTURE IN THE FUTURE
        // AS THIS CODE IS NOT THREAD-SAFE!
        if ((write && (obj->_pfd_socket.revents & POLLIN))
            || (!write && (obj->_pfd_socket.revents & POLLOUT)))
            continue;
        
        // Return true if the socket is up and ready.
        return !!(obj->_pfd_socket.revents & (write ? POLLOUT : POLLIN));
    }
#endif

    ASSERT(false, return false, "Operating system not supported!");
}

// Signal an mt_socket shutdown attempt without actually shutting the
// socket's commmunication channels.
void mt_socket_hint_shutdown(struct mt_socket* obj)
{
    obj->_shutdown_sequence_started = true;

#if defined WIN32
    SetEvent(obj->_shut_rd_event);
#elif defined POSIX
    char buf = '\0';
    write(shutdown_pipe[1], &buf, sizeof(buf));
#endif
}

// Call shutdown() on the mt_socket instance.
void mt_socket_shutdown(struct mt_socket* obj, int flags)
{
    shutdown(obj->socket, flags);
    mt_socket_hint_shutdown(obj);
}

// Clear the mt_socket instance's send queue.
void mt_socket_clear_queue(struct mt_socket* obj)
{
    ASSERT(obj != NULL, return);
    while (!QUEUE_EMPTY(obj->send_queue))
    {
        QUEUE_DEQUEUE(struct mt_socket_write_node* node, obj->send_queue, obj->send_queue_tail);
        tagged_free(node, TAG_TEMP);
    }
}

// Cleanup an mt_socket instance and shutdown its associated socket.
void cleanup_mt_socket(struct mt_socket* obj)
{
    ASSERT(obj != NULL, return);
    mt_socket_clear_queue(obj);
    shutdown(obj->socket, SHUT_RDWR);
    closesocket(obj->socket);
    obj->socket = INVALID_SOCKET;

    mtx_destroy(&obj->read_lock);
    mtx_destroy(&obj->write_lock);
    cnd_destroy(&obj->send_signal);

#if defined WIN32
    WSACloseEvent(obj->_socket_event);
    WSACloseEvent(obj->_shut_rd_event);
#elif defined POSIX
    int prev = atomic_fetch_sub(&shutdown_pipe_ref_count, 1);
    ASSERT(prev > 1, return, "mt_socket cleanup when ref counter is zero");
    if (prev == 1)
        close(shutdown_event);
#endif
}