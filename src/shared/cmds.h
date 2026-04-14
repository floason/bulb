// floason (C) 2026
// Licensed under the MIT License.

#pragma once

#include "unisock.h"
#include "client_node.h"
#include "server_node.h"

#define MAX_CMD_NAME_LENGTH 32

struct cmd_args
{
    int argc;
    char* param;
    struct cmd_args* next;
};

typedef bool (*bulb_cmd_func)(struct server_node* server, struct cmd_args* params);

// Register a new command.
void bulb_register_cmd(const char* name, bulb_cmd_func func);

// Parse a command prompt and invoke the appropriate command. Returns false if
// the command was not found.
bool bulb_parse_cmd_input(struct server_node* server, const char* buffer);

// Register all shared commands.
void bulb_register_shared_cmds();

// Cleanup on process exit.
void bulb_cmds_cleanup();