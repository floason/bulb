// floason (C) 2026
// Licensed under the MIT License.

#include <stdbool.h>
#include <string.h>
#include <ctype.h>

#include "unisock.h"
#include "bulb_version.h"
#include "cmds.h"
#include "trie.h"
#include "client_node.h"
#include "server_node.h"
#include "shared_interface.h"

#ifdef CLIENT
#   include "bulb_client.h"

#   define CONSOLE localclient
#else
#   include "bulb_server.h"

#   define CONSOLE server
#endif

#define CMD_ERROR(ERROR_MSG, ...)                       \
    {                                                   \
         bulb_printf(CONSOLE, ERROR_MSG, #__VA_ARGS__); \
         return false;                                  \
    }

static struct trie* bulb_cmds;
static unsigned bulb_cmds_ref_count;

// status: lists information about the Bulb protocol and server.
bool _cmd_status(struct server_node* server, struct cmd_args* params)
{
#ifdef CLIENT
    client_throw_exception(localclient->bulb_client, CLIENT_STATUS_CMD, server->clients_info_head);
#else
    server_throw_exception(server->bulb_server, SERVER_STATUS_CMD, server->clients_info_head);
#endif
    return true;
}

// exit: terminate the protocol.
bool _cmd_exit(struct server_node* server, struct cmd_args* params)
{
#ifdef CLIENT
    localclient->exit_is_orderly = true;
    server_disconnect_client(server, localclient, false, false, false);
#else
    server_shutdown(server->bulb_server, server->info.server_shutdown_timeout_s);
#endif
    return true;
}

// kick: kick a client from the server.
bool _cmd_kick(struct server_node* server, struct cmd_args* params)
{
    if (params->argc < 1)
        CMD_ERROR("-kick username [reason]\n");

    struct client_node* client = server_find_by_name(server, params->argv[0]);
    if (client == NULL)
        CMD_ERROR("Could not find client \"%s\"!\n", params->argv[0]);

    const char* reason = ((params->argc > 1) ? params->argv[1] : "");
    server_kick(server, client, reason);
    return true;
}

// Register a new command. Returns true upon successful registration, otherwise 
// false.
bool bulb_register_cmd(const char* name, bulb_cmd_func func)
{
    if (bulb_cmds == NULL)
        bulb_cmds = trie_new();

    if (strlen(name) > MAX_CMD_NAME_LENGTH || trie_find(bulb_cmds, name) != NULL)
        return false;
    trie_add(bulb_cmds, name, func);
    return true;
}

// Parse a command prompt and invoke the appropriate command. Returns false if
// the command was not found.
bool bulb_parse_cmd_input(struct server_node* server, const char* buffer)
{
    ASSERT(bulb_cmds != NULL, return false);

    size_t temp_len = strlen(buffer) + 1;
    char cmd[MAX_CMD_NAME_LENGTH + 1] = "";
    struct cmd_args params = { };
    params.argv = tagged_calloc(1, sizeof(char*), TAG_TEMP);

    // This loop goes up to the end of buffer + the NUL character, in order to
    // handle creating the final parameter/finalising the command name.
    char* temp_buffer = params.argv[params.argc] = tagged_malloc(temp_len, TAG_TEMP);
    bool in_quotes = false;
    for (int i = 0; i < temp_len; i++)
    {
        switch (buffer[i])
        {
            case '\"':
                in_quotes = !in_quotes;
                continue;
            case '\0':
                in_quotes = false;
                break;
        }

        
        if ((isspace(buffer[i]) || buffer[i] == '\0') && strlen(temp_buffer) > 0 && !in_quotes)
        {
            size_t cmd_len = strlen(cmd);
            if (cmd_len == 0)
            {
                strncpy(cmd, temp_buffer, sizeof(cmd));
                memset(temp_buffer, 0, temp_len);
            }
            else
            {
                char** new = tagged_calloc(1 + (++params.argc), sizeof(char*), TAG_TEMP);
                memcpy(new, params.argv, sizeof(char**) * params.argc);
                tagged_free(params.argv, TAG_TEMP);
                params.argv = new;
                temp_buffer = params.argv[params.argc] = tagged_malloc(temp_len, TAG_TEMP);
            }
        }
        else
            temp_buffer[strlen(temp_buffer)] = buffer[i];
    }

    // The temporary buffer must be free()'d separately as it is still re-allocated 
    // independently of its assignment to any parameter object.
    tagged_free(temp_buffer, TAG_TEMP);

    bulb_cmd_func func = trie_find(bulb_cmds, cmd);
    bool cmd_success = (func == NULL);
    if (func == NULL)
        goto finish;
    cmd_success = func(server, &params);

finish:
    for (int i = 0; i < params.argc; i++)
        tagged_free(params.argv[i], TAG_TEMP);
    tagged_free(params.argv, TAG_TEMP);
    return cmd_success;
}

// Register all shared commands.
void bulb_cmds_init()
{
    if (bulb_cmds_ref_count++ == 0)
    {
        bulb_register_cmd("status", _cmd_status);
        bulb_register_cmd("exit", _cmd_exit);
    }
}

// Register all server commands.
void bulb_register_server_cmds()
{   
    ASSERT(bulb_cmds_ref_count > 0, return, "Bulb commands not initialized!");
    bulb_register_cmd("kick", _cmd_kick);
}

// Cleanup on process exit.
void bulb_cmds_cleanup()
{
    if (--bulb_cmds_ref_count == 0)
    {
        if (bulb_cmds != NULL)
            trie_free(bulb_cmds);
    }
}