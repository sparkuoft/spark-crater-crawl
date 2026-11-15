#include <stdarg.h>
#include <stdio.h>
#include "debug_log.h"

#ifdef ARDUINO
#include <Arduino.h>

// Longest single log line. Anything longer is cut off, so keep each log_text
// call to one line of output (print_help in console.cpp does this).
#define LOG_BUF_SIZE 160

void log_line(uint32_t now_ms, const char *fmt, ...)
{
    char buf[LOG_BUF_SIZE];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    Serial.print('[');
    Serial.print((unsigned long)now_ms);
    Serial.print("] ");
    Serial.println(buf);
}

void log_text(const char *fmt, ...)
{
    char buf[LOG_BUF_SIZE];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    Serial.print(buf);
}

#else

void log_line(uint32_t now_ms, const char *fmt, ...)
{
    printf("[%lu] ", (unsigned long)now_ms);

    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);

    putchar('\n');
}

void log_text(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

#endif

const char *game_phase_name(GamePhase p)
{
    switch (p)
    {
    case PHASE_IDLE:              return "IDLE";
    case PHASE_COUNTDOWN:         return "COUNTDOWN";
    case PHASE_RUNNING:           return "RUNNING";
    case PHASE_PLAYER_ELIMINATED: return "PLAYER_ELIMINATED";
    case PHASE_GAME_OVER:         return "GAME_OVER";
    }
    return "?";
}

const char *pillar_phase_name(PillarPhase p)
{
    switch (p)
    {
    case PILLAR_IDLE:     return "IDLE";
    case PILLAR_WARNING:  return "WARNING";
    case PILLAR_RISING:   return "RISING";
    case PILLAR_EXTENDED: return "EXTENDED";
    case PILLAR_LOWERING: return "LOWERING";
    }
    return "?";
}

const char *warning_mode_name(WarningMode m)
{
    switch (m)
    {
    case WARN_EASY:   return "EASY";
    case WARN_NORMAL: return "NORMAL";
    case WARN_HARD:   return "HARD";
    }
    return "?";
}
