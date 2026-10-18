/*
 * Crater Crawl -- laptop test driver (plain C++, no Arduino)
 * Spark Design Club, 2026-2027
 *
 * Stands in for the Mega's main loop while we test on a computer, and owns
 * g_state. Time is simulated: it only moves when you type +<ms>, and each
 * simulated millisecond runs one loop pass (game_fsm_update + console_tick),
 * the same as loop() would. Runs are repeatable, so a test script can be
 * piped in:
 *
 *   clang++ -std=c++11 -Wall -Wextra -o crater_crawl *.cpp
 *   ./crater_crawl
 *   ./crater_crawl < my_test.txt
 *
 * Until the pillars branch is merged, pillar_fsm.h/.cpp have to be copied
 * into this folder to build (don't commit them here).
 */

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <string>
#include "shared_state.h"
#include "game_fsm.h"
#include "console.h"
#include "debug_log.h"

#define MAX_ADVANCE_MS 600000UL /* cap one +<ms> step at 10 min of simulated time */

SharedState g_state;

static uint32_t s_now_ms = 0;

// One pass of what loop() does on the Mega. console_tick goes last so pillar
// changes made by this update are reported at the same millisecond.
static void run_loop_once()
{
    game_fsm_update(s_now_ms);
    console_tick(s_now_ms);
}

static void advance(uint32_t ms)
{
    for (uint32_t i = 0; i < ms; i++)
    {
        s_now_ms++;
        run_loop_once();
    }
}

// Strips surrounding whitespace, including the \r from Windows line endings.
static void trim(std::string &s)
{
    while (!s.empty() && isspace((unsigned char)s.back()))
        s.pop_back();

    size_t start = 0;
    while (start < s.size() && isspace((unsigned char)s[start]))
        start++;
    s.erase(0, start);
}

int main()
{
    game_fsm_init(s_now_ms);
    console_init(s_now_ms);

    std::string line;
    for (;;)
    {
        fflush(stdout);
        if (!std::getline(std::cin, line))
            break;

        trim(line);
        if (line.empty())
            continue;

        // Echoed with the time it's applied, so piped scripts read like a log.
        log_line(s_now_ms, "> %s", line.c_str());

        if (line == "q")
            break;

        if (line[0] == '+')
        {
            char *end = nullptr;
            unsigned long ms = strtoul(line.c_str() + 1, &end, 10);
            if (!isdigit((unsigned char)line[1]) || *end != '\0' || ms > MAX_ADVANCE_MS)
            {
                log_line(s_now_ms, "usage: +<ms>, up to %lu", MAX_ADVANCE_MS);
                continue;
            }
            advance((uint32_t)ms);
            continue;
        }

        console_handle_line(line.c_str(), s_now_ms);
        run_loop_once(); // same loop pass as the command, like on the Mega
    }

    return 0;
}
