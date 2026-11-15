#ifndef CHARACTER_CONTROLLER_H
#define CHARACTER_CONTROLLER_H

#include "CharacterModule.h"

/* GAME LOGIC INTEGRATION GUIDE (adapter still to be implemented)
 *
 * Setup:
 *   1. Configure the hardware callbacks and initialize two player modules.
 *   2. Initialize one controller referencing those modules; attach reference
 *      interrupts, then request homing for each player.
 *   3. Continue updates while homing. Before starting a round, require usable
 *      calibrated positions; GL decides how to present/retry a homing failure.
 *      Existing game_start() does not perform this tracking readiness check.
 *
 * Proposed main-loop order:
 *   - Service encoder acquisition, then capture now_ms on the common clock.
 *   - Call character_controller_update(..., now_ms, pair_collisions_allowed).
 *   - Publish each valid angle via the single Tracking->g_state write path.
 *     Only angle_x10 belongs to Tracking; GL owns alive and locked_out.
 *   - Drain BOTH pillar queues into game_on_collision(ev, now_ms), preserving
 *     each hit_ms as ev.timestamp_ms. Deliver both before game_fsm_update()
 *     so the existing same-loop double-elimination/draw behavior is preserved.
 *   - Consume the pair event; apply the agreed movement policy if still eligible.
 *   - Advance game_fsm_update(now_ms).
 *
 * Integration decisions for GL/Wheels:
 *   - Define pair_collisions_allowed from phase/alive/lockout rules. A single
 *     bool gates the PAIR, not individual players or pillar switches.
 *   - Choose ONE owner of motor response. Default choose_response=NULL leaves
 *     response to GL after reading the event. This allows same-loop pillar
 *     hits to be processed first, then eligibility to be checked again.
 *     If callback response is enabled, apply_response runs INSIDE update,
 *     before the caller drains pillar events. Resolve that ordering explicitly
 *     so a pair response cannot undo a same-loop fatal hit or lockout command.
 *   - Decide the unknown-pillar ID representation (module uses -1, while the
 *     existing CollisionEvent expects an unsigned valid pillar index).
 *   - Define behavior for invalid/stale angles and recovery. SharedState has
 *     no angle-valid flag; keeping an old angle does not make it current.
 *   - Decide startup overlap, movement policy and thresholds through testing.
 *
 * Consume events promptly, even when ignoring them under game rules. Disabling
 * pair detection does NOT clear already queued pair or per-player events.
 * Drain/discard old notifications at round boundaries so they cannot affect
 * the next round. Check is_hit before using event fields.
 *
 * Both players must share one physical zero and positive direction. The
 * 'positive' enum means increasing angle; Wheels maps that to actual motion.
 * Keep these C files independent of GL headers; the C++ adapter can include
 * both this header and game_fsm.h. */
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CHARACTER_NEGATIVE = -1, /* Decreasing angle in the shared physical frame. */
    CHARACTER_STOPPED = 0,
    CHARACTER_POSITIVE = 1
} CharacterDirection;

typedef struct {
    bool is_hit;
    uint32_t hit_ms;
    uint8_t player_id[2];
    uint16_t angle_x10[2];
    uint16_t separation_x10;
    int16_t signed_gap_x10; /* Shortest signed angle from player[0] to player[1]. */
    int16_t previous_signed_gap_x10;
    bool approach_known;
    bool approaching;
    bool directions_valid; /* Both direction reads succeeded; otherwise do not act on incoming. */
    CharacterDirection incoming[2];
} CharacterPairCollision;

typedef struct {
    uint16_t enter_x10;
    uint16_t release_x10;
    uint32_t max_pair_sample_skew_ms;
    bool detect_initial_overlap;
    void *context;
    bool (*read_direction)(void *, uint8_t player_id, CharacterDirection *);
    /* Optional policy: gets a snapshot of BOTH players. Return false for
     * notification only. No policy is selected by default. */
    bool (*choose_response)(void *, const CharacterPairCollision *, CharacterDirection outgoing[2]);
    /* One paired command per collision, not one per player notification.
     * Adapter owns simultaneous application and game/motor interlocks. */
    void (*apply_response)(void *, const uint8_t player_id[2], const CharacterDirection outgoing[2]);
} CharacterControllerConfig;

typedef struct {
    CharacterModule *players[2];
    CharacterControllerConfig config;
    bool initialized;
    bool pair_valid;
    bool baseline_seen;
    bool armed;
    int16_t previous_signed_gap_x10;
    uint16_t previous_separation_x10;
    uint32_t calibration_revision[2];
    CharacterPairCollision pending_event;
    uint32_t dropped_events;
} CharacterController;

/* Provisional defaults: enter <=5 degrees, rearm >10 degrees, sample skew
 * <=5ms, no automatic motor response, and no hit on initial overlap. */
CharacterControllerConfig character_controller_default_config(void);

/* Bind two distinct initialized players and validate thresholds/callbacks.
 * Returns false on invalid input, with destination unchanged. Does not start
 * homing, copy the players, attach interrupts or touch g_state. Both player
 * objects and callback context must outlive the controller. */
bool character_controller_init(CharacterController *controller,
                               CharacterModule *first, CharacterModule *second,
                               const CharacterControllerConfig *config);
/* Updates both modules even when collisions_enabled=false (e.g. homing).
 * Caller supplies game eligibility; no dependency on GL. Call this OR each
 * module's update, not both in the same loop pass.
 *
 * After updating, compare fresh angles only if their sample times are close
 * enough. Trigger once at separation <=enter_x10; rearm at >release_x10.
 * The larger release threshold prevents repeated responses while overlapping.
 * By default initial overlap (including recovery from invalid/disabled data)
 * needs separation before another approach can trigger. An offset refresh
 * suppresses detection on that pass and requires separation if still close;
 * this avoids treating recalibration as motion but may skip a coincident hit.
 * Does not publish g_state, drain pillar events, or call game_on_collision. */
void character_controller_update(CharacterController *controller, uint32_t now_ms,
                                 bool collisions_enabled);

/* Consume the pending PAIR event, or return is_hit=false. Includes both player
 * IDs, angles, separation, incoming directions and approach information from
 * before any response. Only one pair event is retained; dropped_events counts
 * additional unread events. Use this for pair decisions; the per-player reads
 * are notifications of the same encounter, not two more collisions. */
CharacterPairCollision character_controller_read_collision(CharacterController *controller);
/* OPTIONAL proposed policy, never enabled implicitly. Separate by ordering;
 * catch-up requires observed approach. At coincidence use previous ordering,
 * otherwise decline. A starting point for playtesting, not a settled rule.
 * Return true means both outgoing entries have been filled (-1/0/+1); false
 * means do not apply a response. No motor is commanded by this function.
 * Select it as choose_response with read_direction/apply_response callbacks,
 * OR call it from GL after consuming a pair event. Do not use both routes.
 * context is unused by this built-in policy; custom policies may use it. */
bool character_response_separate(void *context, const CharacterPairCollision *event,
                                 CharacterDirection outgoing[2]);

#ifdef __cplusplus
}
#endif
#endif
