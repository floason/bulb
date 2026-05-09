// floason (C) 2026
// Licensed under the MIT License.

#pragma once

#include "unisock.h"
#include "client_node.h"
#include "server_node.h"

#define MAX_CMD_NAME_LENGTH 32

struct bulb_cmd;
struct cmd_args;

typedef bool (*bulb_cmd_func)(struct bulb_cmd* cmd, struct server_node* server, struct cmd_args* params);

struct cmd_args
{
    int argc;
    char** argv;
};

struct bulb_cmd
{
    const char* desc;
    bulb_cmd_func func;
};

// Register a new command. Returns true upon successful registration, otherwise 
// false.
bool bulb_register_cmd(const char* name, const char* desc, bulb_cmd_func func);

// Parse a command prompt and invoke the appropriate command. Returns false if
// the command was not found.
bool bulb_parse_cmd_input(struct server_node* server, const char* buffer);

// Register all shared commands.
void bulb_cmds_init();

// Register all server commands.
void bulb_register_server_cmds();

// Cleanup on process exit.
void bulb_cmds_cleanup();