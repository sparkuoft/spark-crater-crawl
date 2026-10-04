#ifndef SHARED_STATE_H
#define SHARED_STATE_H

/*
 * Crater Crawl -- shared interface contract
 * Spark Design Club, 2026-2027
 *
 * Owner: Antoine Tabet (Game Logic / GL|E)
 * Last updated: 2026-08-11
 *
 * ONE WRITER PER FIELD. Every field below is tagged with the subteam that
 * writes it; everyone else reads only. If you need to write a field you don't
 * own, that's an interface change -- talk to GL first, don't just write it.
 *
 * Target: single Arduino Mega 2560 (decision provisional pending P|E pin count).
 */

#include <stdint.h>

/* ======================================================================
 * CONFIG CONSTANTS
 *
 * Values marked PROVISIONAL come from first-round prototyping and will move.
 * The sequencer reads these at runtime
 * a motor or mechanism change is a one-line edit here.
 * ====================================================================== */

/* Pillar count and indexing.
 * LOCKED 2026-08-02 with Sakanan (P|E).
 * Pillar 0 is nearest Player 1's start position, counting counter-clockwise. */
#define NUM_PILLARS 6
#define NUM_PLAYERS 2

/* Angular encoding. Absolute degrees, single shared reference frame for both
 * players (LOCKED with Anthony, T|E). Stored as tenths of a degree:
 * 0..3599. Resolution 0.1 deg, comfortably inside AS5600's 12-bit (0.088 deg). */
#define ANGLE_MAX_X10 3600

/* Post-hit lockout: how long a player stays frozen after being hit.
 * 1.2 s END-TO-END (command sent -> servo extended -> 1 s hold -> retracted),
 * measured by Alice (W|E), Aug 2026. Confirmed with her 2026-08-11 after an
 * initial ambiguity over whether the figure included the hold.
 * If this ever disagrees with observed behaviour at integration, re-measure
 * end-to-end before changing it -- do not adjust to make a symptom go away. */
#define POST_HIT_LOCKOUT_MS 1200

/* Pillar motion timing. PROVISIONAL -- Sakanan (P|E), Aug 2026.
 * Measured on a worm-gear DC motor + belt-driven linear actuator, 42 cm travel,
 * WITHOUT pillar weight. Loaded times will be slower, or equal at higher power.
 * Rise and lower are currently about equal; to be re-measured under load.
 * Pillars has significant prototyping left and may change motor OR mechanism,
 * so treat both as estimates until re-confirmed. */
#define PILLAR_RISE_MS 1700  /* PROVISIONAL */
#define PILLAR_LOWER_MS 1700 /* PROVISIONAL */

/* Simultaneous-motion cap for the sequencer.
 * PROVISIONAL -- derived from a ~28 W target Sarah gave for pillar motors, at
 * ~9 W per moving motor. Sakanan notes this is a prototyping-stage assumption,
 * not a measured electrical ceiling. The sequencer MUST respect it regardless:
 * difficulty scaling may never command a 4th pillar into motion. */
#define MAX_SIMULTANEOUS_PILLAR_MOVES 3 /* PROVISIONAL */

/* ======================================================================
 * ENUMS
 * ====================================================================== */

/* Top-level game FSM. Written by GL. */
enum GamePhase : uint8_t
{
    PHASE_IDLE = 0,
    PHASE_COUNTDOWN,
    PHASE_RUNNING,
    PHASE_PLAYER_ELIMINATED,
    PHASE_GAME_OVER
};

/* Per-pillar FSM. Six instances run in parallel. Written by GL. */
enum PillarPhase : uint8_t
{
    PILLAR_IDLE = 0,
    PILLAR_WARNING,
    PILLAR_RISING,
    PILLAR_EXTENDED,
    PILLAR_LOWERING
};

/* Action sent to Pillars. GL owns the timing: the command is issued at the
 * exact moment the action should begin, so there is deliberately NO delay
 * field. This keeps cancel-on-elimination logic on the GL side. */
