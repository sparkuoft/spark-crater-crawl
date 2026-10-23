#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include "console.h"
#include "game_fsm.h"
#include "pillar_fsm.h"
#include "debug_log.h"

#define ARENA_LINE_MAX 160

// What the demo view last showed. Read-only copies; the console never writes
// game or pillar state.
static PillarPhase s_last_pillar_phase[NUM_PILLARS];
static bool s_last_alive[NUM_PLAYERS];
static bool s_last_locked[NUM_PLAYERS];
static GamePhase s_last_phase = PHASE_IDLE;
static int s_last_countdown = -1;
static uint32_t s_round_start_ms = 0;
static uint32_t s_round_end_ms = 0;
static unsigned s_pillars_raised = 0;

static void print_help()
{
    log_text("commands:\n"
             "  s         start game\n"
             "  r         reset to IDLE\n"
             "  h<p> [n]  fake a Tracking hit on player p by pillar n, e.g. h1 3\n"
             "            (no n: uses a pillar that's up. Only EXTENDED pillars count)\n"
             "  w<n>      warn + raise pillar n now (RUNNING only, at most %d active pillars,\n"
             "            same limit as the sequencer)\n"
             "  ?         print state\n"
             "  +<ms>     advance simulated time, e.g. +3000 (live mode: wait that long)\n"
             "  # text    comment, printed as-is (for demo scripts)\n"
             "  q         quit\n",
             MAX_SIMULTANEOUS_PILLAR_MOVES);
}

// First EXTENDED pillar, or 0 if none is up (the hit is then ignored).
static uint8_t raised_pillar()
{
    for (uint8_t i = 0; i < NUM_PILLARS; i++)
    {
        if (g_state.pillars[i].phase == PILLAR_EXTENDED)
            return i;
    }
    return 0;
}

// Pillars out of IDLE. The sequencer keeps this below the motion cap so no
// pillar ever waits for a motor slot; manual warnings follow the same rule.
static uint8_t active_pillars()
{
    uint8_t n = 0;
    for (uint8_t i = 0; i < NUM_PILLARS; i++)
    {
        if (g_state.pillars[i].phase != PILLAR_IDLE)
            n++;
    }
    return n;
}

// Reads an unsigned number at s (skipping leading spaces) and advances s past it.
static bool parse_uint8(const char *&s, uint8_t &out)
{
    while (*s == ' ')
        s++;
    if (*s < '0' || *s > '9')
        return false;

    uint16_t v = 0;
    while (*s >= '0' && *s <= '9')
    {
        v = (uint16_t)(v * 10 + (uint16_t)(*s - '0'));
        if (v > 255)
            return false;
        s++;
    }
    out = (uint8_t)v;
    return true;
}

static void print_status(uint32_t now_ms)
{
    log_line(now_ms, "STATUS phase=%s for %lu ms, difficulty=%s",
             game_phase_name(g_state.phase),
             (unsigned long)(now_ms - g_state.phase_entered_ms),
             warning_mode_name(game_difficulty()));

    for (uint8_t i = 0; i < NUM_PLAYERS; i++)
    {
        const PlayerState &pl = g_state.players[i];
        log_text("  P%u %s", (unsigned)i, pl.alive ? "alive" : "eliminated");
        if (pl.locked_out)
            log_text(", locked out since %lu", (unsigned long)pl.lockout_started_ms);
        log_text("\n");
    }

    log_text("  pillars:");
    for (uint8_t i = 0; i < NUM_PILLARS; i++)
        log_text(" %u:%s", (unsigned)i, pillar_phase_name(g_state.pillars[i].phase));
    log_text("\n");
}

