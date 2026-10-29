#ifndef CHARACTER_MODULE_H
#define CHARACTER_MODULE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CHARACTER_TURN_X10 3600U
#define CHARACTER_UNKNOWN_PILLAR (-1)
#define CHARACTER_EVENT_CAPACITY 4U

typedef struct {
    int id; /* Zero-based pillar index, or CHARACTER_UNKNOWN_PILLAR. */
    bool is_hit;
    uint32_t hit_ms;
} PillarCollisionStatus;

typedef struct {
    bool is_hit;
    uint32_t hit_ms;
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
    void *context;
    bool (*read_encoder)(void *, uint8_t channel, CharacterEncoderSample *);
    bool (*read_reference_sample)(void *, uint8_t channel, uint32_t crossing_ms,
                                  CharacterEncoderSample *);
    bool (*read_reference_active)(void *, uint8_t player_id, bool *active);
    bool (*read_pillar_pressed)(void *, uint8_t player_id, bool *pressed);
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

/* Defaults are provisional software tuning, not measured hardware values.
 * Caller MUST supply callbacks and actual player/channel mappings. */
CharacterConfig character_default_config(void);
bool character_init(CharacterModule *character, const CharacterConfig *config);
bool character_start_homing(CharacterModule *character, uint32_t now_ms);
void character_cancel_homing(CharacterModule *character);
/* Call on reference ACTIVATION edge. No bus access. Bind the correct player
 * in the platform ISR; attach after init, detach before reinit/free. */
void character_reference_isr(CharacterModule *character, uint32_t crossing_ms);
void character_update(CharacterModule *character, uint32_t now_ms);
uint16_t character_get_angle(const CharacterModule *character);
bool character_is_calibrated(const CharacterModule *character);
bool character_angle_is_valid(const CharacterModule *character);
/* Consuming reads; empty returns is_hit=false (pillar id=-1). */
PillarCollisionStatus character_read_pillar_collision(CharacterModule *character);
CharacterCollisionStatus character_read_character_collision(CharacterModule *character);
/* Controller-to-module notification, main-loop only. */
void character_record_character_collision(CharacterModule *character, uint32_t now_ms);
/* Inputs must be normalized to 0..3599. */
uint16_t character_angular_distance(uint16_t first_x10, uint16_t second_x10);

#ifdef __cplusplus
}
#endif
#endif
