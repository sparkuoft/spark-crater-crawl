#include <Arduino.h>
#include <ctype.h>
#include "serial_console.h"
#include "game_fsm.h"
#include "pillar_fsm.h"
#include "debug_log.h"

#define CONSOLE_LINE_MAX 24

static char s_line[CONSOLE_LINE_MAX];
static uint8_t s_len = 0;
static bool s_overflow = false;
static PillarPhase s_last_pillar_phase[NUM_PILLARS];

static void print_help()
{
    Serial.println(F("commands (end with newline):"));
    Serial.println(F("  s         start game"));
    Serial.println(F("  r         reset to IDLE"));
    Serial.println(F("  h<p> [n]  fake hit on player p by pillar n (default 0), e.g. h0, h1 3"));
    Serial.println(F("  w<n>      warn + raise pillar n (RUNNING only, stands in for the sequencer)"));
    Serial.println(F("  ?         print state"));
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
    log_stamp(now_ms);
    Serial.print(F("STATUS phase="));
    Serial.print(game_phase_name(g_state.phase));
    Serial.print(F(" for "));
    Serial.print(now_ms - g_state.phase_entered_ms);
    Serial.print(F(" ms, difficulty="));
    Serial.println(warning_mode_name(game_difficulty()));

    for (uint8_t i = 0; i < NUM_PLAYERS; i++)
    {
        const PlayerState &pl = g_state.players[i];
        Serial.print(F("  P"));
        Serial.print(i);
        Serial.print(pl.alive ? F(" alive") : F(" eliminated"));
        if (pl.locked_out)
        {
            Serial.print(F(", locked out since "));
            Serial.print(pl.lockout_started_ms);
        }
        Serial.println();
    }

    Serial.print(F("  pillars:"));
    for (uint8_t i = 0; i < NUM_PILLARS; i++)
    {
        Serial.print(' ');
        Serial.print(i);
        Serial.print(':');
        Serial.print(pillar_phase_name(g_state.pillars[i].phase));
    }
    Serial.println();
}

static void handle_line(const char *line, uint32_t now_ms)
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
            log_stamp(now_ms);
            Serial.println(F("warn ignored: not RUNNING"));
            break;
        }
        if (!pillarStartWarning(a, game_difficulty(), false, now_ms))
        {
            log_stamp(now_ms);
            Serial.println(F("warn ignored: bad pillar id or pillar not IDLE"));
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

        log_stamp(now_ms);
        Serial.print(F("  pillar "));
        Serial.print(i);
        Serial.print(' ');
        Serial.print(pillar_phase_name(s_last_pillar_phase[i]));
        Serial.print(F(" -> "));
        Serial.println(pillar_phase_name(p));
        s_last_pillar_phase[i] = p;
    }
}

void serial_console_init(uint32_t now_ms)
{
    for (uint8_t i = 0; i < NUM_PILLARS; i++)
        s_last_pillar_phase[i] = g_state.pillars[i].phase;

    log_stamp(now_ms);
    Serial.println(F("serial console ready"));
    print_help();
}

void serial_console_poll(uint32_t now_ms)
{
    // Only drains what's already buffered, so this never waits on input.
    while (Serial.available() > 0)
    {
        char c = (char)Serial.read();
        if (c == '\r' || c == '\n')
        {
            if (!s_overflow && s_len > 0)
            {
                s_line[s_len] = '\0';
                handle_line(s_line, now_ms);
            }
            s_len = 0;
            s_overflow = false;
        }
        else if (s_len < CONSOLE_LINE_MAX - 1)
        {
            s_line[s_len++] = c;
        }
        else
        {
            s_overflow = true; // drop the whole line rather than run a truncated command
        }
    }

    watch_pillars(now_ms);
}
