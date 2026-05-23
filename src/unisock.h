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

#   define PIPE_READ            0
#   define PIPE_WRITE           1
#endif

#define SOCKET_TRANSFER_CLOSED  0
#define TIMEOUT_INDEFINITE      -1L

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