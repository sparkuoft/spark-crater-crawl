#include <Arduino.h>
#include "debug_log.h"

void log_stamp(uint32_t now_ms)
{
    Serial.print('[');
    Serial.print(now_ms);
    Serial.print(F("] "));
}

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
