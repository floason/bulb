// floason (C) 2025
// Licensed under the MIT License.

#pragma once

#include <errno.h>
#include <threads.h>
#include <time.h>
#include <stdbool.h>

#include "util.h"

// Define Windows-specific macros and headers.
#if defined WIN32
    // Do not include Winsock.h (if I need Windows.h in the future).
#   ifndef WIN32_LEAN_AND_MEAN
#       define WIN32_LEAN_AND_MEAN
#   endif

#   include <winsock2.h>
#   include <ws2tcpip.h>

    // Define shutdown macros here.
#   define SHUT_RD              SD_RECEIVE
#   define SHUT_WR              SD_SEND
#   define SHUT_RDWR            SD_BOTH
    
    // Define common socket errors here.
#   define SOCKET_CLOSED        WSAENOTCONN
#   define SOCKET_TIMEDOUT      WSAETIMEDOUT
#   define SOCKET_AGAIN         WSAEWOULDBLOCK

    // This is used to suppress SIGPIPE on POSIX, however this macro does not exist
    // in Winsock2.
#   define MSG_NOSIGNAL         0

// Define POSIX-specific macros and headers.
#elif defined __UNIX__
#   include <unistd.h>
#   include <sys/types.h>
#   include <sys/socket.h>
#   include <sys/time.h>
#   include <arpa/inet.h>
#   include <netdb.h>

    // Define SOCKET as int since POSIX sockets are int descriptors.
#   define SOCKET               int

    // Windows uses closesocket(), but POSIX just uses close() as it works for all
    // int descriptors. As closesocket() is more clear, define a macro here for 
    // closesocket().
#   define closesocket          close

#   define SOCKET_ERROR         -1
#   define INVALID_SOCKET       -1

    // Define common socket errors here.
#   define SOCKET_CLOSED        ENOTCONN
#   define SOCKET_TIMEDOUT      ETIMEDOUT
#   define SOCKET_AGAIN         EWOULDBLOCK
#endif

#define MT_SOCKET_INDEFINITE    -1L

static inline int set_socket_timeout(SOCKET sock, unsigned timeout, bool write)
{
#if defined WIN32
    DWORD optval = timeout;
#elif defined __UNIX__
    struct timeval optval;
    optval.tv_sec = timeout;
#else
    ASSERT(false, return, "Operating system not supported!\n");
#endif
    return setsockopt(sock, (write ? SO_SNDTIMEO : SO_RCVTIMEO), SO_SNDTIMEO, (const char*)&optval, 
        sizeof(optval));
}

static inline int socket_errno()
{
#ifdef WIN32
    return WSAGetLastError();
#endif
    return errno;
}

// Due to the multithreaded nature of my code, I decided to create a struct that 
// encapsulates a SOCKET, coupled with information that manages the read/write
// state of the given SOCKET.

struct mt_socket_write_node
{
    struct mt_socket_write_node* prev;
    struct mt_socket_write_node* next;
    size_t len;
    
    char data[];
};

struct mt_socket
{
    SOCKET socket;
    mtx_t read_lock;
    mtx_t write_lock;
    int64_t timeout_duration;
    bool send_timeout;

    cnd_t send_signal;
    struct mt_socket_write_node* send_queue;
    struct mt_socket_write_node* send_queue_tail;

    struct timespec ping_start;
    struct timespec ping_end;

    // recv() may not terminate immediately if the socket's read connection is shut
    // down. Unfortunately this requires different implementations across separate
    // platforms.
    bool _shutdown_sequence_started;
#if defined WIN32
    // Use Winsock2's event handling interface for reliably terminating recv()/
    WSAEVENT _socket_event;
    WSAEVENT _shut_rd_event;
#elif defined POSIX
    // In order to reliably handle terminating recv() when shutting down the socket's
    // read connection across POSIX systems, poll() will be used with a global event
    // file descriptor, alongside the given socket.
    struct pollfd _pfd_socket;
    struct pollfd _pfd_shutdown;
#endif
};

// Configure an mt_socket instance using a pre-initialised socket.
void setup_mt_socket(struct mt_socket* obj, SOCKET sock);

// Set an mt_socket instance as non-blocking.
void mt_socket_set_non_blocking(struct mt_socket* obj);

// Wait for recv()/send() to be ready. Returns false on socket close.
bool mt_socket_poll(struct mt_socket* obj, bool write);

// Signal an mt_socket shutdown attempt without actually shutting the
// socket's commmunication channels.
void mt_socket_hint_shutdown(struct mt_socket* obj);

// Call shutdown() on the mt_socket instance.
void mt_socket_shutdown(struct mt_socket* obj, int flags);

// Clear the mt_socket instance's send queue.
void mt_socket_clear_queue(struct mt_socket* obj);

// Cleanup an mt_socket instance and shutdown its associated socket.
void cleanup_mt_socket(struct mt_socket* obj);