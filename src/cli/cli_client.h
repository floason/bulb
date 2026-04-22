// floason (C) 2026
// Licensed under the MIT License.

#include <stdint.h>

#include "bulb_client.h"

struct bulb_client* cli_client_init(const char* custom_host, uint16_t custom_port);

void cli_client_cleanup(struct bulb_client* client);