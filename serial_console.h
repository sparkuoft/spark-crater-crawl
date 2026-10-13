#ifndef SERIAL_CONSOLE_H
#define SERIAL_CONSOLE_H

#include <stdint.h>

/* Bench-test console over Serial, standing in for hardware that doesn't exist
 * yet. Line-based (send with a newline), non-blocking.
 *
 *   s          start a game
 *   r          reset to IDLE
 *   h<p> [n]   fake a hit on player p by pillar n (default 0): "h0", "h1 3"
 *   w<n>       warn + raise pillar n at the current difficulty. RUNNING only,
 *              stands in for the sequencer while it's a stub
 *   ?          print current state
 *
 * Also reports every pillar phase change (read-only), so pillars coming down
 * after a round can be watched. */
void serial_console_init(uint32_t now_ms);
void serial_console_poll(uint32_t now_ms);

#endif
