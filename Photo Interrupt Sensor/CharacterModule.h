#ifndef CHARACTER_MODULE_H
#define CHARACTER_MODULE_H

#include <stdbool.h>
#include <stdint.h>

/* GAME LOGIC: this header describes ONE player's tracking state. Allocate one
 * zero-initialized CharacterModule per player; pass its address to functions.
 * A pointer parameter lets the function work on that player's existing state.
 * 'const CharacterModule *' means a read-only operation on that state.
 *
 * All angles use tenths of a degree (450 = 45 degrees), matching angle_x10 in
 * shared_state.h. All times use the same uint32_t millisecond clock.
 * Tracking detects contact and reports position; GL owns lives, alive,
 * locked_out, round transitions and whether a reported hit should count.
 * See CharacterController.h for setup order and the proposed GL loop.
 *
 * This is a C API. The guards below allow a C++ adapter to call it while the
 * implementations remain compiled as C. Do not include the C++ shared_state.h
 * in these .c files; translate events in the adapter instead. */
#ifdef __cplusplus
extern "C" {
#endif

#define CHARACTER_TURN_X10 3600U
#define CHARACTER_UNKNOWN_PILLAR (-1)
#define CHARACTER_EVENT_CAPACITY 4U

typedef struct {
    int id; /* Zero-based pillar index, or CHARACTER_UNKNOWN_PILLAR. */
    bool is_hit; /* Test this first; false means no queued event. */
    uint32_t hit_ms; /* First observed press, not the time GL reads the event. */
} PillarCollisionStatus;

typedef struct {
    bool is_hit;
    uint32_t hit_ms; /* Same encounter time as the controller's pair event. */
} CharacterCollisionStatus;

typedef struct {
    uint16_t angle_x10; /* Absolute encoder position, normalized to 0..3599. */
    uint32_t sampled_ms; /* Acquisition time, on the same clock as update/ISR. */
} CharacterEncoderSample;

/* All callbacks except critical_enter/exit run in the main loop, never ISR.
 * Reads return false on failure and must not block indefinitely. Encoder reads
 * select the supplied mux channel themselves and use the SAME representation
 * and clock. Convert gearing in HAL. Apply encoder_sign to align directions.
 * read_reference_sample obtains/reconstructs a sample at crossing_ms; it must
 * NEVER label a late read with the crossing time. Missing samples are rejected.
 * critical_enter returns prior interrupt state; critical_exit restores it.
 * They protect ONLY a mailbox copy, never bus I/O or motor commands. */
typedef struct {
    void *context; /* Adapter-owned data passed back to every hardware callback. */
    bool (*read_encoder)(void *, uint8_t channel, CharacterEncoderSample *);
    bool (*read_reference_sample)(void *, uint8_t channel, uint32_t crossing_ms,
                                  CharacterEncoderSample *);
    /* Return value means read succeeded; the output bool is the input state.
     * Convert HIGH/LOW polarity in the adapter, not in game logic. */
    bool (*read_reference_active)(void *, uint8_t player_id, bool *active);
    bool (*read_pillar_pressed)(void *, uint8_t player_id, bool *pressed);
    /* Start controlled movement and report acceptance. stop_homing must stop
     * that movement. Direction/speed and motor wiring belong to Wheels/HAL. */
    bool (*start_homing)(void *, uint8_t player_id);
    void (*stop_homing)(void *, uint8_t player_id);
    uint8_t (*critical_enter)(void *);
    void (*critical_exit)(void *, uint8_t previous_state);
    /* Optional direct wiring attribution. Return -1 if unknown. */
    int (*identify_pillar)(void *, uint8_t player_id);
} CharacterHardware;

typedef struct {
    uint8_t player_id;
    uint8_t encoder_channel;
    int8_t encoder_sign; /* +1 or -1, to align both physical directions. */
    uint16_t reference_rearm_x10;
    uint32_t release_debounce_ms;
    uint32_t homing_timeout_ms;
    uint32_t max_sample_age_ms;
    uint32_t reference_sample_tolerance_ms;
    uint32_t max_reference_event_age_ms;
    bool accept_initial_press;
    /* Optional measured angles; table must outlive module. Only a UNIQUE
     * match within tolerance is attributed; no assumed pillar spacing. */
    const uint16_t *pillar_angles_x10;
    uint8_t pillar_count;
    uint16_t pillar_tolerance_x10;
    CharacterHardware hardware;
} CharacterConfig;

typedef enum {
    CHARACTER_UNCALIBRATED,
    CHARACTER_HOMING,
    CHARACTER_READY,
    CHARACTER_HOMING_TIMEOUT,
    CHARACTER_HOMING_FAILED
} CharacterCalibrationState;

/* Fixed storage; use functions to mutate. Only the reference mailbox is ISR
 * shared. Other fields are main-loop state and read-only diagnostics. */
typedef struct {
    CharacterConfig config;
    bool initialized;
    CharacterCalibrationState calibration;
    uint16_t angle_x10;
    uint16_t reference_offset_x10;
    bool angle_valid;
    bool encoder_error;
    bool reference_input_error;
    bool pillar_input_error;
    uint32_t sampled_ms;
    uint32_t calibration_revision;
    uint32_t rejected_reference_events;
    uint32_t dropped_pillar_events;
    uint32_t dropped_character_events;
    bool reference_armed;
    uint32_t homing_started_ms;
    volatile bool reference_pending;
    volatile uint32_t reference_crossing_ms;
    bool switch_seen;
    bool switch_armed;
    bool release_timing;
    uint32_t release_started_ms;
    PillarCollisionStatus pillar_events[CHARACTER_EVENT_CAPACITY];
    uint8_t pillar_head;
    uint8_t pillar_count;
    CharacterCollisionStatus character_event;
} CharacterModule;

/* Return starting settings, including 45-degree rearm buffer, 20ms stable
 * release and 10s homing timeout. These are provisional, not measurements.
 * Fill in callbacks, player/channel mapping and optional pillar map before
 * init. A returned config alone is not sufficient to operate the hardware. */
CharacterConfig character_default_config(void);

/* Call once before attaching interrupts or starting homing. Validates config,
 * clears state/events and copies config; does no hardware I/O or movement.
 * Returns false without changing the destination if config is invalid.
 * Start with zero-initialized storage and check the result. Config is copied,
 * but its context and pillar-table pointers must remain valid for its lifetime. */
bool character_init(CharacterModule *character, const CharacterConfig *config);

/* Request initial calibration (or an explicit retry). true means homing STARTED,
 * not completed. Keep calling update and wait for READY/valid angle before
 * starting gameplay. If the reference starts blocked, it must clear before a
 * new activation is accepted. update enforces timeout and stops on completion.
 * Inspect calibration for HOMING_TIMEOUT/HOMING_FAILED; GL chooses retry/abort.
 * Calling this on a previously calibrated player invalidates its old angle. */
bool character_start_homing(CharacterModule *character, uint32_t now_ms);

/* Abort an active homing operation, request stop, and mark uncalibrated.
 * Does nothing outside HOMING. This is NOT a general gameplay motor-stop API. */
void character_cancel_homing(CharacterModule *character);

/* Call on reference ACTIVATION edge. No bus access. Bind the correct player
 * in the platform ISR; attach after init, detach before reinit/free.
 * Stores the first pending timestamp only. update later retrieves the matching
 * encoder sample and decides whether to accept it. GL does not call this as
 * part of normal polling or artificially trigger it to set an angle. */
void character_reference_isr(CharacterModule *character, uint32_t crossing_ms);

/* One nonblocking tracking pass: reference processing, encoder validation,
 * angle calculation/rearming, then pillar-switch processing. Call frequently
 * enough to observe the shortest switch contact. No life/phase changes occur.
 * The controller calls this for both players; do not call it again that pass. */
void character_update(CharacterModule *character, uint32_t now_ms);

/* Return last valid cached angle (0..3599), without reading hardware. Zero can
 * also be the initial cache, so never use the returned number as proof of
 * calibration. Check character_angle_is_valid after the current update first. */
uint16_t character_get_angle(const CharacterModule *character);

/* Has a reference offset been established and calibration reached READY?
 * Can remain true during an encoder fault; it does not prove a usable sample. */
bool character_is_calibrated(const CharacterModule *character);

/* Was the latest update both calibrated and supplied with a valid, fresh
 * sample? A false result means GL must not treat the cached number as current.
 * This getter does not advance time: freshness is checked during update. */
bool character_angle_is_valid(const CharacterModule *character);

/* Pop the oldest pillar contact, or return is_hit=false/id=-1 if empty. Repeat
 * until empty each loop, including outside gameplay, to avoid delayed hits in
 * a later round. There are four slots; dropped_pillar_events exposes overflow.
 *
 * GL adapter mapping: module.config.player_id -> CollisionEvent.player_id,
 * id -> pillar_id (after validating/mapping it), is_hit -> hit, hit_ms ->
 * timestamp_ms. Call game_on_collision(ev, now_ms) in the MAIN loop.
 * id=-1 is still real contact. Agree an unknown-pillar contract with GL before
 * converting it to uint8_t; do not invent pillar 0 or discard the contact.
 * GL already decides RUNNING/alive/lockout eligibility. Angle and pillar phase
 * must not postpone the switch hit. Tracking debounce is separate from GL's
 * post-hit lockout: one prevents duplicate presses, the other enforces rules. */
PillarCollisionStatus character_read_pillar_collision(CharacterModule *character);

/* Consume this player's pair-collision notification. Empty: is_hit=false.
 * It is a copy of the encounter reported by the controller, not an extra hit.
 * Do not send it to game_on_collision (the pillar/life-loss path), and do not
 * issue a second motor response if the controller already applied one. */
CharacterCollisionStatus character_read_character_collision(CharacterModule *character);

/* Controller-to-module notification, main-loop only; normally GL need not call
 * this. Keeps one pending event. If unread, preserves it and increments
 * dropped_character_events instead of overwriting its timestamp. */
void character_record_character_collision(CharacterModule *character, uint32_t now_ms);

/* Shortest circular separation, also in tenths: (3580, 20) -> 40 (4 degrees).
 * Inputs must already be normalized to 0..3599. Pure calculation; no I/O. */
uint16_t character_angular_distance(uint16_t first_x10, uint16_t second_x10);

#ifdef __cplusplus
}
#endif
#endif
