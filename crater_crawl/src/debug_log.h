#ifndef DEBUG_LOG_H
#define DEBUG_LOG_H

#include <stdint.h>
#include "shared_state.h"

/* Debug output shared by the game FSM and the test console. Goes to Serial
 * on the Mega and to stdout in the laptop build. Every print in the game
 * goes through here, so this is the one place to quiet them for timing. */

#if defined(__GNUC__)
#define LOG_PRINTF_FMT(fmt_idx, args_idx) __attribute__((format(printf, fmt_idx, args_idx)))
#else
#define LOG_PRINTF_FMT(fmt_idx, args_idx)
#endif

/* One timestamped line: "[<now_ms>] <message>\n". */
void log_line(uint32_t now_ms, const char *fmt, ...) LOG_PRINTF_FMT(2, 3);

/* Raw text, no timestamp or newline added. */
void log_text(const char *fmt, ...) LOG_PRINTF_FMT(1, 2);

/* Turns every game print on or off (on by default). Off is for timing tests on
 * the board, where Serial prints slow the loop down, and for the unit tests. */
void log_set_enabled(bool enabled);
bool log_is_enabled();

const char *game_phase_name(GamePhase p);
const char *pillar_phase_name(PillarPhase p);
const char *warning_mode_name(WarningMode m);

#endif
