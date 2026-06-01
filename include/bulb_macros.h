// floason (C) 2025
// Licensed under the MIT License.

#pragma once

// Use this as the first port to try as the server, by default.
#define BULB_FIRST_PORT     32765

// These macros do not include the NUL character.
#define MAX_NAME_LENGTH     32
#define MAX_DESC_LENGTH     512
#define MAX_MESSAGE_LENGTH  2048
#define MAX_ERROR_LENGTH    128 // Only used internally.

#define IPV4_ADDRESS_STRLEN 16  // xxx.xxx.xxx.xxx\0

#ifdef BULB_SHARED_LIBRARY
#   ifdef WIN32
#      ifdef BULB_EXPORT
#          define BULB_API __declspec(dllexport)
#      else
#          define BULB_API __declspec(dllimport)
#      endif
#   else
#      define BULB_API __attribute__((visibility("default")))
#   endif
#else
#   define BULB_API
#endif

// Types of *_PRINT_STDOUT.
enum stdout_type
{
    STDOUT_GENERIC,

    // All of these messages are associated with the server disconnecting the client.
    STDOUT_KICK_MSG,
    STDOUT_BAN_MSG,
    STDOUT_SERVER_SHUTDOWN
};