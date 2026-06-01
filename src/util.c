// floason (C) 2026
// Licensed under the MIT License.

#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <threads.h>

#include "util.h"

#if defined WIN32
#   include "windows.h"
#endif

void sleeps(unsigned seconds)
{
#if defined __UNIX__
    sleep(seconds);
#elif defined WIN32
    Sleep(seconds * 1000);
#else
    ASSERT(false, return, "sleeps() not supported on target platform");
#endif
}

void set_console_exit_handler(void* func)
{
#if defined WIN32
    SetConsoleCtrlHandler(func, TRUE);
#elif defined __UNIX__
    struct sigaction action = { .sa_handler = func };
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGQUIT, &action, NULL);
    sigaction(SIGINT, &action, NULL);
#else
    ASSERT(false, return, "console_exit_handler() not supported on target platform");
#endif
}

bool timespec_ns_get(struct timespec* timespec)
{
    ASSERT(timespec != NULL, return false);

#if defined WIN32
    static LARGE_INTEGER frequency = { 0 };
    if (frequency.QuadPart == 0)
    {
        ASSERT(QueryPerformanceFrequency(&frequency), return false, 
            "QueryPerformanceFrequency() failed: %d\n", GetLastError());
    }

    LARGE_INTEGER count;
    ASSERT(QueryPerformanceCounter(&count), return false, "QueryPerformanceCounter() failed: %d\n",
        GetLastError());
    timespec->tv_sec = count.QuadPart / frequency.QuadPart;
    timespec->tv_nsec = ((count.QuadPart * NANOSECONDS) / frequency.QuadPart) % NANOSECONDS;
    return true;
#elif defined __UNIX__  
    clock_gettime(CLOCK_MONOTONIC, timespec);
    return true;
#endif

    ASSERT(false, return false, "Target platform not supported by timespec_ns_get() function");
}