void console_handle_line(const char *line, uint32_t now_ms)
{
    const char *args = line + 1;
    uint8_t a = 0;
    uint8_t b = 0;

    switch (tolower((unsigned char)line[0]))
    {
    case 's':
        game_start(now_ms);
        break;

    case 'r':
        game_reset(now_ms);
        break;

    case 'h':
    {
        if (!parse_uint8(args, a))
        {
            print_help();
            break;
        }
        // Stands in for Tracking's report. Without a pillar number, use one that's
        // up: a pillar only stays EXTENDED for about a second, too short to read
        // its number and type it in live mode.
        CollisionEvent ev;
        ev.player_id = a;
        ev.pillar_id = parse_uint8(args, b) ? b : raised_pillar();
        ev.hit = true;
        ev.timestamp_ms = now_ms;
        game_on_collision(ev, now_ms);
        break;
    }

    case 'w':
        if (!parse_uint8(args, a))
        {
            print_help();
            break;
        }
        if (g_state.phase != PHASE_RUNNING)
        {
            log_line(now_ms, "warn ignored: not RUNNING");
            break;
        }
        if (active_pillars() >= MAX_SIMULTANEOUS_PILLAR_MOVES)
        {
            log_line(now_ms, "warn ignored: %u pillars already active", (unsigned)active_pillars());
            break;
        }
        if (!pillarStartWarning(a, game_difficulty(), false, now_ms))
        {
            log_line(now_ms, "warn ignored: bad pillar id or pillar not IDLE");
        }
        break;

    case '?':
        print_status(now_ms);
        break;

    default:
        print_help();
        break;
    }
}

/* ---- Demo view --------------------------------------------------------- */

static char pillar_symbol(PillarPhase p)
{
    switch (p)
    {
    case PILLAR_IDLE:     return '.';
    case PILLAR_WARNING:  return '!';
    case PILLAR_RISING:   return '^';
    case PILLAR_EXTENDED: return '#';
    case PILLAR_LOWERING: return 'v';
    }
    return '?';
}

static const char *player_status(const PlayerState &pl)
{
    if (!pl.alive)
        return "OUT";
    return pl.locked_out ? "LOCKED" : "ok";
}

// Time into the round; frozen once the round ends.
static uint32_t round_time_ms(uint32_t now_ms)
{
    if (g_state.phase == PHASE_RUNNING)
        return now_ms - s_round_start_ms;
    return s_round_end_ms - s_round_start_ms;
}

// Appends to a fixed buffer without ever writing past its end.
static void appendf(char *buf, size_t size, size_t *len, const char *fmt, ...) LOG_PRINTF_FMT(4, 5);
static void appendf(char *buf, size_t size, size_t *len, const char *fmt, ...)
{
    if (*len >= size - 1)
        return;

    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf + *len, size - *len, fmt, args);
    va_end(args);

    if (n > 0)
        *len = (*len + (size_t)n < size) ? *len + (size_t)n : size - 1;
}

// The whole arena on one line. Pillars that just changed are in brackets:
//   [41200]   t=38.2s NORMAL | P0 ok     P1 LOCKED | 0:.  1:! (2:^) 3:#  4:v  5:.
static void print_arena(uint32_t now_ms, const bool changed[NUM_PILLARS])
{
    char buf[ARENA_LINE_MAX];
    size_t len = 0;
    uint32_t t = round_time_ms(now_ms);

    appendf(buf, sizeof(buf), &len, "  t=%2lu.%lus %-6s |",
            (unsigned long)(t / 1000), (unsigned long)(t % 1000 / 100),
            warning_mode_name(game_difficulty()));
    for (uint8_t i = 0; i < NUM_PLAYERS; i++)
        appendf(buf, sizeof(buf), &len, " P%u %-6s", (unsigned)i, player_status(g_state.players[i]));
    appendf(buf, sizeof(buf), &len, " |");
    for (uint8_t i = 0; i < NUM_PILLARS; i++)
    {
        char sym = pillar_symbol(g_state.pillars[i].phase);
        if (changed[i])
            appendf(buf, sizeof(buf), &len, " (%u:%c)", (unsigned)i, sym);
        else
            appendf(buf, sizeof(buf), &len, "  %u:%c ", (unsigned)i, sym);
    }

    log_line(now_ms, "%s", buf);
}

