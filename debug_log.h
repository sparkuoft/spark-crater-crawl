#ifndef DEBUG_LOG_H
#define DEBUG_LOG_H

#include <stdint.h>
#include "shared_state.h"

/* Serial debug helpers shared by the game FSM and the serial console. */

/* Prints "[<now_ms>] " at the start of a log line. */
void log_stamp(uint32_t now_ms);

const char *game_phase_name(GamePhase p);
const char *pillar_phase_name(PillarPhase p);
const char *warning_mode_name(WarningMode m);

#endif
