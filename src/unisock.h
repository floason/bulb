// floason (C) 2025
// Licensed under the MIT License.

#pragma once

#include <errno.h>

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
#   define SHUT_RD          SD_RECEIVE
#   define SHUT_WR          SD_SEND
#   define SHUT_RDWR        SD_BOTH

// Define POSIX-specific macros and headers.
#elif defined __UNIX__
#   include <unistd.h>
#   include <sys/types.h>
#   include <sys/socket.h>
#   include <arpa/inet.h>
#   include <netdb.h>

    // Define SOCKET as int since POSIX sockets are int descriptors.
#   define SOCKET           int

    // Windows uses closesocket(), but POSIX just uses close() as it works for all
    // int descriptors. As closesocket() is more clear, define a macro here for 
    // closesocket().
#   define closesocket      close

#   define SOCKET_ERROR     -1
#   define INVALID_SOCKET   -1
#endif

#ifndef MIN
#   define MIN(A, B) (((A) < (B)) ? (A) : (B))
#endif
#ifndef MAX
#   define MAX(A, B) (((A) > (B)) ? (A) : (B))
#endif

static inline int socket_errno()
{
#ifdef WIN32
    return WSAGetLastError();
#endif
    return errno;
}