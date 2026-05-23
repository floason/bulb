// floason (C) 2026
// Licensed under the MIT License.

#include <stdbool.h>
#include <string.h>
#include <ctype.h>

#include "cmds.h"
#include "trie.h"
#include "client_node.h"
#include "server_node.h"
#include "shared_interface.h"

#ifdef CLIENT
#   include "bulb_client.h"
#else
#   include "bulb_server.h"
#endif

#define CMD_ERROR(ERROR_MSG, ...)                                               \
    {                                                                           \
         bulb_printf(BULB_CONSOLE, ERROR_MSG, ##__VA_ARGS__);                   \
         return false;                                                          \
    }

#define CMD_ENFORCE_MIN_PARAM(N)                                                \
    {                                                                           \
        if (params->argc < N)                                                   \
            CMD_ERROR("%s\n", cmd->desc)                                        \
    }

#define CMD_GET_PARAM_CLIENT(NODE, I)                                           \
    struct client_node* NODE = server_find_by_name(server, params->argv[I]);    \
    if (NODE == NULL)                                                           \
        CMD_ERROR("Could not find client \"%s\"!\n", params->argv[I]);          \

static struct trie* bulb_cmds;
static unsigned bulb_cmds_ref_count;

// list: lists all commands that are employed.
bool _cmd_list(struct bulb_cmd* cmd, struct server_node* server, struct cmd_args* params)
{
    TRIE_DFS(bulb_cmds, node, bulb_printf(BULB_CONSOLE, "- %s\n", ((struct bulb_cmd*)node)->desc));
    return true;
}

// status: lists information about the Bulb protocol and server.
bool _cmd_status(struct bulb_cmd* cmd, struct server_node* server, struct cmd_args* params)
{
#ifdef CLIENT
    client_throw_exception(localclient->bulb_client, CLIENT_STATUS_CMD, server->clients_info_head);
#else
    server_throw_exception(server->bulb_server, SERVER_STATUS_CMD, server->clients_info_head);
#endif
    return true;
}

// exit: terminate the protocol.
bool _cmd_exit(struct bulb_cmd* cmd, struct server_node* server, struct cmd_args* params)
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
bool _cmd_kick(struct bulb_cmd* cmd, struct server_node* server, struct cmd_args* params)
{
    CMD_ENFORCE_MIN_PARAM(1);
    CMD_GET_PARAM_CLIENT(client, 0);

    const char* reason = ((params->argc > 1) ? params->argv[1] : "");
    server_kick(server, client, reason);
    return true;
}

// ban: ban a client from the server. This involves writing the client's address
// a database, which by default is Bulb's flat-file banlist implementation.
bool _cmd_ban(struct bulb_cmd* cmd, struct server_node* server, struct cmd_args* params)
{
    CMD_ENFORCE_MIN_PARAM(1);

    const char* reason = ((params->argc > 1) ? params->argv[1] : "You are banned from this server.");
    if (!server_ban(server, params->argv[0], reason))
        CMD_ERROR("The specified address has already been banned!\n");
    return true;
}

// unban: remove an address from the server's database of banned addresses.
bool _cmd_unban(struct bulb_cmd* cmd, struct server_node* server, struct cmd_args* params)
{
    CMD_ENFORCE_MIN_PARAM(1);
    if (!server_unban(server, params->argv[0]))
        CMD_ERROR("The specified address was not banned!\n");
    return true;
}

// banned: check if the specified address is in the server's database of banned
// address.
bool _cmd_banned(struct bulb_cmd* cmd, struct server_node* server, struct cmd_args* params)
{
#ifdef SERVER
    CMD_ENFORCE_MIN_PARAM(1);
    struct bulb_ban obj;
    obj.ip_addr = params->argv[0];
    server_throw_exception(server->bulb_server, SERVER_IS_CLIENT_BANNED, &obj);
    bulb_printf(BULB_CONSOLE, (obj.is_banned ? "yes\n" : "no\n"));
#endif
    return true;
}

// store_bans: write the banlist database to permanent storage.
bool _cmd_store_bans(struct bulb_cmd* cmd, struct server_node* server, struct cmd_args* params)
{
#ifdef SERVER
    server_throw_exception(server->bulb_server, SERVER_BANLIST_SAVE, NULL);
#endif
    return true;
}

// Register a new command. Returns true upon successful registration, otherwise 
// false.
bool bulb_register_cmd(const char* name, const char* desc, bulb_cmd_func func)
{
    if (bulb_cmds == NULL)
        bulb_cmds = trie_new();

    if (strlen(name) > MAX_CMD_NAME_LENGTH || trie_find(bulb_cmds, name) != NULL)
        return false;
    trie_add_copy(bulb_cmds, name, &(struct bulb_cmd){ desc, func }, sizeof(struct bulb_cmd));
    return true;
}

// Parse a command prompt and invoke the appropriate command. Returns false if
// the command was not found.
bool bulb_parse_cmd_input(struct server_node* server, const char* buffer)
{
    ASSERT(bulb_cmds != NULL, return false);

    size_t temp_len = strlen(buffer) + 1;
    char cmd[MAX_CMD_NAME_LENGTH + 1] = "";
    struct cmd_args params = { .argv = tagged_calloc(1, sizeof(char*), TAG_TEMP) };

    // This loop goes up to the end of buffer + the NUL character, in order to
    // handle creating the final parameter/finalising the command name.
    char* temp_buffer = params.argv[params.argc] = tagged_malloc(temp_len, TAG_TEMP);
    bool in_quotes = false;
    bool terminate = false;
    size_t offset = 0;
    for (; offset < temp_len; offset++)
    {
        switch (buffer[offset])
        {
            case '\"':
                in_quotes = !in_quotes;
                continue;
            case '\0':
                offset++;
                in_quotes = false;
                terminate = true;
                break;
            case '&':
                // Allow commands to be chained together if separated by &&.
                if (offset + 1 < temp_len && buffer[offset + 1] == '&' && !in_quotes)
                {
                    offset += 2;
                    in_quotes = false;
                    terminate = true;
                    break;
                }
        }

        if ((isspace(buffer[offset]) && !in_quotes) || terminate)
        {
            if (strlen(temp_buffer) > 0)
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
            if (terminate)
                break;
        }
        else
            temp_buffer[strlen(temp_buffer)] = buffer[offset];
    }

    // The temporary buffer must be free()'d separately as it is still re-allocated 
    // independently of its assignment to any parameter object.
    tagged_free(temp_buffer, TAG_TEMP);

    struct bulb_cmd* cmd_obj = trie_find(bulb_cmds, cmd);
    bool cmd_success = (cmd_obj != NULL);
    if (cmd_obj == NULL)
        goto finish;
    cmd_success = cmd_obj->func(cmd_obj, server, &params);

finish:
    for (int i = 0; i < params.argc; i++)
        tagged_free(params.argv[i], TAG_TEMP);
    tagged_free(params.argv, TAG_TEMP);
    return cmd_success && ((offset < temp_len) ? bulb_parse_cmd_input(server, &buffer[offset]) : true);
}

// Register all shared commands.
void bulb_cmds_init()
{
    if (bulb_cmds_ref_count++ == 0)
    {
        bulb_register_cmd("list", "list", _cmd_list);
        bulb_register_cmd("status", "status", _cmd_status);
        bulb_register_cmd("exit", "exit", _cmd_exit);
    }
}

// Register all server commands.
void bulb_register_server_cmds()
{   
    ASSERT(bulb_cmds_ref_count > 0, return, "Bulb commands not initialized!");
    bulb_register_cmd("kick", "kick username [reason]", _cmd_kick);
    bulb_register_cmd("ban", "ban ip [reason]", _cmd_ban);
    bulb_register_cmd("unban", "unban ip", _cmd_unban);
    bulb_register_cmd("banned", "banned ip (checks if address is banned)", _cmd_banned);
    bulb_register_cmd("store_bans", "store_bans (stores bans permanently in storage)", _cmd_store_bans);
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