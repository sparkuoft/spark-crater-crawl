#ifndef CHARACTER_CONTROLLER_H
#define CHARACTER_CONTROLLER_H

#include "CharacterModule.h"

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
    bool directions_valid;
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

CharacterControllerConfig character_controller_default_config(void);
bool character_controller_init(CharacterController *controller,
                               CharacterModule *first, CharacterModule *second,
                               const CharacterControllerConfig *config);
/* Updates both modules even when collisions_enabled=false (e.g. homing).
 * Caller supplies game eligibility; no dependency on GL. Call this OR each
 * module's update, not both in the same loop pass. */
void character_controller_update(CharacterController *controller, uint32_t now_ms,
                                 bool collisions_enabled);
CharacterPairCollision character_controller_read_collision(CharacterController *controller);
/* OPTIONAL proposed policy, never enabled implicitly. Separate by ordering;
 * catch-up requires observed approach. At coincidence use previous ordering,
 * otherwise decline. A starting point for playtesting, not a settled rule. */
bool character_response_separate(void *context, const CharacterPairCollision *event,
                                 CharacterDirection outgoing[2]);

#ifdef __cplusplus
}
#endif
#endif
