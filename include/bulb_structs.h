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
    bool is_server;
    bool ping_clients;
    bool init_bulb_banlist_database;    // Used to initialise the Bulb flat-file banlist database.
    bool print_ban_message_to_all;      // Print ban message to all clients if banned address connects.
    unsigned server_shutdown_timeout_s; // Time spent waiting for clients to exit on called server exit.

    // Variables modified by the running server instance.
    unsigned ping_ms;
    char ip_addr[IPV4_ADDRESS_STRLEN];

    // bulb_userinfo objects can be linked together. This is the mechanism
    // by which the server's connected clients and their information
    // can be traversed, such as through invoking the status command.
    struct bulb_userinfo* prev;
    struct bulb_userinfo* next;
    bool linked;
};

struct bulb_stdout
{
    const char* message;
    enum stdout_type type;
};

struct bulb_message
{
    const char* name;
    const char* message;
    bool is_server;
};

struct bulb_ban
{
    // Information describing who to ban and why.
    const char* ip_addr;
    const char* reason;
    void* additional_data;  // Unused if using Bulb's banlist implementation.

    // Response from the exception handler. If this is true, the reason will
    // be stored in the reason field.
    bool is_banned;
};

// Set default settings for a bulb_userinfo struct instance.
static inline void bulb_userinfo_defaults(struct bulb_userinfo* userinfo)
{
    if (userinfo->is_server)
    {
        userinfo->timeout_s = 300;
        userinfo->ping_clients = true;
        userinfo->server_shutdown_timeout_s = 5;
        userinfo->init_bulb_banlist_database = true;
        userinfo->print_ban_message_to_all = true;
    }
    else
    {
        userinfo->timeout_s = 30;
    }
}