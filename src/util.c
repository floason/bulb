// floason (C) 2026
// Licensed under the MIT License.

#include <stdlib.h>
#include <stdint.h>
#include <time.h>

#include "util.h"

#if defined WIN32
#   include "windows.h"
#endif

#define TAGGED_MAGIC    0xC3DA4DF1

static struct tagged_chunk
{
    int magic;
    size_t count;
    size_t size;

    struct tagged_chunk* prev;
    struct tagged_chunk* next;
    bool linked;
    enum tags tag;
} *tagged_head, *tagged_tail;

void* tagged_calloc(size_t count, size_t size, int tag)
{
    struct tagged_chunk* ptr = (struct tagged_chunk*)quick_calloc(1, 
        sizeof(struct tagged_chunk) + count * size);
    ptr->magic = TAGGED_MAGIC;
    ptr->tag = tag;
    ptr->count = count;
    ptr->size = size;
    
    LINKED_LIST_ADD(ptr, tagged_head, tagged_tail);
    return (void*)((uintptr_t)ptr + sizeof(struct tagged_chunk));
}

void* tagged_malloc(size_t size, int tag)
{
    return tagged_calloc(1, size, tag);
}

void tagged_free(void* ptr, int tag)
{
    ASSERT(ptr != NULL, return);
    
    struct tagged_chunk* chunk = (struct tagged_chunk*)((uintptr_t)ptr - sizeof(struct tagged_chunk));
    ASSERT(chunk->magic == TAGGED_MAGIC, return);
    ASSERT(chunk->tag == tag, return);

    LINKED_LIST_REMOVE(chunk, tagged_head, tagged_tail);
    chunk->magic = TAG_FIRST;
    free(chunk);
}

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