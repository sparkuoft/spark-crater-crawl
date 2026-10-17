#include "pillar_seq.h"

// placeholder values

#define MODE_NORMAL_AFTER_MS 15000
#define MODE_HARD_AFTER_MS 30000

#define SPAWN_GAP_START_MS 3500
#define SPAWN_GAP_FLOOR_MS 2000 // comes from the total pillar movement time / power cap limit
#define SPAWN_RAMP_MS 90000

const uint8_t NO_PILLAR = 0xFF;
static uint8_t lastPicked = NO_PILLAR;

static WarningMode modeForElapsed(uint32_t elapsed_ms) {
    if (elapsed_ms >= MODE_HARD_AFTER_MS) return WARN_HARD;
    if (elapsed_ms >= MODE_NORMAL_AFTER_MS) return WARN_NORMAL;
    return WARN_EASY;
}

static uint32_t spawnIntervalMs(uint32_t elapsed_ms) {
    if (elapsed_ms >= SPAWN_RAMP_MS)
        return SPAWN_GAP_FLOOR_MS;

    uint32_t drop = (SPAWN_GAP_START_MS - SPAWN_GAP_FLOOR_MS) * elapsed_ms / SPAWN_RAMP_MS;
    return SPAWN_GAP_START_MS - drop;
}

static uint8_t busyCount() {
    uint8_t n = 0;
    for (uint8_t i = 0; i < NUM_PILLARS; i++)
    {
        if (g_state.pillars[i].phase != PILLAR_IDLE)
            n++;
    }
    return n;
}

static bool capAllowsNewPillar() {
    return busyCount() < MAX_SIMULTANEOUS_PILLAR_MOVES;
}

static uint8_t pickPillar() {
    uint8_t candidates[NUM_PILLARS];
    uint8_t n = 0;

    for (uint8_t i = 0; i < NUM_PILLARS; i++)
    {
        if (g_state.pillars[i].phase == PILLAR_IDLE && i != lastPicked) {
            candidates[n] = i;
            n++;
        }
    }

    if (n == 0) { // lastPicked is the only idle pillar
        if (lastPicked != NO_PILLAR && g_state.pillars[lastPicked].phase == PILLAR_IDLE) {
            return lastPicked;
        } else {
            return NO_PILLAR;
        }
    }

    return (uint8_t)candidates[rand() % n];
}

void pillarSeqInit(uint32_t now_ms) {

}
 
void pillarSeqUpdate(uint32_t now_ms) {

}