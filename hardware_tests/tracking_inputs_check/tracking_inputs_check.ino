// Tracking inputs check (Wheels Mega)
//
// Prints every change on the photo-interrupters and hit switches, using the
// same pins as crater_crawl_wheels/tracking_hal.cpp:
//   photo-interrupters: A8 (player 0), A9 (player 1)
//   hit switches:       22 (player 0), 23 (player 1), INPUT_PULLUP
//
// Open the Serial Monitor at 115200, then:
//   1. Put something through each photo-interrupter's slot. The line printed
//      while it's blocked tells you the "blocked" level. tracking_hal.cpp
//      assumes HIGH (REFERENCE_BLOCKED_LEVEL); if it prints LOW, change that.
//      The interrupt count should go up each time: that's what Tracking
//      uses to notice the crossing.
//   2. Press each hit switch. It should read LOW while pressed and HIGH when
//      released.

static const uint8_t REFERENCE_PIN[2] = {A8, A9};
static const uint8_t SWITCH_PIN[2] = {22, 23};

static uint8_t s_last_reference[2];
static uint8_t s_last_switch[2];
static volatile uint16_t s_pin_change_count = 0;
static uint16_t s_reported_count = 0;

// Pin-change interrupt for A8..A15: counts changes so we can see it fires.
ISR(PCINT2_vect)
{
    s_pin_change_count++;
}

static void print_change(const __FlashStringHelper *what, uint8_t player, uint8_t level)
{
    Serial.print(millis());
    Serial.print(F(" ms  P"));
    Serial.print(player);
    Serial.print(' ');
    Serial.print(what);
    Serial.println(level == HIGH ? F(": HIGH") : F(": LOW"));
}

void setup()
{
    Serial.begin(115200);
    for (uint8_t p = 0; p < 2; p++)
    {
        pinMode(REFERENCE_PIN[p], INPUT);
        pinMode(SWITCH_PIN[p], INPUT_PULLUP);
        s_last_reference[p] = digitalRead(REFERENCE_PIN[p]);
        s_last_switch[p] = digitalRead(SWITCH_PIN[p]);
    }

    // A8 = PCINT16, A9 = PCINT17, both on pin-change group 2.
    PCMSK2 |= (1 << PCINT16) | (1 << PCINT17);
    PCICR |= (1 << PCIE2);

    Serial.println(F("Tracking inputs check. Starting levels:"));
    for (uint8_t p = 0; p < 2; p++)
    {
        print_change(F("photo-interrupter"), p, s_last_reference[p]);
        print_change(F("hit switch"), p, s_last_switch[p]);
    }
}

void loop()
{
    for (uint8_t p = 0; p < 2; p++)
    {
        uint8_t ref = digitalRead(REFERENCE_PIN[p]);
        if (ref != s_last_reference[p])
        {
            s_last_reference[p] = ref;
            print_change(F("photo-interrupter"), p, ref);
        }

        uint8_t sw = digitalRead(SWITCH_PIN[p]);
        if (sw != s_last_switch[p])
        {
            s_last_switch[p] = sw;
            print_change(F("hit switch"), p, sw);
        }
    }

    noInterrupts();
    uint16_t count = s_pin_change_count;
    interrupts();
    if (count != s_reported_count)
    {
        s_reported_count = count;
        Serial.print(F("  pin-change interrupts so far: "));
        Serial.println(count);
    }
}
