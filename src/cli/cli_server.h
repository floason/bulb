// floason (C) 2026
// Licensed under the MIT License.

#include "bulb_server.h"

struct bulb_server* cli_server_init(uint16_t custom_port);

void cli_server_cleanup(struct bulb_server* server);