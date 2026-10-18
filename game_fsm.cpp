#include "game_fsm.h"
#include "pillar_fsm.h"
#include "sequencer.h"
#include "debug_log.h"

static_assert(DIFFICULTY_NORMAL_AT_MS < DIFFICULTY_HARD_AT_MS, "difficulty thresholds out of order");
static_assert(PLAYER_LIVES >= 1, "PLAYER_LIVES must be at least 1");

static WarningMode s_difficulty = WARN_EASY;
static uint32_t s_round_started_ms = 0;
static uint8_t s_hits[NUM_PLAYERS];

static void enter_phase(GamePhase next, uint32_t now_ms)
{
    log_line(now_ms, "PHASE %s -> %s", game_phase_name(g_state.phase), game_phase_name(next));

    g_state.phase = next;
    g_state.phase_entered_ms = now_ms;
}

// Based only on time into the round, so it never steps back down mid-round.
static WarningMode difficulty_for(uint32_t round_elapsed_ms)
{
    if (round_elapsed_ms >= DIFFICULTY_HARD_AT_MS)
        return WARN_HARD;
    if (round_elapsed_ms >= DIFFICULTY_NORMAL_AT_MS)
        return WARN_NORMAL;
    return WARN_EASY;
}

static void update_difficulty(uint32_t now_ms)
{
    WarningMode next = difficulty_for(now_ms - s_round_started_ms);
    if (next == s_difficulty)
        return;

    log_line(now_ms, "DIFFICULTY %s -> %s", warning_mode_name(s_difficulty), warning_mode_name(next));
    s_difficulty = next;
}

static void reset_players()
{
    for (uint8_t i = 0; i < NUM_PLAYERS; i++)
    {
        PlayerState &pl = g_state.players[i]; // angle_x10 belongs to T|E, leave it
        pl.alive = true;
        pl.locked_out = false;
        pl.lockout_started_ms = 0;
        s_hits[i] = 0;
    }
}

static void update_lockouts(uint32_t now_ms)
{
    for (uint8_t i = 0; i < NUM_PLAYERS; i++)
    {
        PlayerState &pl = g_state.players[i];
        if (pl.locked_out && now_ms - pl.lockout_started_ms >= POST_HIT_LOCKOUT_MS)
        {
            pl.locked_out = false;
            log_line(now_ms, "P%u lockout over", (unsigned)i);
        }
    }
}

static bool any_player_eliminated()
{
    for (uint8_t i = 0; i < NUM_PLAYERS; i++)
    {
        if (!g_state.players[i].alive)
            return true;
    }
    return false;
}

static void announce_result(uint32_t now_ms)
{
    uint8_t alive_count = 0;
    uint8_t survivor = 0;
    for (uint8_t i = 0; i < NUM_PLAYERS; i++)
    {
        if (g_state.players[i].alive)
        {
            alive_count++;
            survivor = i;
        }
    }

    if (alive_count == 1)
        log_line(now_ms, "RESULT P%u wins", (unsigned)survivor);
    else
        log_line(now_ms, "RESULT draw");
}

static bool all_pillars_down()
{
    for (uint8_t i = 0; i < NUM_PILLARS; i++)
    {
        if (g_state.pillars[i].phase != PILLAR_IDLE)
            return false;
    }
    return true;
}

// TEMPORARY until pillar_fsm has a cancel / lower-all call of its own.
// Stops the sequencer and cancels pending warnings so nothing new rises.
// Pillars already RISING / EXTENDED / LOWERING are left to the pillar FSM,
// which brings them down within MAX_SIMULTANEOUS_PILLAR_MOVES -- forcing all
// six down at once could break the power cap.
static void lower_all_pillars(uint32_t now_ms)
{
    sequencer_reset();

    for (uint8_t i = 0; i < NUM_PILLARS; i++)
    {
        PillarState &p = g_state.pillars[i];
        if (p.phase == PILLAR_WARNING)
        {
            p.phase = PILLAR_IDLE;
            p.phase_entered_ms = now_ms;
            log_line(now_ms, "pillar %u warning cancelled", (unsigned)i);
        }
    }
}

// Outbound to Wheels (W|E). No transport agreed with Alice yet, so for now
// this only logs the payload.
static void send_wheels_trigger(const WheelsTrigger &t, uint32_t now_ms)
{
    log_line(now_ms, "WHEELS player=%u fatal=%s ts=%lu",
             (unsigned)t.player_id, t.fatal ? "yes" : "no", (unsigned long)t.timestamp_ms);
}

