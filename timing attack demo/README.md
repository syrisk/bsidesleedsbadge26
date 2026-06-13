# Timing Attack — a side-channel teaching badge

Standalone firmware for the **BSides Leeds 2026 “Artie the Owl” badge** that turns it
into a hands-on demonstration of a **timing side-channel attack**. The badge guards a
secret PIN and checks your guesses with a deliberately vulnerable comparison whose
*duration* leaks how close you are. Notice the leak and you crack the PIN in a handful
of tries instead of brute force.

Built for the badge by [Punk Security](https://github.com/punk-security/bsides-leeds-2026-badge).
This is standalone firmware — it replaces the games/animations with this single demo.

## The idea in one paragraph

The badge holds a 4-digit PIN where each digit is one of three colours (red, green,
blue) — 3^4 = **81 combinations**. A naive attacker brute-forces it in ~41 guesses on
average. But the badge’s check compares your guess one digit at a time and **stops at
the first wrong digit**, blinking a dim white “thinking” pulse once per comparison. The
number of pulses tells you how many leading digits you got right. Read that signal and
you can solve the PIN digit-by-digit in **~12 guesses**. Same “Access Denied” every
time — only the *time taken* differs. That’s a timing side-channel.

## How to play

1. Power on. An **orange double-flash** means a new secret PIN has been armed.
1. Enter a 4-digit guess by tapping colour pads (either eye’s pads work):
- **Red**, **Green**, or **Blue** — each tap fills the next slot, shown on the eyes.
- The **button** clears a half-entered guess so you can start the guess over.
1. After 4 digits, the badge checks your guess:
- It blinks a **dim white pulse for each digit it compares** before stopping.
- **Green flashes** = cracked. **Red flashes** = denied (looks identical every time).
1. On a crack, the badge lights **one green LED per guess you used** — fewer lit means
   you used the leak well. Then it arms a fresh PIN and you go again.

### Reading the leak

|White pulses before denial|What it means                            |
|--------------------------|-----------------------------------------|
|1                         |First digit wrong                        |
|2                         |First digit right, second wrong          |
|3                         |First two right, third wrong             |
|4                         |First three right (only the 4th is wrong)|

So: fix the last three digits, cycle the first through red/green/blue and watch for the
guess that earns **2 pulses** — that’s digit 1. Repeat for each position. Digits 1-3
fall to the pulse count; digit 4 you get from the final green/red. Roughly 3 guesses per
digit ≈ 12 total.

## Controls

|Action                  |Result                                |
|------------------------|--------------------------------------|
|Tap a colour pad        |Enter that colour as the next digit   |
|Tap the button          |Clear the current (half-entered) guess|
|Hold the button > 1.2 s |Sleep                                 |
|Press button after sleep|Wake (restarts with a new PIN)        |
|Idle 15 minutes         |Auto-sleep                            |

## Why this works (the security lesson)

The vulnerable check is the whole point:

```c
for (uint8_t i = 0; i < PIN_LENGTH; ++i) {
  thinkPulse();                            // time spent is observable
  if (guess[i] != secret[i]) { full = false; break; }   // early return
}
```

Two real-world bugs are baked in:

- **Early return** — bailing on the first mismatch makes runtime depend on the secret.
- **Observable timing** — the work done is measurable from outside (here, by eye).

This is the same class of flaw as non-constant-time string/MAC comparison (`memcmp` on
a token, `==` on a password hash). The fix is **constant-time comparison**: always do
the same work regardless of input. To show the fix on the badge, pad every check to a
fixed 4 pulses regardless of where the mismatch is — the leak disappears and brute force
is all that’s left. (See *Extensions*.)

> Pedagogical note: the leak is rendered as **countable pulses** rather than raw
> elapsed time, because people distinguish “3 blinks vs 4 blinks” far better than
> “900 ms vs 1200 ms”. It’s still a pure timing channel — pulse count equals the number
> of comparisons performed — and it shows no colours or counter, so players still have
> to *notice* it rather than being handed an oracle.

## Building

Compiled and size-checked with:

- **arduino-cli** + [megaTinyCore](https://github.com/SpenceKonde/megaTinyCore) **2.6.11**
- Board: *ATtiny3224/1624/1614/1604/824/814/804/…* -> **Chip: ATtiny814**
- Default board options; libraries `PTC` and `tinyNeoPixel_Static` ship with megaTinyCore

```sh
arduino-cli core update-index \
  --additional-urls http://drazzy.com/package_drazzy.com_index.json
arduino-cli core install megaTinyCore:megaavr \
  --additional-urls http://drazzy.com/package_drazzy.com_index.json
arduino-cli compile -b megaTinyCore:megaavr:atxy4:chip=814 .
```

Expected output:

```
Sketch uses 4268 bytes (52%) of program storage space. Maximum is 8192 bytes.
Global variables use 247 bytes (48%) of dynamic memory.
```

Flash via UPDI as usual for this badge.

## Tuning

In the `Game tuning` block near the top:

|Constant                      |Effect                                                      |
|------------------------------|------------------------------------------------------------|
|`PIN_LENGTH`                  |Number of digits (raises difficulty fast — 3^n search space)|
|`THINK_ON_MS` / `THINK_OFF_MS`|Pulse on/off time — make the leak easier/harder to read     |
|`IDLE_SLEEP_MS`               |Auto-sleep timeout                                          |

The search space is 3^`PIN_LENGTH`, and three colours come from the three pad colours;
adding more colours would need a different input scheme.

## Extensions (room to grow — uses only ~52% of flash)

- **Hardened mode** — after the player cracks the vulnerable PIN, replay the same game
  with a constant-time check (always 4 pulses). Watching the leak vanish is the payoff.
- **Best score** — persist fewest-guesses-to-crack in EEPROM and show it on arm.
- **Subtle mode** — replace pulses with a single steady dim-white hold of
  `comparisons × UNIT_MS`, for a duration-only channel that’s harder to read.

## Hardware

|     |                                                               |
|-----|---------------------------------------------------------------|
|MCU  |ATtiny814 (8 KB flash, 512 B SRAM)                             |
|LEDs |18 addressable RGB (two 9-LED “eyes”)                          |
|Input|6 capacitive touch pads (red/green/blue per eye) + 1 button    |
|Sleep|Power-down after 15 min idle or long button press; button wakes|

## Credits

- Badge hardware and platform: **Punk Security** —
  [punk-security/bsides-leeds-2026-badge](https://github.com/punk-security/bsides-leeds-2026-badge)
- Timing-attack demo firmware: Syrisk project

This is a teaching tool. The “vulnerability” is intentional — that’s the lesson.
