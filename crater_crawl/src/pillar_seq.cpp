#include "pillar_seq.h"
#include "game_fsm.h" // game_difficulty(): GL's single source of truth for difficulty

// placeholder values

#define SPAWN_GAP_START_MS 3500
#define SPAWN_GAP_FLOOR_MS 2000 // comes from the total pillar movement time / power cap limit
#define SPAWN_RAMP_MS 90000

#define FIRST_SPAWN_DELAY_MS 2000 // grace period before the first warning

const uint8_t NO_PILLAR = 0xFF;
static uint8_t lastPicked = NO_PILLAR;

static uint32_t gameStart_ms = 0; // set in pillarSeqInit
static uint32_t nextSpawn_ms = 0; // earliest time the next pillar may start
static bool started = false;      // false until the first update of a round calls pillarSeqInit

static uint32_t spawnIntervalMs(uint32_t elapsed_ms)
{
    if (elapsed_ms >= SPAWN_RAMP_MS)
        return SPAWN_GAP_FLOOR_MS;

    uint32_t drop = (SPAWN_GAP_START_MS - SPAWN_GAP_FLOOR_MS) * elapsed_ms / SPAWN_RAMP_MS;
    return SPAWN_GAP_START_MS - drop;
}

static uint8_t busyCount()
{
    uint8_t n = 0;
    for (uint8_t i = 0; i < NUM_PILLARS; i++)
    {
        if (g_state.pillars[i].phase != PILLAR_IDLE)
            n++;
    }
    return n;
}

static bool capAllowsNewPillar()
{
    return busyCount() < MAX_SIMULTANEOUS_PILLAR_MOVES;
}

static uint8_t pickPillar()
{
    uint8_t candidates[NUM_PILLARS];
    uint8_t n = 0;

    for (uint8_t i = 0; i < NUM_PILLARS; i++)
    {
        if (g_state.pillars[i].phase == PILLAR_IDLE && i != lastPicked)
        {
            candidates[n] = i;
            n++;
        }
    }

    if (n == 0)
    { // lastPicked is the only idle pillar
        if (lastPicked != NO_PILLAR && g_state.pillars[lastPicked].phase == PILLAR_IDLE)
        {
            return lastPicked;
        }
        else
        {
            return NO_PILLAR;
        }
    }

    return (uint8_t)candidates[rand() % n];
}

// Fake warnings stay off until the team decides on them.
#define FAKE_WARNINGS_ENABLED 0

// random placeholder logic up for "ankle-breakers" based on SHARED_STATE_H
static bool shouldSpawnFake(WarningMode mode)
{
    if (!FAKE_WARNINGS_ENABLED || mode != WARN_HARD)
    {
        return false;
    }
    else
    {
        return (rand() % 2);
    }
}

void pillarSeqInit(uint32_t now_ms)
{
    gameStart_ms = now_ms;
    nextSpawn_ms = now_ms + FIRST_SPAWN_DELAY_MS;
    lastPicked = NO_PILLAR;
    srand(now_ms); // start time varies with when players press start
    started = true;
}

void pillarSeqUpdate(uint32_t now_ms)
{
    // game_fsm only calls pillarSeqReset() at round start, so the first update
    // of each round is where the sequencer picks up its start time.
    if (!started)
        pillarSeqInit(now_ms);

    // Signed cast so this stays correct when millis() wraps (~49 days).
    if ((int32_t)(now_ms - nextSpawn_ms) < 0)
        return;

    if (!capAllowsNewPillar())
        return;

    uint8_t id = pickPillar();
    if (id == NO_PILLAR)
        return;

    uint32_t elapsed = now_ms - gameStart_ms;
    WarningMode mode = game_difficulty();
    bool isFake = shouldSpawnFake(mode);

    // only record the pick and schedule the next spawn if the start succeeded.
    if (pillarStartWarning(id, mode, isFake, now_ms))
    {
        lastPicked = id;
        nextSpawn_ms = now_ms + spawnIntervalMs(elapsed);
    }
}

// Called by game_fsm at init, at round start, and when pillars are lowered.
// The next pillarSeqUpdate re-initialises with that moment as the start time.
void pillarSeqReset()
{
    started = false;
    lastPicked = NO_PILLAR;
}
