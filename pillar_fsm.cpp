#include "pillar_fsm.h"
#include <iostream>
using namespace std;

// placeholder
#define PILLAR_HOLD_MS 1000 // should this be in the header file (shared or pillar)? is there a reason it isn't?

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

static void sendPillarCommand(uint8_t id, PillarAction action, WarningMode mode, bool isFake)
{
    PillarCommand cmd = {id, action, mode, isFake};
    // TODO (wondering how to implement this one since i think it connects to hardware?)
    cout << "Pillar Command: " << id << action << mode << isFake << "sent" << endl;
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
                    sendPillarCommand(id, ACTION_RAISE, p.warn_mode, false);
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

        case PILLAR_EXTENDED: // discuss with Antoine how to make sure the pillar doesn't get stuck in extended state due to power cap
            if (elapsed >= PILLAR_HOLD_MS && movingCount() < MAX_SIMULTANEOUS_PILLAR_MOVES)
            {
                p.phase = PILLAR_LOWERING;
                p.phase_entered_ms = now_ms;
                sendPillarCommand(id, ACTION_LOWER, p.warn_mode, false);
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
    sendPillarCommand(id, ACTION_WARN_ONLY, mode, isFake);
    return true;
}
