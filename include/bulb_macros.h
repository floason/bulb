// floason (C) 2025
// Licensed under the MIT License.

#pragma once

// Use this as the first port to try as the server, by default.
#define FIRST_PORT          32765

// These macros do not include the NUL character.
#define MAX_NAME_LENGTH     32
#define MAX_MESSAGE_LENGTH  2048
#define MAX_ERROR_LENGTH    128 // Only used internally.

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