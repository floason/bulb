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
#include "userinfo_obj.h"

#ifdef CLIENT
#   include "client.h"
#else
#   include "server.h"
#endif

static struct trie* bulb_cmds;

// status: lists information about the Bulb protocol and server.
bool _cmd_status(struct server_node* server, struct client_node* client, struct cmd_args* params)
{
    bulb_printver();
    printf("no. connected: %d\n", server->number_connected);
    LOOP_CLIENTS(server->clients, NULL, node, 
    {
        printf("- \"%s\"", node->userinfo->name);
#ifdef SERVER
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &node->addr.sin_addr, ip_str, sizeof(ip_str));
        printf(" (%s)", ip_str);
#endif
        puts("");
    });
    return true;
}

// exit: terminate the connnection.
bool _cmd_exit(struct server_node* server, struct client_node* client, struct cmd_args* params)
{
#ifdef CLIENT
    client_throw_exception(client->bulb_client, CLIENT_DISCONNECT, NULL);
#else
    server_throw_exception(server->bulb_server, SERVER_FINISH, NULL);
#endif
    return true;
}

// Register a new command.
void bulb_register_cmd(const char* name, bulb_cmd_func func)
{
    if (bulb_cmds == NULL)
        bulb_cmds = trie_new();

    ASSERT(strlen(name) <= MAX_CMD_NAME_LENGTH, return);
    ASSERT(trie_find(bulb_cmds, name) == NULL, return);
    trie_add(bulb_cmds, name, func);
}

// Parse a command prompt and invoke the appropriate command. Returns false if
// the command was not found.
bool bulb_parse_cmd_input(struct server_node* server, struct client_node* client, const char* buffer)
{
    ASSERT(bulb_cmds != NULL, return false);

    int buffer_len = strlen(buffer), temp_len = buffer_len + 1;
    char cmd[MAX_CMD_NAME_LENGTH + 1] = "";
    char* temp_buffer = (char*)quick_malloc(temp_len);
    struct cmd_args* params = NULL;
    struct cmd_args* next = NULL;

    // This loop goes up to the end of buffer + the NUL character, in order to
    // handle creating the final parameter/finalising the command name.
    for (int i = 0; i < temp_len; i++)
    {
        if ((isspace(buffer[i]) || buffer[i] == '\0') && strlen(temp_buffer) > 0)
        {
            if (cmd[0] == '\0')
                strncpy(cmd, temp_buffer, sizeof(cmd));
            else
            {
                if (params == NULL)
                    next = params = (struct cmd_args*)quick_malloc(sizeof(struct cmd_args));
                else
                    next = params->next = (struct cmd_args*)quick_malloc(sizeof(struct cmd_args));
                params->argc++;
                next->param = temp_buffer;
                temp_buffer = (char*)quick_malloc(temp_len);
            }
            memset(temp_buffer, 0, temp_len);
        }
        else
            temp_buffer[strlen(temp_buffer)] = buffer[i];
    }

    bulb_cmd_func func = trie_find(bulb_cmds, buffer);
    if (func == NULL)
        goto finish;
    func(server, client, params);

finish:
    next = params;
    while (next)
    {
        params = next;
        free(next->param);
        free(next);
        next = params->next;
    }

    // This must also be free()'d as it is still re-allocated independently of
    // its assignment to any parameter object.
    free(temp_buffer);
    return func != NULL;
}

// Register all shared commands.
void bulb_register_shared_cmds()
{
    bulb_register_cmd("status", _cmd_status);
    bulb_register_cmd("exit", _cmd_exit);
}

// Cleanup on process exit.
void bulb_cmds_cleanup()
{
    if (bulb_cmds != NULL)
        trie_free(bulb_cmds);
}