void game_fsm_init(uint32_t now_ms)
{
    g_state.phase = PHASE_IDLE;
    g_state.phase_entered_ms = now_ms;
    reset_players();
    s_difficulty = WARN_EASY;
    pillarFsmInit(now_ms);
    sequencer_reset();

    log_line(now_ms, "game FSM ready, phase IDLE");
}

void game_start(uint32_t now_ms)
{
    if (g_state.phase != PHASE_IDLE)
    {
        log_line(now_ms, "start ignored: not IDLE (reset first)");
        return;
    }
    if (!all_pillars_down())
    {
        log_line(now_ms, "start ignored: pillars still coming down");
        return;
    }

    reset_players();
    enter_phase(PHASE_COUNTDOWN, now_ms);
}

void game_reset(uint32_t now_ms)
{
    lower_all_pillars(now_ms);
    reset_players();
    s_difficulty = WARN_EASY;

    if (g_state.phase != PHASE_IDLE)
        enter_phase(PHASE_IDLE, now_ms);
}

void game_on_collision(const CollisionEvent &ev, uint32_t now_ms)
{
    if (!ev.hit)
        return;

    if (ev.player_id >= NUM_PLAYERS)
    {
        log_line(now_ms, "collision ignored: bad player_id %u", (unsigned)ev.player_id);
        return;
    }

    // A lockout may have run out since the last update; settle it before checking.
    update_lockouts(now_ms);
    PlayerState &pl = g_state.players[ev.player_id];

    // Ignored hits are logged too so they're visible while testing. If
    // Tracking ends up re-reporting every loop while a player overlaps a
    // pillar, this will flood the log and should be quietened.
    const char *ignored = nullptr;
    if (g_state.phase != PHASE_RUNNING)
        ignored = "not RUNNING";
    else if (!pl.alive)
        ignored = "already eliminated";
    else if (pl.locked_out)
        ignored = "locked out";

    log_line(now_ms, "HIT P%u pillar %u (%s) ts=%lu%s%s",
             (unsigned)ev.player_id, (unsigned)ev.pillar_id,
             ev.pillar_id < NUM_PILLARS ? pillar_phase_name(g_state.pillars[ev.pillar_id].phase) : "bad id",
             (unsigned long)ev.timestamp_ms,
             ignored ? ": ignored, " : "", ignored ? ignored : "");
    if (ignored)
        return;

    // Pillar phase deliberately not checked: Tracking is the authority, and a
    // pillar can hit a player while RISING, not just EXTENDED.
    s_hits[ev.player_id]++;

    WheelsTrigger t;
    t.player_id = ev.player_id;
    t.fatal = s_hits[ev.player_id] >= PLAYER_LIVES;
    t.timestamp_ms = ev.timestamp_ms;
    send_wheels_trigger(t, now_ms);

    if (t.fatal)
    {
        // Wheels holds on a fatal hit, so no lockout. RUNNING ends the round on
        // the next update, which also lets a same-loop hit on the other player
        // count as a draw.
        pl.alive = false;
        return;
    }

    // Timed from when the trigger is sent, not ev.timestamp_ms: Alice's 1.2 s
    // is measured end-to-end from the command.
    pl.locked_out = true;
    pl.lockout_started_ms = now_ms;
    log_line(now_ms, "P%u locked out", (unsigned)ev.player_id);
}

void game_fsm_update(uint32_t now_ms)
{
    update_lockouts(now_ms);

    uint32_t in_phase_ms = now_ms - g_state.phase_entered_ms;

    switch (g_state.phase)
    {
    case PHASE_IDLE:
        break;

    case PHASE_COUNTDOWN:
        if (in_phase_ms >= COUNTDOWN_MS)
        {
            sequencer_reset();
            s_round_started_ms = now_ms;
            s_difficulty = WARN_EASY;
            enter_phase(PHASE_RUNNING, now_ms);
        }
        break;

    case PHASE_RUNNING:
        if (any_player_eliminated())
        {
            lower_all_pillars(now_ms);
            enter_phase(PHASE_PLAYER_ELIMINATED, now_ms);
            announce_result(now_ms);
            break;
        }
        update_difficulty(now_ms);
        sequencer_update(now_ms, s_difficulty);
        break;

    case PHASE_PLAYER_ELIMINATED:
        if (all_pillars_down())
            enter_phase(PHASE_GAME_OVER, now_ms);
        break;

    case PHASE_GAME_OVER:
        break;
    }

    // Every phase, not just RUNNING, so pillars finish their motion after a round ends.
    pillarFsmUpdate(now_ms);
}

WarningMode game_difficulty()
{
    return s_difficulty;
}
