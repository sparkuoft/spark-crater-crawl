#include "tracking_adapter.h"
#include "tracking_hal.h"
#include "tracking/CharacterModule.h"
#include "game_fsm.h"
#include "debug_log.h"

/* Tracking adapter (GL). TEMPLATE: the numbered TODOs are the parts to write.
 * It compiles as-is and does nothing until they're filled in.
 *
 * Decisions this follows (2026-11-15):
 *  - A hit is a limit-switch press, and the switch only registers once the
 *    pillar is fully extended (Anthony). game_on_collision keeps its
 *    EXTENDED-only check.
 *  - No player-player collisions (the rings are vertically aligned), so
 *    Tracking's CharacterController isn't used. Each player's module is
 *    updated directly.
 *  - GL picks CollisionEvent.pillar_id (TODO 4 in tracking_update).
 *  - g_state.players[i].angle_x10 is written here and nowhere else.
 */

static_assert(ANGLE_MAX_X10 == CHARACTER_TURN_X10,
              "Tracking and shared_state.h must use the same angle units");

// One Tracking module per player. Zero-initialised, set up by tracking_init().
static CharacterModule s_players[NUM_PLAYERS];
static bool s_initialized = false;

bool tracking_init()
{
    // TODO 1: Set up each player's module. For each player i (0 and 1):
    //     CharacterConfig cfg = character_default_config();
    //     cfg.player_id = i;
    //     cfg.encoder_channel = tracking_hal_encoder_channel(i);
    //     cfg.encoder_sign = tracking_hal_encoder_sign(i);
    //     cfg.hardware = tracking_hal_callbacks();
    //     if (!character_init(&s_players[i], &cfg)) -> log_line(...) and return false
    //   For first tests, cfg.homing_timeout_ms = 60000 gives a minute to push
    //   the figure past the photo-interrupter by hand (default is 10 s).
    //
    // TODO 2: Only after BOTH inits succeed:
    //     s_initialized = true;
    //     tracking_hal_begin();   // pin modes and interrupts
    //   The interrupts must not be attached before the modules are set up.
    //
    // TODO 3: log_line(..., "tracking ready") and return true.
    return false;
}

bool tracking_home(uint8_t player_id, uint32_t now_ms)
{
    // TODO: Return false if !s_initialized or player_id >= NUM_PLAYERS.
    //   Only home between rounds (g_state.phase == PHASE_IDLE).
    //   Then: return character_start_homing(&s_players[player_id], now_ms);
    //   Afterwards, s_players[player_id].calibration says how it went:
    //   CHARACTER_READY, CHARACTER_HOMING_TIMEOUT or CHARACTER_HOMING_FAILED.
    (void)player_id;
    (void)now_ms;
    return false;
}

void tracking_update(uint32_t now_ms)
{
    if (!s_initialized)
        return;

    // TODO 1: tracking_hal_service();
    //   Reads both encoders and keeps a short history (Tracking writes it).

    for (uint8_t i = 0; i < NUM_PLAYERS; i++)
    {
        // TODO 2: character_update(&s_players[i], now_ms);
        //
        // TODO 3: Publish the angle, only when it's valid:
        //     if (character_angle_is_valid(&s_players[i]))
        //         g_state.players[i].angle_x10 = character_get_angle(&s_players[i]);
        //   Valid means: homed, and this pass's encoder read worked, is
        //   0..3599 and is at most 20 ms old. Otherwise keep the old value.
        //
        // TODO 4: Deliver every switch hit, in EVERY phase. game_on_collision
        //   ignores hits outside a round; a hit left in the queue would land
        //   in the next round instead.
        //     for (;;) {
        //         PillarCollisionStatus hit = character_read_pillar_collision(&s_players[i]);
        //         if (!hit.is_hit) break;
        //         uint8_t pillar = <choose the pillar, see below>;
        //         if (<no pillar>) {
        //             log_line(now_ms, "P%u switch pressed but no pillar is up: ignored", (unsigned)i);
        //             continue;
        //         }
        //         CollisionEvent ev;
        //         ev.player_id = i;
        //         ev.pillar_id = pillar;
        //         ev.hit = true;
        //         ev.timestamp_ms = hit.hit_ms;  // when the switch was pressed
        //         game_on_collision(ev, now_ms); // now_ms: when we deliver it
        //     }
        //
        //   Choosing the pillar:
        //     a) If 0 <= hit.id < NUM_PILLARS, Tracking knows it: use it.
        //     b) Otherwise it must be an EXTENDED pillar, because the switch
        //        only registers at full extension. Look through
        //        g_state.pillars[p].phase == PILLAR_EXTENDED:
        //          - one: use it
        //          - several: the one closest to the player's angle once pillar
        //            positions are measured; until then the first one. That's
        //            fine for now: the game only uses pillar_id for the
        //            EXTENDED check and the log.
        //          - none: not a real hit, don't send it.
    }

    // Both players' hits are delivered before game_fsm_update() runs in
    // loop(), so two hits in the same pass still end in a draw.
    (void)now_ms;
}

bool tracking_ready()
{
    // TODO: return character_angle_is_valid(&s_players[0])
    //            && character_angle_is_valid(&s_players[1]);
    //   (false while !s_initialized). Used as the start check, see README.
    return false;
}

// Runs inside the photo-interrupter interrupt (tracking_hal.cpp). Keep it
// exactly this short: no Serial, no I2C, no game calls inside an interrupt.
void tracking_reference_edge(uint8_t player_id, uint32_t crossing_ms)
{
    if (s_initialized && player_id < NUM_PLAYERS)
        character_reference_isr(&s_players[player_id], crossing_ms);
}
