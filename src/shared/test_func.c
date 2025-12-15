// floason (C) 2025
// Licensed under the MIT License.

#include <stdio.h>

#include "bulb_version.h"
#include "test_func.h"

#if defined CLIENT
#   include "client_name.h"
#endif

#if defined SERVER
#   include "server_name.h"
#endif

void test()
{
#if defined CLIENT
    puts("Hello world - " CLIENT_NAME "!");
#elif defined SERVER
    puts("Hello world - " SERVER_NAME "!");
#endif
    printf("Version %d.%d.%d\n", MAJOR, MINOR, PATCH);
}