// floason (C) 2026
// Licensed under the MIT License.

#include <stdio.h>
#include <stdbool.h>
#include <stddef.h>

#include "util.h"
#include "bulb_macros.h"
#include "bulb_banlist.h"
#include "trie.h"

struct banlist_record
{   
    char ip_addr[IPV4_ADDRESS_STRLEN];
    char reason[MAX_BANLIST_REASON_LENGTH + 1];
};

// Read from a banlist database text file into memory. This will create a new
// database if the file is not present. Returns false on failure.
bool server_banlist_load(struct bulb_server* server)
{
    ASSERT(server != NULL, return false);
    server_banlist_close(server);

    // xxx.xxx.xxx.xxx [reason]\0
    char line_buffer[IPV4_ADDRESS_STRLEN + 1 + MAX_BANLIST_REASON_LENGTH + 1]; 

    FILE* file = fopen("banlist.txt", "a+");
    ASSERT(file != NULL, return false);
    
    server->banlist = trie_new();
    while (fgets(line_buffer, sizeof(line_buffer), file) != NULL)
    {
        // Read the address for this record.
        char ip_addr[IPV4_ADDRESS_STRLEN] = { 0 };
        size_t i = 0;
        size_t line_buffer_len = strlen(line_buffer);
        for (; i < sizeof(ip_addr) && i < line_buffer_len; i++)
        {
            if (isspace(line_buffer[i]))
                break;
            ip_addr[strlen(ip_addr)] = line_buffer[i];
        }

        // If the address for this record already exists, filter it out.
        if (trie_find(server->banlist, ip_addr) != NULL)
            continue;

        // Create a new banlist record and optionally read into its reason field,
        // if a ban reason is specified.
        struct banlist_record* record = tagged_malloc(sizeof(struct banlist_record), TAG_BANLIST_RECORD);
        strncpy(record->ip_addr, ip_addr, sizeof(record->ip_addr));
        if (i + 1 < line_buffer_len)
        {
            strncpy(record->reason, line_buffer + i + 1, sizeof(record->reason));
            size_t len = strlen(record->reason);
            if (record->reason[len - 1] == '\n')
                record->reason[len - 1] = '\0';
        }

        // Add this record to the banlist dictionary.
        if (trie_add(server->banlist, record->ip_addr, record) == NULL)
            tagged_free(record, TAG_BANLIST_RECORD);
    }

    fclose(file);
    return true;
}

// Add a new IP address to the banlist database. Returns false if the address
// is already present, or on failure.
bool server_banlist_addip(struct bulb_server* server, 
                          const char* ip_addr, 
                          const char* reason, 
                          bool* already_banned)
{
    ASSERT(server != NULL, return false);
    ASSERT(server->banlist != NULL, return false);

    bool found = (trie_find(server->banlist, ip_addr) != NULL);
    if (already_banned != NULL)
        *already_banned = found;
    if (found)
        return false;

    struct banlist_record* record = tagged_malloc(sizeof(struct banlist_record), TAG_BANLIST_RECORD);
    strncpy(record->ip_addr, ip_addr, sizeof(record->ip_addr));
    strncpy(record->reason, reason, sizeof(record->reason));
    return trie_add(server->banlist, ip_addr, record);
}

// Remove an IP address from the banlist database. Returns false if the address
// was not already present, or on failure.
bool server_banlist_removeip(struct bulb_server* server, 
                             const char* ip_addr, 
                             bool* already_banned)
{
    ASSERT(server != NULL, return false);
    ASSERT(server->banlist != NULL, return false);

    struct banlist_record* record = trie_find(server->banlist, ip_addr);
    if (already_banned != NULL)
        *already_banned = (record != NULL);
    if (record == NULL)
        return false;

    return trie_delete(server->banlist, ip_addr);
}

// Check if an IP address is already included in the banlist database.
bool server_banlist_isbanned(struct bulb_server* server, 
                             const char* ip_addr, 
                             const char** reason)
{
    ASSERT(server != NULL, return false);
    ASSERT(server->banlist != NULL, return false);
    
    struct banlist_record* record = trie_find(server->banlist, ip_addr);
    if (record == NULL)
        return false;
    if (reason != NULL)
        *reason = record->reason;
    return true;
}

// Store the banlist database text file onto permanent storage. Returns false
// on failure.
bool server_banlist_store(struct bulb_server* server)
{
    ASSERT(server != NULL, return false);
    ASSERT(server->banlist != NULL, return false);

    FILE* file = fopen("banlist.txt", "w");
    ASSERT(file, return false);
    TRIE_DFS(server->banlist, node,
    {
        struct banlist_record* record = (struct banlist_record*)node;
        fprintf(file, "%s %s\n", record->ip_addr, record->reason);
    });

    fclose(file);
    return true;
}

// De-allocate the loaded banlist database from memory.
BULB_API void server_banlist_close(struct bulb_server* server)
{
    ASSERT(server, return);
    if (server->banlist)
    {
        server_banlist_store(server);
        trie_free(server->banlist);
    }
}