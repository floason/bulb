// floason (C) 2026
// Licensed under the MIT License.

#pragma once

#include "bulb_macros.h"
#include "bulb_server.h"

#define MAX_BANLIST_REASON_LENGTH   1024

// Read from a banlist database text file into memory. This will create a new
// database if the file is not present. Returns false on failure.
BULB_API bool server_banlist_load(struct bulb_server* server);

// Add a new IP address to the banlist database. Returns false if the address
// is already present, or on failure.
BULB_API bool server_banlist_addip(struct bulb_server* server, 
                                   const char* ip_addr, 
                                   const char* reason,
                                   bool* already_banned);

// Remove an IP address from the banlist database. Returns false if the address
// was not already present, or on failure.
BULB_API bool server_banlist_removeip(struct bulb_server* server, 
                                      const char* ip_addr, 
                                      bool* already_banned);

// Check if an IP address is already included in the banlist database.
BULB_API bool server_banlist_isbanned(struct bulb_server* server, 
                                      const char* ip_addr, 
                                      const char** reason);

// Store the banlist database text file onto permanent storage. Returns false
// on failure.
BULB_API bool server_banlist_store(struct bulb_server* server);

// De-allocate the loaded banlist database from memory.
BULB_API void server_banlist_close(struct bulb_server* server);