enum PillarAction : uint8_t
{
    ACTION_NONE = 0,
    ACTION_RAISE,
    ACTION_LOWER,
    ACTION_WARN_ONLY /* LED warning with no rise behind it -- see fake warnings */
};

/* LED warning difficulty mode.
 * GL owns WHEN the mode flips (tracked against game time) and sends which mode
 * applies. Pillars owns the preset animation for each mode. LOCKED 2026-08-02. */
enum WarningMode : uint8_t
{
    WARN_EASY = 0,
    WARN_NORMAL,
    WARN_HARD
};

/* ======================================================================
 * STRUCTS
 * ====================================================================== */

/* Player position and status.
 * angle_x10  -- written by TRACKING (T|E)
 * everything else -- written by GL
 *
 * OPEN: Alice (W|E) lists 2x AS5600 on the character ring, and Anthony's
 * tracking doc also lists magnetic encoders for character position. These may
 * be the same two sensors. Owner of the read path is unresolved as of
 * 2026-08-11 -- GL must receive the angle from exactly one source. */
struct PlayerState
{
    uint16_t angle_x10;          /* T|E. Absolute, shared reference, 0..3599 */
    bool alive;                  /* GL */
    bool locked_out;             /* GL. True while POST_HIT_LOCKOUT_MS is running */
    uint32_t lockout_started_ms; /* GL. millis() at hit */
};

/* Per-pillar state.
 * phase, warn_mode  -- written by GL
 * at_bottom/at_top  -- written by PILLARS (P|E)
 *
 * Position feedback rule (LOCKED 2026-08-02): on IDLE and EXTENDED these are
 * REAL limit-switch readings. On RISING and LOWERING they are echoed from the
 * commanded state -- GL must not trust them mid-motion. Collision detection
 * only ever tests EXTENDED, where the reading is real. */
struct PillarState
{
    PillarPhase phase;         /* GL */
    WarningMode warn_mode;     /* GL */
    bool at_bottom;            /* P|E. Real on IDLE, echoed on RISING/LOWERING */
    bool at_top;               /* P|E. Real on EXTENDED, echoed on RISING/LOWERING */
    uint32_t phase_entered_ms; /* GL */
};

/* Command from GL to Pillars. LED is bundled -- one command, not two.
 * Written by GL, read by P|E.
 *
 * Fake warnings ("ankle breakers", Sakanan's term): send ACTION_WARN_ONLY with
 * is_fake set. The LED animation plays and no pillar rises. Implementation is
 * GL's call; Pillars just plays the preset. Intended for WARN_HARD only. */
struct PillarCommand
{
    uint8_t pillar_id; /* 0..NUM_PILLARS-1 */
    PillarAction action;
    WarningMode warn_mode;
    bool is_fake;
};

/* Collision report from Tracking to GL. LOCKED with Anthony (T|E).
 * Written by T|E, read by GL. */
struct CollisionEvent
{
    uint8_t player_id; /* 0..NUM_PLAYERS-1 */
    uint8_t pillar_id; /* 0..NUM_PILLARS-1 */
    bool hit;
    uint32_t timestamp_ms;
};

/* Hit trigger from GL to Wheels. LOCKED with Alice (W|E).
 * Deliberately carries player_id ONLY -- Wheels does not need to know which
 * pillar was involved, and the payload is identical whether the animation ends
 * up being servo or the LED fallback.
 * Written by GL, read by W|E. */
struct WheelsTrigger
{
    uint8_t player_id;
    bool fatal; /* false = hit (retracts), true = death (holds) */
    uint32_t timestamp_ms;
};

/* ======================================================================
 * GLOBAL SHARED STATE
 * ====================================================================== */

struct SharedState
{
    GamePhase phase;           /* GL */
    uint32_t phase_entered_ms; /* GL */
    PlayerState players[NUM_PLAYERS];
    PillarState pillars[NUM_PILLARS];
};

extern SharedState g_state;

#endif /* SHARED_STATE_H */
