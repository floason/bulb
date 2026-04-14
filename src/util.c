// floason (C) 2026
// Licensed under the MIT License.

#include <stdint.h>

#include "util.h"

#define TAGGED_MAGIC    0xC3DA4DF1

static struct tagged_chunk
{
    int magic;
    size_t count;
    size_t size;

    struct tagged_chunk* prev;
    struct tagged_chunk* next;
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