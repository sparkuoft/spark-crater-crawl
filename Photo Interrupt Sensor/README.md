# Character tracking (C)

`CharacterModule.c` and `CharacterController.c` implement the tracking logic in
C11 with fixed storage and no dynamic allocation. They do not include Arduino
or `shared_state.h`. C++ callers can include the headers directly because the
function declarations have `extern "C"` guards. Compile the `.c` files as C.

The physical pin mapping, encoder/mux driver, interrupt attachment and Wheels
transport are supplied through callbacks. No hardware driver or connection to
the existing game FSM/main loop is implemented here (those are the next step).

## Responsibilities

- One `CharacterModule` per player: encoder offset/angle validity, nonblocking
  homing, reference refresh and a queue of pillar-switch contact events.
- One `CharacterController` for the two players: updates both modules, validates
  the angle pair, detects circular proximity, and optionally requests a paired
  movement response. It never changes lives, lockouts or the game phase.
- The future game adapter publishes valid angles and drains both players'
  pillar queues before calling `game_fsm_update()`. Player-pair notifications
  are separate from pillar hits and must not be sent to `game_on_collision()`.

## Public API and initialization

Start with zero-initialized module/controller storage. Obtain a config with
`character_default_config()`, set player ID, actual encoder channel and hardware
callbacks, and check `character_init()`'s bool result. Initialization validates
configuration but does not access hardware or start motors. A failed init does
not modify the destination. Do not reinitialize active modules/controllers.

Provide all `CharacterHardware` callbacks except `identify_pillar`, which is
optional. A context pointer can hold the platform's player-to-pin and mux maps.
The input callbacks return logical **active/pressed**, so wiring polarity and
pull-up choices belong in the hardware adapter. Callbacks must be bounded and
nonblocking enough to meet the application's loop timing requirements.

Call `character_start_homing(player, now_ms)` explicitly. The start callback
must start controlled homing and report whether it succeeded; the stop callback
must stop it. Homing stops on success, timeout, reference-input failure or
`character_cancel_homing()`. Starting with the reference blocked requires a
clear observation followed by a new activation edge. It does not zero at an
arbitrary point inside the blocked region. A timeout/failure needs an explicit
retry; no automatic motor restart occurs. An encoder failure cannot produce a
valid angle and an unsuccessful homing attempt eventually times out.

Use `character_controller_update(controller, now_ms, collisions_enabled)` once
per loop. It calls both modules' updates; do not update them a second time.
Pass false during homing or when game rules forbid pair responses. Pillar
detection and reference tracking continue independently of that flag.

`character_get_angle()` returns a cached angle, not a fresh hardware read. Check
`character_angle_is_valid()` before using it. `character_is_calibrated()` only
means an offset was established: a later read failure invalidates the angle
without discarding that offset. The last valid cache is retained for diagnostics.

## Angle and reference contract

All angles are tenths of a degree, normalized to 0..3599. The adapter converts
raw encoder counts/gearing into an absolute character-ring angle. Configure
`encoder_sign` as +1 or -1 so both players use the same physical positive
direction and the same physical zero:

```text
angle_x10 = wrap3600(encoder_sign * (raw_angle_x10 - reference_offset_x10))
```

Both encoder callbacks receive the same configured mux channel. Normal reads
return a timestamped sample acquired at or before `now_ms`, within
`max_sample_age_ms`. Typically the adapter provides a recent cached acquisition
and the application timestamps its update after acquisition. Invalid/out-of-
range, stale or future samples do not update the cache or enable collisions.

Bind each hardware reference ACTIVATION interrupt to
`character_reference_isr(&player, crossing_ms)`. This only writes a first-edge
mailbox. Repeated edges while pending cannot replace the timestamp. The main
loop atomically takes the mailbox using the supplied critical-section callbacks;
on Mega these must save/disable/restore interrupt state. **No-op critical
callbacks are valid only in the single-threaded host tests.** Attach interrupts
after successful initialization and detach them before reinitializing/freeing
the module. The same module's ISR must not reenter itself.

`read_reference_sample` must return an actual acquisition near the crossing,
or a sample reconstructed at that time from sufficiently close samples. It must
not stamp a delayed reading with the old crossing timestamp. The module checks
the event age, sample timestamp and crossing/sample tolerance, rejects missing
or late data, and increments `rejected_reference_events`. It cannot verify the
truthfulness of a hardware adapter's timestamp. The adapter must document and
measure its acquisition/reconstruction error; the tests do not establish real
hardware timing. No I2C/mux operations run inside the ISR or critical section.

After calibration the reference is disarmed. It rearms only when the sensor is
clear and circular distance from zero is **strictly greater** than the buffer.
359 degrees is 1 degree from zero. Activation direction/flag width compensation
is deliberately deferred. Each accepted reference increments
`calibration_revision`, so the controller can distinguish an offset jump from
physical approach.

