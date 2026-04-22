// floason (C) 2026
// Licensed under the MIT License.

#pragma once

#include <stdatomic.h>
#include <threads.h>

#include "trie.h"
#include "bulb_macros.h"
#include "bulb_structs.h"
#include "console_io.h"

#define COLOR_LENGTH                        11 // \x1B[38:5:###m
#define COLOR_WHITE                         "\x1B[38:5:15m"
#define COLOR_LIGHT_CYAN                    "\x1B[38:5:159m"
#define COLOR_RED                           "\x1B[38:5:1m"
#define COLOR_GREEN                         "\x1B[38:5:2m"
#define COLOR_DEFAULT                       "\x1B[39m"

#define BULB_USE_DEFAULT_PORT               0

extern char input_buffer[MAX_MESSAGE_LENGTH + 1];
extern int input_buffer_w;
extern bool waiting_for_input;
extern bool echo_input;

// <SEQ>NAME<SEQ>\0
extern char name_buffer[COLOR_LENGTH + MAX_NAME_LENGTH + COLOR_LENGTH + 1];

extern mtx_t print_message_lock;
extern atomic_bool print_message_lock_busy;

extern struct trie* cli_cmds;
extern struct bulb_userinfo userinfo;

static inline void cleanup()
{
    if (cli_cmds != NULL)
        trie_free(cli_cmds);
    disable_console_io_functions();
}

void clear_input_buffer_on_screen();

void print_message(const char* message);