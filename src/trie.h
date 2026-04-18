// floason (C) 2026
// Licensed under the MIT License.

#pragma once

#include <stdbool.h>
#include <threads.h>

#define TRIE_DFS(ROOT, ID, SCOPE)                                               \
    {                                                                           \
        struct trie* node##ID = ROOT->children;                                 \
        while (node##ID != NULL)                                                \
        {                                                                       \
            if (node##ID->value != NULL)                                        \
            {                                                                   \
                void* ID = node##ID->value;                                     \
                SCOPE;                                                          \
            }                                                                   \
                                                                                \
            if (node##ID->children != NULL)                                     \
                node##ID = node##ID->children;                                  \
            else                                                                \
            {                                                                   \
                while (node##ID != NULL && node##ID->next == NULL)              \
                    node##ID = node##ID->parent;                                \
                if (node##ID != NULL)                                           \
                    node##ID = node##ID->next;                                  \
            }                                                                   \
        }                                                                       \
    }    

struct trie
{
    char prefix;
    void* value;
    struct trie* parent;
    struct trie* children;

    // Used for linking children together in a linked list.
    struct trie* next;
};

// Create a new trie.
struct trie* trie_new();

// Add a new entry to the trie. Returns false if the key already exists, 
// or on failure.
bool trie_add(struct trie* trie, const char* key, void* value);

// Search for an entry in the trie. Returns NULL if the value is not found,
// or on failure.
void* trie_find(struct trie* trie, const char* key);

// Delete an entry in the trie. Returns false if the key is not found,
// or on failure.
bool trie_delete(struct trie* trie, const char* key);

// Delete a trie.
void trie_free(struct trie* trie);