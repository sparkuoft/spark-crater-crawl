#include "pillar_fsm.h"
#include "debug_log.h"

// Per-pillar flag: true if the current warning is an ankle-breaker (no rise).
// Internal to the pillar FSM, so it lives here rather than in shared_state.h.
static bool fake[NUM_PILLARS];

// placeholder
#define PILLAR_HOLD_MS 1000UL // should this be in the header file (shared or pillar)? is there a reason it isn't?

static uint32_t warnDurationMs(WarningMode mode)
{
    switch (mode)
    {
    case WARN_EASY:
        return 1500; // placeholder
    case WARN_NORMAL:
        return 1000; // placeholder
    case WARN_HARD:
        return 600; // placeholder
    }
    return 1000; // placeholder
}

// how many pillars are moving (so we don't exceed the power cap)
static uint8_t movingCount()
{
    uint8_t n = 0;
    for (uint8_t i = 0; i < NUM_PILLARS; i++)
    {
        PillarPhase p = g_state.pillars[i].phase;
        if (p == PILLAR_RISING || p == PILLAR_LOWERING)
            n++;
    }
    return n;
}

static const char *actionName(PillarAction a)
{
    switch (a)
    {
    case ACTION_NONE:
        return "NONE";
    case ACTION_RAISE:
        return "RAISE";
    case ACTION_LOWER:
        return "LOWER";
    case ACTION_WARN_ONLY:
        return "WARN";
    }
    return "?";
}

// Outbound to Pillars (P|E). No transport agreed yet, so for now this only
// logs the payload (same approach as send_wheels_trigger in game_fsm.cpp).
static void sendPillarCommand(const PillarCommand &cmd, uint32_t now_ms)
{
    log_line(now_ms, "PILLAR CMD pillar=%u action=%s mode=%s fake=%s",
             (unsigned)cmd.pillar_id, actionName(cmd.action),
             warning_mode_name(cmd.warn_mode), cmd.is_fake ? "yes" : "no");
}

static void sendPillarCommand(uint8_t id, PillarAction action, WarningMode mode, bool isFake, uint32_t now_ms)
{
    PillarCommand cmd = {id, action, mode, isFake};
    sendPillarCommand(cmd, now_ms);
}

// finite state machine
void pillarFsmUpdate(uint32_t now_ms)
{

    for (uint8_t id = 0; id < NUM_PILLARS; id++)
    {
        PillarState &p = g_state.pillars[id];
        uint32_t elapsed = now_ms - p.phase_entered_ms;

        switch (p.phase)
        {
        case PILLAR_IDLE:
            break;

        case PILLAR_WARNING:
            if (elapsed >= warnDurationMs(p.warn_mode))
            {
                if (fake[id])
                {
                    p.phase = PILLAR_IDLE;
                    p.phase_entered_ms = now_ms;
                }
                else if (movingCount() < MAX_SIMULTANEOUS_PILLAR_MOVES)
                {
                    p.phase = PILLAR_RISING;
                    p.phase_entered_ms = now_ms;
                    sendPillarCommand(id, ACTION_RAISE, p.warn_mode, false, now_ms);
                }
            }
            break;

        case PILLAR_RISING:
            if (elapsed >= PILLAR_RISE_MS)
            {
                p.phase = PILLAR_EXTENDED;
                p.phase_entered_ms = now_ms;
            }
            break;

        // Can't get stuck here: pillar_seq caps ALL non-IDLE pillars at
        // MAX_SIMULTANEOUS_PILLAR_MOVES, so while this one is EXTENDED at most
        // cap-1 others can be moving. Loosening that cap would reopen this.
        case PILLAR_EXTENDED:
            if (elapsed >= PILLAR_HOLD_MS && movingCount() < MAX_SIMULTANEOUS_PILLAR_MOVES)
            {
                p.phase = PILLAR_LOWERING;
                p.phase_entered_ms = now_ms;
                sendPillarCommand(id, ACTION_LOWER, p.warn_mode, false, now_ms);
            }
            break;

        case PILLAR_LOWERING:
            if (elapsed >= PILLAR_LOWER_MS)
            {
                p.phase = PILLAR_IDLE;
                p.phase_entered_ms = now_ms;
            }
            break;
        }
    }
}

void pillarFsmInit(uint32_t now_ms)
{
    for (uint8_t id = 0; id < NUM_PILLARS; id++)
    {
        g_state.pillars[id].phase = PILLAR_IDLE;
        g_state.pillars[id].warn_mode = WARN_EASY;
        g_state.pillars[id].phase_entered_ms = now_ms;
        fake[id] = false;
    }
}

bool pillarStartWarning(uint8_t id, WarningMode mode, bool isFake, uint32_t now_ms)
{
    if (id >= NUM_PILLARS)
    {
        return false;
    }

    PillarState &p = g_state.pillars[id];

    if (p.phase != PILLAR_IDLE)
    {
        return false;
    }

    p.phase = PILLAR_WARNING;
    p.warn_mode = mode;
    p.phase_entered_ms = now_ms;
    fake[id] = isFake;
    sendPillarCommand(id, ACTION_WARN_ONLY, mode, isFake, now_ms);
    return true;
}
