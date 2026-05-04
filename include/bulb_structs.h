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

    // Settings applicable to both client or server.
    unsigned timeout_s;

    // Server-only settings.
    unsigned server_shutdown_timeout_s;

    // Variables modified by the running server instance.
    unsigned ping_ms;
};

struct bulb_message
{
    const char* name;
    const char* message;
    bool is_server;
};

// Set default settings for a bulb_userinfo struct instance.
static inline void bulb_userinfo_defaults(struct bulb_userinfo* userinfo, bool is_server)
{
    if (is_server)
    {
        userinfo->timeout_s = 300;
        userinfo->server_shutdown_timeout_s = 5;
    }
    else
    {
        userinfo->timeout_s = 30;
    }
}