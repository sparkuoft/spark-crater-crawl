// Mega only: FastLED doesn't exist in the laptop build (see pillar_led_laptop.cpp).
#ifdef ARDUINO

#include <FastLED.h>
#include "pillar_led.h"

// --- HARDWARE CONFIGURATION ---
#define LED_PIN          6
#define LEDS_PER_PILLAR  5
#define TOTAL_LEDS       (NUM_PILLARS * LEDS_PER_PILLAR)
#define LED_TYPE         WS2812B
#define COLOR_ORDER      GRB

CRGB leds[TOTAL_LEDS];

const CRGB DEEP_ORANGE = CRGB(255, 60, 0);

// --- ANIMATION STATES ---
enum AnimationStage {
    STAGE_IDLE,
    STAGE_YELLOW,
    STAGE_ORANGE,
    STAGE_RED
};

// Each pillar has its own animation
AnimationStage animationStages[NUM_PILLARS] = {};

uint32_t stageStartTime[NUM_PILLARS] = {};
uint32_t lastToggleTime[NUM_PILLARS] = {};
bool toggleState[NUM_PILLARS] = {};
uint32_t yellowDuration[NUM_PILLARS] = {};
uint32_t orangeDuration[NUM_PILLARS] = {};

// --- LED HELPER FUNCTION ---
void setPillarColor(uint8_t pillar_id, CRGB color) {

    if (pillar_id >= NUM_PILLARS) return;

    int startIdx = pillar_id * LEDS_PER_PILLAR;

    for (int i = 0; i < LEDS_PER_PILLAR; i++) {
        leds[startIdx + i] = color;
    }
}

// Match Game Logic warning durations
uint32_t getWarningDuration(WarningMode mode) {
    switch (mode) {
        case WARN_EASY:   return 1500;
        case WARN_NORMAL: return 1000;
        case WARN_HARD:   return 600;
    }
    return 1000;
}

// --- START WARNING ANIMATION ---
void startAnimation(uint8_t pillar_id,
                    WarningMode mode,
                    uint32_t now_ms) {

    if (pillar_id >= NUM_PILLARS) return;

    uint32_t duration = getWarningDuration(mode);

    // 80% yellow, 20% orange
    yellowDuration[pillar_id] = duration * 4 / 5;
    orangeDuration[pillar_id] = duration - yellowDuration[pillar_id];

    animationStages[pillar_id] = STAGE_YELLOW;
    stageStartTime[pillar_id] = now_ms;
    lastToggleTime[pillar_id] = now_ms;
    toggleState[pillar_id] = true;
}

// --- RECEIVE GAME LOGIC COMMAND ---
void pillarLedsHandleCommand(const PillarCommand &cmd,
                             uint32_t now_ms) {

    uint8_t id = cmd.pillar_id;

    if (id >= NUM_PILLARS) return;

    switch (cmd.action) {

        case ACTION_WARN_ONLY:
            startAnimation(id, cmd.warn_mode, now_ms);
            break;

        case ACTION_RAISE:
            animationStages[id] = STAGE_RED;
            stageStartTime[id] = now_ms;
            break;

        case ACTION_LOWER:
        case ACTION_NONE:
            animationStages[id] = STAGE_IDLE;
            break;
    }
}

// --- UPDATE ANIMATIONS ---
void pillarLedsUpdate(uint32_t now_ms) {

    for (uint8_t i = 0; i < NUM_PILLARS; i++) {

        // Synchronize with actual Game Logic state.
        // This cancels fake warnings and reset warnings.
        PillarPhase phase = g_state.pillars[i].phase;

        if (phase == PILLAR_IDLE ||
            phase == PILLAR_LOWERING) {

            animationStages[i] = STAGE_IDLE;
        }

        // Solid red while rising or extended.
        if (phase == PILLAR_RISING ||
            phase == PILLAR_EXTENDED) {

            animationStages[i] = STAGE_RED;
        }

        switch (animationStages[i]) {

            case STAGE_IDLE:
                setPillarColor(i, CRGB::Black);
                break;

            // YELLOW: Slow flashing
            case STAGE_YELLOW:

                if (now_ms - stageStartTime[i] >=
                    yellowDuration[i]) {

                    animationStages[i] = STAGE_ORANGE;
                    stageStartTime[i] = now_ms;
                    lastToggleTime[i] = now_ms;
                    toggleState[i] = true;
                }
                else if (now_ms - lastToggleTime[i] >= 500) {
                    lastToggleTime[i] = now_ms;
                    toggleState[i] = !toggleState[i];
                }

                setPillarColor(
                    i,
                    animationStages[i] == STAGE_ORANGE
                        ? DEEP_ORANGE
                        : (toggleState[i] ? CRGB::Yellow : CRGB::Black)
                );
                break;

            // ORANGE: Fast flashing
            case STAGE_ORANGE:

                if (now_ms - stageStartTime[i] >=
                    orangeDuration[i]) {

                    // If Game Logic has not commanded a rise,
                    // keep flashing orange until it does.
                    if (now_ms - lastToggleTime[i] >= 100) {
                        lastToggleTime[i] = now_ms;
                        toggleState[i] = !toggleState[i];
                    }
                }
                else if (now_ms - lastToggleTime[i] >= 100) {
                    lastToggleTime[i] = now_ms;
                    toggleState[i] = !toggleState[i];
                }

                setPillarColor(
                    i,
                    toggleState[i] ? DEEP_ORANGE : CRGB::Black
                );
                break;

            // RED: Solid while raised
            case STAGE_RED:
                setPillarColor(i, CRGB::Red);
                break;
        }
    }

    // Send all LED colours to the ring
    FastLED.show();
}

// --- INITIALIZE LEDS ---
void pillarLedsInit() {

    FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(
        leds, TOTAL_LEDS
    );

    FastLED.setBrightness(128);

    for (uint8_t i = 0; i < NUM_PILLARS; i++) {
        animationStages[i] = STAGE_IDLE;
    }

    FastLED.clear();
    FastLED.show();
}

#endif // ARDUINO
