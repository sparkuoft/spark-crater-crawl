#ifndef CONSOLE_H
#define CONSOLE_H

#include <stdint.h>

/* Test console: typed commands standing in for hardware that doesn't exist
 * yet. The driver (main.cpp) reads whole lines, handles +<ms> and q itself,
 * and passes everything else here.
 *
 *   s          start a game
 *   r          reset to IDLE
 *   h<p> [n]   fake a hit on player p by pillar n (default 0): "h0", "h1 3".
 *              Only counts if pillar n is EXTENDED, so raise it with w<n> first
 *   w<n>       warn + raise pillar n now, at the current difficulty. RUNNING
 *              only, and refused once MAX_SIMULTANEOUS_PILLAR_MOVES pillars are
 *              active (the same limit the sequencer keeps)
 *   ?          print current state
 *
 * console_tick also reports every pillar phase change (read-only), so pillars
 * coming down after a round can be watched. */
void console_init(uint32_t now_ms);
void console_handle_line(const char *line, uint32_t now_ms);
void console_tick(uint32_t now_ms); /* call every loop */

#endif
