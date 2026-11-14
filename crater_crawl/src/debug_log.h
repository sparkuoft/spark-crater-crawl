#ifndef DEBUG_LOG_H
#define DEBUG_LOG_H

#include <stdint.h>
#include "shared_state.h"

/* Debug output shared by the game FSM and the test console.
 * Plain stdio for now while we test on a laptop. When moving to the Mega,
 * debug_log.cpp is the only output code that needs to change. */

#if defined(__GNUC__)
#define LOG_PRINTF_FMT(fmt_idx, args_idx) __attribute__((format(printf, fmt_idx, args_idx)))
#else
#define LOG_PRINTF_FMT(fmt_idx, args_idx)
#endif

/* One timestamped line: "[<now_ms>] <message>\n". */
void log_line(uint32_t now_ms, const char *fmt, ...) LOG_PRINTF_FMT(2, 3);

/* Raw text, no timestamp or newline added. */
void log_text(const char *fmt, ...) LOG_PRINTF_FMT(1, 2);

const char *game_phase_name(GamePhase p);
const char *pillar_phase_name(PillarPhase p);
const char *warning_mode_name(WarningMode m);

#endif
