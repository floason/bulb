// floason (C) 2026
// Licensed under the MIT License.

#pragma once

#include <stdbool.h>

#include "bulb_macros.h"

struct bulb_userinfo
{
    char name[MAX_NAME_LENGTH + 1];
    char description[MAX_DESC_LENGTH + 1];

    // Used to authenticate the version of the Bulb protocol.
    short major;
    short minor;
    short patch;
};

struct bulb_message
{
    const char* name;
    const char* message;
    bool is_server;
};