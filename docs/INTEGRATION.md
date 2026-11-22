# How Crater Crawl fits together (two Megas)

Written 2026-11-22, when Wheels, Tracking and Pillars were first connected to the
game. If you only read one section, read [What happens during a round](#what-happens-during-a-round).

## The short version

The game runs on **two Arduino Megas** connected by a serial cable.

```
        GAME MEGA  (crater_crawl/)                       WHEELS MEGA  (crater_crawl_wheels/)
 +------------------------------------+            +---------------------------------------+
 | game FSM     rounds, hits, lockout |            | Alice's wheel code                    |
 | pillar FSM + sequencer             |  Serial2   |   steering wheel (DC motor + encoder) |
 | LED strip (Pillars)  pin 6         |  16 <-> 17 |   character stepper                   |
 | console on USB (Serial Monitor)    |<---------->|   AS5600 angle sensor (via mux)       |
 | wheels_link.cpp                    |  + GND     | Tracking's code                       |
 +------------------------------------+            |   photo-interrupters (homing)         |
                                                   |   hit switches                        |
                                                   | game_link.cpp                         |
                                                   +---------------------------------------+
     Game -> Wheels:  T (player hit), X (release everyone), M (home a player)
     Wheels -> Game:  B (booted), H (hit switch pressed), A (angle + tracking state, every 100 ms)
```

**Why two boards?** Alice's wheel code and the game couldn't share one Mega:

- Her two steering-wheel encoders need four of the Mega's interrupt pins (2, 3, 18, 19).
  The other two (20, 21) are the I2C pins the angle sensors use, so there are none left.
- Her stepper needs `loop()` to come round every few hundred microseconds. The game's
  Serial prints and the LED strip would make it stutter.
- The LED strip turns interrupts off for about 1 ms every time it updates, which would make
  the encoders miss counts.
- Both want pin 6.

**Why Tracking is on the Wheels Mega:** Tracking needs the AS5600 reading at the exact moment
the photo-interrupter fires (within 2 ms). The AS5600 is wired to the Wheels Mega, so
Tracking's sensors and code go there too. The game only needs the *result*: "player 0 was
hit" and "player 0 is at 123.4°".

## What runs where

| | Game Mega | Wheels Mega |
| --- | --- | --- |
| Sketch | `crater_crawl/crater_crawl.ino` | `crater_crawl_wheels/crater_crawl_wheels.ino` |
| Owns | rounds, lives, lockout, difficulty, pillars, LEDs | steering wheels, character motion, angles, hit switches, homing |
| USB Serial Monitor | the game console (`?`, `s`, `t`…) | Alice's telemetry line every 200 ms, `1`/`0` to test resistance |
| Talks to the other board with | `src/wheels_link.cpp` | `game_link.cpp` |

## What happens during a round

Here's one round, message by message. Lines starting `[1234]` are what the Game Mega's
Serial Monitor prints.

**1. Power on.** The Wheels Mega starts Alice's motors, then Tracking
(`tracking_begin()` in `tracking_hal.cpp`), then the link, and sends `B`. The Game Mega
prints `LINK Wheels Mega started`.

**2. Homing.** Tracking needs to know where "zero" is. It starts homing each player straight
away and reports `A 0 <angle> 1` (state 1 = homing) every 100 ms:
`LINK P0 tracking: homing`.

There's no motor code for homing. The player turns their steering wheel, the character
follows (Alice's stepper), and when it passes the photo-interrupter:

- The photo-interrupter's pin changes. A pin-change interrupt (`ISR(PCINT2_vect)` in
  `tracking_hal.cpp`) records the time. That's all the interrupt does.
- On the next pass of `loop()`, Tracking reads the AS5600 fresh, checks that the reading was
  taken within 2 ms of the crossing, and saves it as that player's zero.
- From now on the angle is "degrees past the photo-interrupter". The link reports state 2:
  `LINK P0 tracking: ready`, and `?` on the Game Mega shows the angle.

If nobody turns the wheel for 60 s, homing gives up (`homing failed`). Type `m0` on the
Game Mega to try again.

**3. Start.** Type `s`. Countdown 3-2-1, then RUNNING. Pillar logic is unchanged:

- The sequencer picks a pillar and starts its warning. The pillar FSM sends a `PillarCommand`.
- That command goes to the LED code (`pillarLedsHandleCommand` in `LEDGameLogic.cpp`):
  yellow flashing, then fast orange.
- When the warning ends the pillar rises (LEDs solid red), stays up for 1 s, then lowers
  (LEDs off).

**4. A hit.** Say pillar 3 is fully up and player 0's character is over it.

1. The raised pillar presses player 0's **hit switch**. By Tracking's design, it can only be
   pressed by a pillar that's fully up.
2. Wheels Mega: Tracking's `character_update()` sees the press. It's debounced, so one press
   is one hit even if the switch bounces or is held. `game_link.cpp` sends `H 0 -1`, where -1
   means "Tracking doesn't know which pillar".
3. Game Mega: `wheels_link.cpp` reads the line and picks the pillar: Tracking's number if it
   gave one, otherwise the pillar that's up right now (3). It builds a `CollisionEvent` and
   calls `game_on_collision()`. That still checks everything it always did: round running,
   player alive, not locked out, pillar fully up.
4. One hit is fatal for now (`PLAYER_LIVES 1`), so player 0 is out. The game sends a
   `WheelsTrigger` over the link: `T 0 1 0` (player 0, fatal, hold until reset).
5. Wheels Mega: `game_link.cpp` marks player 0 as held. Alice's sketch turns that into
   `resistEnabled = true`: her DC motor resists the steering wheel and the character stops.
6. Game Mega: `PLAYER_ELIMINATED`, the remaining pillars come down, then `GAME_OVER` and the
   summary.

**5. Reset.** Type `r`. The game sends `X`, the Wheels Mega releases the wheel, and you can
press `s` again.

A non-fatal hit (once `PLAYER_LIVES` is more than 1) sends `T 0 0 1200` instead. The Wheels
Mega freezes that player for 1.2 s and then lets go by itself, matching the game's 1.2 s
lockout.

## The link

Plain text lines, so you can watch them in a Serial Monitor. The format lives in
`board_link_protocol.h`. There are two identical copies, one per sketch, because the Arduino
IDE can't include a file from outside a sketch folder. **Change both together.**

| Direction | Message | Meaning |
| --- | --- | --- |
| Game → Wheels | `T <player> <fatal> <hold_ms>` | player was hit: freeze for `hold_ms`, or hold until `X` if fatal |
| Game → Wheels | `X` | release every player (sent on reset) |
| Game → Wheels | `M <player>` | start homing that player again |
| Wheels → Game | `B` | Wheels Mega just started |
| Wheels → Game | `H <player> <pillar>` | hit switch pressed; pillar = Tracking's number or -1 |
| Wheels → Game | `A <player> <angle_x10> <state>` | every 100 ms. state: 0 not homed, 1 homing, 2 ready, 3 homed but no angle, 4 homing failed |

The two clocks aren't synchronised, so the Game Mega times every hit by its own clock, when
the `H` arrives. If the Game Mega hears nothing for 1 s it prints
`LINK nothing from the Wheels Mega...`. Malformed lines are ignored and counted (shown by `?`).

The link runs at **9600 baud**, slower than the USB Serial Monitor (115200). That is on
purpose. FastLED turns interrupts off for about 0.9 ms each time it updates the strip, and at
115200 the Game Mega lost the bytes that arrived during that time. Lines came out chopped
(`A 0 12`) or joined (`A 0 1234 0`). A joined line can even look valid: that one reads as
"player 0 not homed". At 9600 a byte takes about 1 ms, so the serial port holds them until
interrupts are back on.

## Wiring

**Between the boards:** Game 16 → Wheels 17, Game 17 ← Wheels 16, GND ↔ GND.

**Game Mega**

| Pin | Use |
| --- | --- |
| 0, 1 | USB Serial: the console |
| 6 | LED strip data (Pillars). Power the strip from a 5 V supply and join its ground to the Mega's GND |
| 16, 17 | Serial2: link to the Wheels Mega |
| (TBD) | pillar motors: no driver code yet, commands are only logged |

**Wheels Mega.** Alice's pins weren't moved; everything new avoids them.

| Pin | Use | From |
| --- | --- | --- |
| 2, 3 | player 0 steering-wheel encoder (interrupts) | Alice (`WheelsPin.h`) |
| 6, 7, 8, 10 | player 0 DC motor driver: PWM, IN2, IN1, STBY | Alice |
| 52, 53 | player 0 character stepper: STEP, DIR | Alice |
| 20, 21 | I2C → TCA9548A mux → AS5600 (player 0 on channel 0) | Alice |
| 18, 19 | reserved for player 1's steering-wheel encoder | not wired yet |
| 16, 17 | Serial2: link to the Game Mega | new |
| A8, A9 | photo-interrupters, player 0 / 1 (pin-change interrupts) | new, Tracking |
| 22, 23 | hit switches, player 0 / 1 (`INPUT_PULLUP`, pressed = LOW) | new, Tracking |

Timers in use: Timer1 (TimerOne, Alice's resistance loop), so no PWM on pins 11 and 12.
Once her servo hit animation is connected, the Servo library also takes Timer5 (no PWM on
44, 45, 46).

## Files: what each does and who owns it

**Game sketch, `crater_crawl/`**

| File | What it does | Owner |
| --- | --- | --- |
| `crater_crawl.ino` | `setup()` / `loop()`: reads console lines, then link, game, console, LEDs every 20 ms | GL |
| `src/game_fsm.*` | phases, hits, lives, lockout, difficulty; sends the Wheels trigger | GL |
| `src/pillar_fsm.*`, `src/pillar_seq.*` | each pillar's warn/rise/hold/lower, and which pillar goes next | GL (Mahad) |
| `src/LEDGameLogic.cpp`, `src/pillar_led.h` | LED animations per pillar (`pillar_led_laptop.cpp` = do-nothing laptop version) | Pillars (Sakanan) |
| `src/wheels_link.*` | **new.** Game side of the link: hits in, angles in, triggers out | GL |
| `src/board_link_protocol.h` | **new.** The message format (copy 1 of 2) | GL |
| `src/console.*` | Serial commands and the arena view | GL |
| `src/debug_log.*` | all printing; `log_set_enabled(false)` silences everything | GL |
| `src/unit_tests.*` | **new.** On-board unit tests (`t`) | GL |
| `src/shared_state.*` | the interface contract | GL |

**Wheels sketch, `crater_crawl_wheels/`**

| File | What it does | Owner |
| --- | --- | --- |
| `crater_crawl_wheels.ino` | Alice's main sketch; a few added lines start and run Tracking and the link | Wheels (Alice) |
| `AS5600.*`, `DCMotor.*`, `MotorEncoder.*`, `WheelResistance.*`, `ServoMotor.*`, `WheelsPin.h` | Alice's hardware classes | Wheels |
| `tracking_hal.*` | **new.** Tracking on this board: both players' modules, their pins, and the hardware functions Tracking's code calls | Tracking (written by GL; please check) |
| `game_link.*` | **new.** Wheels side of the link | GL |
| `src/tracking/CharacterModule.*` | Tracking's code, moved here from `Photo Interrupt Sensor/` | Tracking |
| `src/board_link_protocol.h` | the message format (copy 2 of 2) | GL |

**Other:** `hardware_tests/` (checks below), `laptop/main.cpp` (runs the game sketch on a
computer), `Photo Interrupt Sensor/` (Tracking's PCB, tests and docs; their tests still pass
after the move).

**Changes to other people's code** were kept small:
- Alice's sketch got a `manualResist` flag (her `1`/`0` commands now set it), one line
  setting `resistEnabled` each loop, and calls to Tracking and the link.
- Sakanan's LED file got an `#ifdef ARDUINO` wrapper, so the laptop build still compiles.
- Mahad's pillar FSM passes each command to the LEDs.
- Tracking's `run_tests.py` got a path fix for the moved files.

## Game Mega console commands

Serial Monitor at 115200 baud, line ending "Newline".

| Command | Does |
| --- | --- |
| `s` / `r` | start a round / reset |
| `?` | status: phase, players, pillars, and the Wheels Mega link (last heard, each player's tracking state and angle) |
| `t` | run the unit tests |
| `m0`, `m1` | ask the Wheels Mega to home that player |
| `w<n>` | raise pillar n by hand (also a quick LED test) |
| `h<p>` | fake a hit on player p directly, skipping the link |
| `l <message>` | act as if the Wheels Mega sent `<message>`, e.g. `l H 0 -1` or `l A 0 900 2`. Lets you test the link logic with only one board |

## Testing on the boards, step by step

Do these in order; each one only needs what the steps before it have proven.

**0. Laptop first (optional):** `./test --test` should end with `ALL PASSED`.

**1. Game Mega alone.** Upload `crater_crawl`, open the Serial Monitor.
- Type `t`. After a few seconds it should end with `0 failed: ALL PASSED`. The number of
  checks can differ slightly from the laptop's, because the Mega's random pillar picks
  differ. A failure prints `FAIL at unit_tests.cpp line N`; look at that line.
- Type `?`: `Wheels Mega: not heard from yet` is expected with no second board.
- Type `s`, then `l H 0 -1` once a pillar shows `#` in the arena line. Player 0 should be
  eliminated. That's the whole hit path without the Wheels Mega.

**2. LED strip (Pillars).** Upload `hardware_tests/led_strip_check`, with data on pin 6. You
should see the whole strip red/green/blue, then each pillar lit in order 0–5, then pillar 0's
warning. Then put `crater_crawl` back on and type `s`: the LEDs should follow the pillars
(or use `w2` to raise pillar 2 by hand).

**3. The cable.** Upload `hardware_tests/link_check` to **both** boards and wire them up. Both
Serial Monitors should show `got: ping ...` twice a second.

**4. Tracking's inputs (Wheels Mega).** Upload `hardware_tests/tracking_inputs_check`. Block
each photo-interrupter and press each hit switch, and watch what prints.
- **Blocked should read HIGH.** If it reads LOW, change `REFERENCE_BLOCKED_LEVEL` in
  `tracking_hal.cpp`.
- The interrupt count should go up each time.

**5. Everything.** Upload `crater_crawl_wheels` to Alice's Mega and `crater_crawl` to the Game
Mega, connected by the cable.
1. Game console `?`: the Wheels Mega is heard, `P0 tracking: homing`.
2. Turn player 0's wheel until the character passes the photo-interrupter: `LINK P0 tracking: ready`.
3. `s`. When a pillar is up, press player 0's hit switch: player 0 is eliminated, and on the
   Wheels Mega's monitor `Resist=1` and the wheel pushes back.
4. `r`: `Resist=0`, and the wheel turns freely.

## Assumptions to confirm with each subteam

These are written into the code as single settings, so each is a one-line change.

**Tracking**
- Photo-interrupter reads **HIGH when blocked** (ITR9608 with a pull-up). Step 4 checks this.
- Pins A8/A9 for the photo-interrupters and 22/23 for the switches.
- Homing is done by the player turning their wheel; time limit 60 s.
- Angle readings may be up to 40 ms old (Alice reads the AS5600 every 20 ms; Tracking's
  default was 20 ms).

**Wheels**
- Hit = resistance on (character frozen) for 1.2 s; death = resistance on until reset. Her
  5-servo hit animation (`ServoMotor`) isn't connected yet: no servo pins or poses are set.
- Only player 0's hardware exists in her sketch. Player 1 needs its own motor pins, an encoder
  on 18/19, and the AS5600 on mux channel 1. Then set `PLAYER_WIRED[1] = true` in
  `tracking_hal.cpp`.
- `GEAR_RATIO 1.0`: the AS5600 angle is taken as the character's angle.

**Pillars**
- The LED strip is refreshed 50 times a second, not every loop, to keep interrupts on.
- No motor driver code for the pillars yet: `PillarCommand`s go to the LEDs and are logged.
- The warning durations (1500/1000/600 ms) are written in both `pillar_fsm.cpp` and
  `LEDGameLogic.cpp`. If they change, change both.

**Game logic**
- `s` doesn't wait for both players to be homed, because player 1 isn't wired yet. Add that
  check once both are.

## Known limits and next steps

- **RAM on the Game Mega is at 48%.** On the Mega all text lives in RAM, and the Arduino IDE
  warns at 75%. If it gets close, move the log messages into flash (`F()` / `PSTR`).
- Two copies of `board_link_protocol.h`: keep them identical.
- Player 1's hardware, the pillar motors and the servo hit animation are still to come.
- Still open from before: `ACTION_WARN_ONLY` is used for real warnings although
  `shared_state.h` defines it as "no rise behind it"; no `PillarCommand` for cancelling a
  warning on reset; placeholder timings outside `shared_state.h`; fake warnings switched off
  until the team decides.