All timestamps use the same unsigned 32-bit millisecond clock. Duration checks
handle rollover; configured intervals are limited to `INT32_MAX`. Service the
loop regularly; timing bounds are not guarantees if callbacks or the caller
block. For sub-millisecond accuracy a finer hardware sampling strategy may be
necessary before integration.

## Pillar contacts

The first observed press is accepted immediately when armed, even if the angle
is unavailable. The switch remains latched while held and through release
bounce; only a stable released interval rearms it. This strategy does not filter
isolated noise pulses. By default an initially pressed switch is ignored until
stable release; `accept_initial_press` can change that policy.

Switches are polled. `hit_ms` is the first observation time, not an ISR edge
timestamp. The maximum loop interval must be shorter than the shortest physical
contact. If that cannot be demonstrated, add hardware edge capture in the
adapter before deployment; this implementation does not claim to capture
contacts entirely between polls. Failed switch reads require stable release
before accepting another press.

`character_read_pillar_collision()` consumes the oldest of four pending events.
Overflow preserves the oldest events and increments `dropped_pillar_events`.
Drain the queue each loop. Switches/queues are independent, so simultaneous hits
remain available for both players.

Attribution uses an optional direct `identify_pillar` callback first. Otherwise,
it uses a valid angle and a measured `pillar_angles_x10` table. Exactly one
pillar must match the tolerance; no match, overlapping matches or unavailable
angles produce `CHARACTER_UNKNOWN_PILLAR` (-1). This never suppresses the hit.
Do not cast -1 blindly into the game's uint8_t pillar ID: the future GL adapter
must agree an unknown-ID contract. Pillar phase/top-limit readings do not gate
contact detection.

## Player-pair collisions

The controller compares both fresh, valid angles only when their acquisition
timestamps are within `max_pair_sample_skew_ms`. It reports once at separation
**<= enter_x10**, and rearms at **> release_x10**. Initial overlap is suppressed
by default until players separate. That baseline policy also applies after
disabled detection or invalid/skewed data; `detect_initial_overlap=true` opts
into reporting overlap on those resumptions too.

When recalibration changes an established angle pair, no collision is reported
on that update. If the corrected pair is inside the release zone, separation is
required before another collision. This deliberately avoids interpreting an
offset correction as contact; it can suppress a real encounter coincident with
recalibration and should be evaluated in physical testing.

The pair event contains both positions and incoming directions captured before
any movement request. `character_controller_read_collision()` consumes the
pending pair notification. Each module also exposes a consuming
`character_read_character_collision()` notification. These are observations of
one encounter: **do not issue motor commands again from the per-player reads**.
Undrained notifications preserve the oldest event and expose overflow counters.

Default behavior is detection only. To opt into the proposed separation policy,
set `choose_response = character_response_separate`, supply `read_direction`
and `apply_response`, or supply your own policy. The built-in policy assigns
outgoing directions away from relative positions, handles head-on motion,
requires observed approach for same-direction catch-up, and declines already-
separating motion. At exact coincidence it uses previous ordering; if none is
available it declines. Unavailable directions also prevent a motor request.
An accepted policy calls `apply_response` once with both players. The adapter
must honor game eligibility, motor limits and command failures. The physical
response policy remains a team/playtesting decision.

## Provisional defaults

| Setting | Default |
| --- | --- |
| Reference rearm buffer | 45 degrees on either side of zero |
| Stable switch release | 20 ms |
| Homing timeout | 10 seconds |
| Maximum encoder sample age | 20 ms |
| Reference sample/crossing tolerance | 2 ms |
| Maximum queued reference event age | 20 ms |
| Pair entry / release thresholds | 5 / 10 degrees |
| Maximum pair sample skew | 5 ms |
| Initial switch press / initial pair overlap | Suppressed |
| Automatic pair movement response | Disabled |

These are starting values, not measurements. Tune acquisition latency against
maximum angular speed, and loop/debounce timing against measured contact width.

## Host verification from the VS Code PowerShell terminal

From the repository root with Python, GCC and G++ on PATH:

```powershell
python '.\Photo Interrupt Sensor\tests\run_tests.py'
```

The script compiles both production sources as C11 with warnings as errors,
runs 14 behavioral test groups, and links a C++ smoke test to the separately
compiled C objects. Outputs go into ignored `build/tracking-tests/`. It does
not build the incomplete game application or upload firmware. Compiler paths
can be passed with `--cc` and `--cxx`. The Python runner also works from Windows
PowerShell installations that restrict execution of `.ps1` scripts, without
changing the shell's execution policy.

The VS Code `windows-gcc-x64` IntelliSense configuration uses this computer's
MSYS2 GCC; the existing macOS configuration is preserved. Select the Windows
configuration locally if VS Code still reports `/usr/bin/clang` errors. Editor
IntelliSense selection does not change how the test script compiles the files.
