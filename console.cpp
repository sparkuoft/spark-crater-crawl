#include <ctype.h>
#include "console.h"
#include "game_fsm.h"
#include "pillar_fsm.h"
#include "debug_log.h"

static PillarPhase s_last_pillar_phase[NUM_PILLARS];

static void print_help()
{
    log_text("commands:\n"
             "  s         start game\n"
             "  r         reset to IDLE\n"
             "  h<p> [n]  fake hit on player p by pillar n (default 0), e.g. h0, h1 3\n"
             "            (only counts if pillar n is EXTENDED: raise it with w<n> first)\n"
             "  w<n>      warn + raise pillar n now (RUNNING only, at most %d active pillars,\n"
             "            same limit as the sequencer)\n"
             "  ?         print state\n"
             "  +<ms>     advance simulated time, e.g. +3000\n"
             "  q         quit\n",
             MAX_SIMULTANEOUS_PILLAR_MOVES);
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
        CollisionEvent ev;
        ev.player_id = a;
        ev.pillar_id = parse_uint8(args, b) ? b : 0;
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

// Read-only: reports pillar_fsm transitions, never writes pillar state.
static void watch_pillars(uint32_t now_ms)
{
    for (uint8_t i = 0; i < NUM_PILLARS; i++)
    {
        PillarPhase p = g_state.pillars[i].phase;
        if (p == s_last_pillar_phase[i])
            continue;

        log_line(now_ms, "  pillar %u %s -> %s", (unsigned)i, pillar_phase_name(s_last_pillar_phase[i]), pillar_phase_name(p));
        s_last_pillar_phase[i] = p;
    }
}

void console_init(uint32_t now_ms)
{
    for (uint8_t i = 0; i < NUM_PILLARS; i++)
        s_last_pillar_phase[i] = g_state.pillars[i].phase;

    log_line(now_ms, "console ready");
    print_help();
}

void console_tick(uint32_t now_ms)
{
    watch_pillars(now_ms);
}
