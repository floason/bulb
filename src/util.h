// floason (C) 2025
// Licensed under the MIT License.

#pragma once

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>

#if defined __unix__ || defined __APPLE__
#   define __UNIX__
#endif

// Macro stringizing function.
#define _STR(X) #X
#define STR(X) _STR(X)

// Outputs number of parameters passed into NUM_ARGS function macro
// (supports 0 to 127 parameters).
#define _NUM_ARGS(_0, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, _40, _41, _42, _43, _44, _45, _46, _47, _48, _49, _50, _51, _52, _53, _54, _55, _56, _57, _58, _59, _60, _61, _62, _63, _64, _65, _66, _67, _68, _69, _70, _71, _72, _73, _74, _75, _76, _77, _78, _79, _80, _81, _82, _83, _84, _85, _86, _87, _88, _89, _90, _91, _92, _93, _94, _95, _96, _97, _98, _99, _100, _101, _102, _103, _104, _105, _106, _107, _108, _109, _110, _111, _112, _113, _114, _115, _116, _117, _118, _119, _120, _121, _122, _123, _124, _125, _126, _127, N, ...) N
#define NUM_ARGS(...) _NUM_ARGS(, __VA_ARGS__, 127, 126, 125, 124, 123, 122, 121, 120, 119, 118, 117, 116, 115, 114, 113, 112, 111, 110, 109, 108, 107, 106, 105, 104, 103, 102, 101, 100, 99, 98, 97, 96, 95, 94, 93, 92, 91, 90, 89, 88, 87, 86, 85, 84, 83, 82, 81, 80, 79, 78, 77, 76, 75, 74, 73, 72, 71, 70, 69, 68, 67, 66, 65, 64, 63, 62, 61, 60, 59, 58, 57, 56, 55, 54, 53, 52, 51, 50, 49, 48, 47, 46, 45, 44, 43, 42, 41, 40, 39, 38, 37, 36, 35, 34, 33, 32, 31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)

#define ASSERT(EXP, ASSERT_SCOPE, ...)              \
    if (!(EXP))                                     \
    {                                               \
        if (NUM_ARGS(__VA_ARGS__) > 0)              \
            _assert_error(stderr, ##__VA_ARGS__ );  \
        assert(0);                                  \
        ASSERT_SCOPE;                               \
    }                                               \
    
#ifndef MIN
#   define MIN(A, B) (((A) < (B)) ? (A) : (B))
#endif
#ifndef MAX
#   define MAX(A, B) (((A) > (B)) ? (A) : (B))
#endif

/* Doubly-linked list function macros start */

#define LINKED_LIST_ADD(NODE, HEAD, TAIL)                                   \
    {                                                                       \
        NODE->next = NULL;                                                  \
        NODE->prev = TAIL;                                                  \
        if (TAIL != NULL)                                                   \
            TAIL->next = NODE;                                              \
        TAIL = NODE;                                                        \
        if (HEAD == NULL)                                                   \
            HEAD = NODE;                                                    \
    }                                                                       \

#define LINKED_LIST_REMOVE(NODE, HEAD, TAIL)                                \
    {                                                                       \
        if (NODE->next)                                                     \
            NODE->next->prev = NODE->prev;                                  \
        else                                                                \
            TAIL = NODE->prev;                                              \
        if (NODE->prev)                                                     \
            NODE->prev->next = NODE->next;                                  \
        else                                                                \
            HEAD = NODE->next;                                              \
    }                                                                       \

#define LINKED_LIST_EMPTY(NODE)     (NODE == NULL)

#define QUEUE_ENQUEUE(NODE, HEAD, TAIL) LINKED_LIST_ADD(NODE, HEAD, TAIL)
#define QUEUE_DEQUEUE(NODE, HEAD, TAIL)                                     \
    ASSERT(HEAD != NULL, { }, "Attempted to dequeue from empty queue!");    \
    NODE = HEAD;                                                            \
    LINKED_LIST_REMOVE(HEAD, HEAD, TAIL)
#define QUEUE_EMPTY(NODE)               LINKED_LIST_EMPTY(NODE)

/* Doubly-linked list function macros end */

/* DO NOT USE! */
static inline void _assert_error(FILE* stream, ...)
{
    va_list argv;
    va_start(argv, stream);
    const char* format = va_arg(argv, const char*);
    vfprintf(stream, format, argv);
    va_end(argv);
}

static inline int str_isprint(const char* str)
{
    for (int i = 0, len = strlen(str); i < len; i++)
    {
        if (!isprint(str[i]))
            return false;
    }
    return true;
}

static inline void* quick_calloc(size_t count, size_t size)
{
    void* ptr = calloc(count, size);
    if (!ptr)
        abort();
    return ptr;
}

static inline void* quick_malloc(size_t size)
{
    return quick_calloc(1, size);
}

/*
 * The functions below are used with wrappers around calloc()/free() which
 * effectively tag all dynamically-allocated chunks of memory.
*/

enum tags
{
    TAG_FIRST,
    TAG_TEMP,

    TAG_TRIE,
    TAG_BULB_CLIENT,
    TAG_BULB_SERVER,
    TAG_CLIENT_NODE,
    TAG_SERVER_NODE,
    TAG_BULB_OBJ,

    TAG_LAST
};

void* tagged_calloc(size_t count, size_t size, int tag);

void* tagged_malloc(size_t size, int tag);

void tagged_free(void* ptr, int tag);