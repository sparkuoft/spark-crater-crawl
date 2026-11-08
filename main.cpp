/*
 * Crater Crawl -- laptop test driver (plain C++, no Arduino)
 * Spark Design Club, 2026-2027
 *
 * Stands in for the Mega's main loop while we test on a computer, and owns
 * g_state. Each simulated millisecond runs one loop pass (game_fsm_update +
 * console_tick), the same as loop() would.
 *
 *   ./crater_crawl          script mode: time only moves on +<ms>, so a round
 *                           runs instantly and the same input always gives
 *                           the same output
 *   ./crater_crawl --live   live mode: time runs in real time and commands
 *                           apply as you type them; +<ms> waits that long
 *                           before the next command
 *
 * Either mode can take a script, e.g. the design review demo:
 *   ./crater_crawl < demo/fall_design_review.txt
 *   ./crater_crawl --live < demo/fall_design_review.txt
 * A script's commands land on the same simulated milliseconds in both modes,
 * so the live run plays the same round, just at real speed.
 *
 * Build:
 *   clang++ -std=c++11 -Wall -Wextra -pthread -o crater_crawl *.cpp
 */

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <atomic>
#include <chrono>
#include <deque>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include "shared_state.h"
#include "game_fsm.h"
#include "console.h"
#include "debug_log.h"

#define MAX_ADVANCE_MS 600000UL /* cap one +<ms> step at 10 min of simulated time */

SharedState g_state;

static uint32_t s_now_ms = 0;
static uint32_t s_resume_ms = 0; // after +<ms>, the next command waits until this time

// Live mode reads input on its own thread so the game keeps running while it
// waits for you to type.
static std::mutex s_input_mutex;
static std::deque<std::string> s_input;
static std::atomic<bool> s_input_done(false);

// One pass of what loop() does on the Mega. console_tick goes last so changes
// made by this update are reported at the same millisecond.
static void run_loop_once()
{
    game_fsm_update(s_now_ms);
    console_tick(s_now_ms);
}

static void tick()
{
    s_now_ms++;
    run_loop_once();
}

static bool ready_for_input()
{
    return (int32_t)(s_now_ms - s_resume_ms) >= 0;
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

// Applies one line of input. Returns false when it's time to quit.
static bool handle_line(std::string line)
{
    trim(line);
    if (line.empty())
        return true;

    // Comments are printed as-is, so demo scripts can caption what's happening.
    if (line[0] == '#')
    {
        log_line(s_now_ms, "%s", line.c_str());
        return true;
    }

    // Echoed with the time it's applied, so piped scripts read like a log.
    log_line(s_now_ms, "> %s", line.c_str());

    if (line == "q")
        return false;

    if (line[0] == '+')
    {
        char *end = nullptr;
        unsigned long ms = strtoul(line.c_str() + 1, &end, 10);
        if (!isdigit((unsigned char)line[1]) || *end != '\0' || ms > MAX_ADVANCE_MS)
        {
            log_line(s_now_ms, "usage: +<ms>, up to %lu", MAX_ADVANCE_MS);
            return true;
        }
        s_resume_ms = s_now_ms + (uint32_t)ms;
        return true;
    }

    console_handle_line(line.c_str(), s_now_ms);
    run_loop_once(); // same loop pass as the command, like on the Mega
    return true;
}

static void run_script_mode()
{
    std::string line;
    for (;;)
    {
        while (!ready_for_input())
            tick();

        fflush(stdout);
        if (!std::getline(std::cin, line))
            return;
        if (!handle_line(line))
            return;
    }
}

static void read_input_forever()
{
    std::string line;
    while (std::getline(std::cin, line))
    {
        std::lock_guard<std::mutex> lock(s_input_mutex);
        s_input.push_back(line);
    }
    s_input_done = true;
}

static bool next_line(std::string &line)
{
    std::lock_guard<std::mutex> lock(s_input_mutex);
    if (s_input.empty())
        return false;
    line = s_input.front();
    s_input.pop_front();
    return true;
}

static bool input_pending()
{
    std::lock_guard<std::mutex> lock(s_input_mutex);
    return !s_input.empty();
}

// Input has ended (end of a piped script, or Ctrl-D) and every line is used up.
static bool input_finished()
{
    return s_input_done && !input_pending();
}

static void run_live_mode()
{
    std::thread(read_input_forever).detach();
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();

    for (;;)
    {
        std::string line;
        while (ready_for_input() && next_line(line))
        {
            if (!handle_line(line))
                return;
        }
        if (ready_for_input() && input_finished())
            return;

        // Catch the simulated clock up to real time, one loop pass per ms. Stop
        // early when a +<ms> wait ends so the next command lands on exactly the
        // same millisecond as it would in script mode.
        uint32_t real_ms = (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - start)
                               .count();
        while ((int32_t)(real_ms - s_now_ms) > 0)
        {
            tick();
            if (s_now_ms == s_resume_ms && input_pending())
                break;
        }

        fflush(stdout);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

int main(int argc, char **argv)
{
    bool live = false;
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--live") == 0)
        {
            live = true;
        }
        else
        {
            fprintf(stderr, "usage: %s [--live]\n", argv[0]);
            return 1;
        }
    }

    game_fsm_init(s_now_ms);
    console_init(s_now_ms);
    if (live)
        log_line(s_now_ms, "live mode: time runs for real. Type h0 or h1 while a pillar is up (#)");
    else
        log_line(s_now_ms, "script mode: time only moves on +<ms>");

    if (live)
    {
        run_live_mode();
        // The input thread may still be blocked reading; exit without waiting on it.
        fflush(stdout);
        _Exit(0);
    }

    run_script_mode();
    return 0;
}