static void print_summary(uint32_t now_ms)
{
    uint32_t t = s_round_end_ms - s_round_start_ms;
    unsigned alive_count = 0;
    unsigned survivor = 0;
    for (uint8_t i = 0; i < NUM_PLAYERS; i++)
    {
        if (g_state.players[i].alive)
        {
            alive_count++;
            survivor = i;
        }
    }

    log_line(now_ms, "=== GAME OVER ===");
    if (alive_count == 1)
        log_line(now_ms, "  P%u wins after %lu.%lus, reached %s, %u pillars raised", survivor,
                 (unsigned long)(t / 1000), (unsigned long)(t % 1000 / 100),
                 warning_mode_name(game_difficulty()), s_pillars_raised);
    else
        log_line(now_ms, "  draw after %lu.%lus, reached %s, %u pillars raised",
                 (unsigned long)(t / 1000), (unsigned long)(t % 1000 / 100),
                 warning_mode_name(game_difficulty()), s_pillars_raised);
    log_line(now_ms, "  type r to reset, then s to play again");
}

// Banners on phase changes, plus the 3-2-1 countdown.
static void watch_phase(uint32_t now_ms)
{
    GamePhase phase = g_state.phase;

    if (phase != s_last_phase)
    {
        if (s_last_phase == PHASE_RUNNING)
            s_round_end_ms = now_ms;

        switch (phase)
        {
        case PHASE_IDLE:
            log_line(now_ms, "ready: type s to start");
            break;

        case PHASE_COUNTDOWN:
            s_round_start_ms = now_ms;
            s_round_end_ms = now_ms;
            s_pillars_raised = 0;
            s_last_countdown = -1;
            log_line(now_ms, "=== GET READY ===");
            break;

        case PHASE_RUNNING:
        {
            s_round_start_ms = now_ms;
            log_line(now_ms, "=== GO! ===   pillars: . down  ! warning  ^ rising  # up  v lowering");
            bool none[NUM_PILLARS] = {false};
            print_arena(now_ms, none);
            break;
        }

        case PHASE_PLAYER_ELIMINATED:
            break;

        case PHASE_GAME_OVER:
            print_summary(now_ms);
            break;
        }
        s_last_phase = phase;
    }

    if (phase == PHASE_COUNTDOWN)
    {
        uint32_t in_phase_ms = now_ms - g_state.phase_entered_ms;
        if (in_phase_ms < COUNTDOWN_MS)
        {
            int left = (int)((COUNTDOWN_MS - in_phase_ms + 999) / 1000);
            if (left != s_last_countdown)
            {
                log_line(now_ms, "  %d...", left);
                s_last_countdown = left;
            }
        }
    }
}

// Prints the arena whenever a pillar or a player changes.
static void watch_arena(uint32_t now_ms)
{
    bool changed[NUM_PILLARS];
    bool any = false;

    for (uint8_t i = 0; i < NUM_PILLARS; i++)
    {
        PillarPhase p = g_state.pillars[i].phase;
        changed[i] = (p != s_last_pillar_phase[i]);
        if (changed[i])
        {
            any = true;
            if (p == PILLAR_RISING)
                s_pillars_raised++;
            s_last_pillar_phase[i] = p;
        }
    }

    bool eliminated[NUM_PLAYERS];
    for (uint8_t i = 0; i < NUM_PLAYERS; i++)
    {
        const PlayerState &pl = g_state.players[i];
        eliminated[i] = s_last_alive[i] && !pl.alive;
        if (pl.alive != s_last_alive[i] || pl.locked_out != s_last_locked[i])
        {
            any = true;
            s_last_alive[i] = pl.alive;
            s_last_locked[i] = pl.locked_out;
        }
    }

    if (any)
        print_arena(now_ms, changed);

    for (uint8_t i = 0; i < NUM_PLAYERS; i++)
    {
        if (eliminated[i])
            log_line(now_ms, "  *** P%u ELIMINATED ***", (unsigned)i);
    }
}

void console_init(uint32_t now_ms)
{
    for (uint8_t i = 0; i < NUM_PILLARS; i++)
        s_last_pillar_phase[i] = g_state.pillars[i].phase;
    for (uint8_t i = 0; i < NUM_PLAYERS; i++)
    {
        s_last_alive[i] = g_state.players[i].alive;
        s_last_locked[i] = g_state.players[i].locked_out;
    }
    s_last_phase = g_state.phase;

    log_line(now_ms, "console ready");
    print_help();
    log_line(now_ms, "ready: type s to start");
}

void console_tick(uint32_t now_ms)
{
    watch_phase(now_ms);
    watch_arena(now_ms);
}
