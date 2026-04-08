// floason (C) 2026
// Licensed under the MIT License.

#include <stdbool.h>

#include "trie.h"
#include "util.h"

// Internal function for searching for a trie node.
static struct trie* _trie_find(struct trie* trie, const char* key)
{
    ASSERT(trie, return NULL);

    int len = strlen(key);
    ASSERT(len > 0, return NULL);

    struct trie* child = trie->children;
    for (int i = 0; i < len; ++i)
    {
        while (child)
        {
            if (child->prefix == key[i])
            {
                if (i + 1 == len)
                    return child;
                else
                {
                    child = child->children;
                    break;
                }
            }
            else
                child = child->next;
        }
    }

    return (child && child->value != NULL) ? child : NULL;
}

// Create a new trie.
struct trie* trie_new()
{
    return (struct trie*)tagged_malloc(sizeof(struct trie), TAG_TRIE);
}

// Add a new entry to the trie. Returns false if the key already exists, 
// or on failure.
bool trie_add(struct trie* trie, const char* key, void* value)
{
    ASSERT(trie, return false);

    int len = strlen(key);
    ASSERT(len > 0, return false);

    struct trie* parent = trie;
    struct trie* child = parent->children;
    for (int i = 0; i < len; i++)
    {
        if (child != NULL)
        {
            for (;;)
            {
                if (child->prefix == key[i])
                    goto next_index;
                else if (child->next != NULL)
                    child = child->next;
                else
                    break;
            }
            child = child->next = trie_new();
        }
        else
            child = parent->children = trie_new();
        child->prefix = key[i];
        child->parent = parent;

next_index:
        parent = child;
        child = child->children;
    }

finish:
    if (parent->value != NULL)
        return false;
    parent->value = value;
    return true;
}

// Search for an entry in the trie. Returns NULL if the key is not found,
// or on failure.
void* trie_find(struct trie* trie, const char* key)
{
    struct trie* node = _trie_find(trie, key);
    return node ? node->value : NULL;
}

// Delete an entry in the trie. Returns false if the key is not found,
// or on failure.
bool trie_delete(struct trie* trie, const char* key)
{
    struct trie* node = _trie_find(trie, key);
    if (node == NULL)
        return false;

    if (node->children == NULL)
    {
        node->parent->children = NULL;
        tagged_free(node, TAG_TRIE);
    }
    else
        node->value = NULL;

    return true;
}

// Delete a trie.
void trie_free(struct trie* trie)
{
    if (trie->next)
        trie_free(trie->next);
    if (trie->children)
        trie_free(trie->children);
    tagged_free(trie, TAG_TRIE);
}