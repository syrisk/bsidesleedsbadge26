# Secret Knock Lock

Standalone firmware for the **BSides Leeds 2026 “Artie the Owl” badge** that turns it
into a **rhythm-based lock**. Tap a secret rhythm on any touch pad; if it matches the
stored pattern, the badge unlocks. The match is tempo-independent — knock the rhythm
fast or slow, only its *shape* matters — and the secret lives in EEPROM, so every badge
can carry its own knock with no recompile.

Built for the badge by [Punk Security](https://github.com/punk-security/bsides-leeds-2026-badge).
This is standalone firmware — it replaces the games/animations with this single demo.

> **Reality check:** this is a party trick and a teaching toy, not real security. A
> short, human-reproducible rhythm has very little entropy and is easy to shoulder-surf.
> Treat it as “prove it’s my badge,” not as a lock you’d trust anything to.

## How it works

1. You tap a rhythm on any capacitive pad. The badge records the time of each tap.
1. It computes the **gaps between taps** and **normalizes them** so the longest gap
   equals 100. That normalization is the trick: it removes overall speed, leaving only
   the rhythm’s proportions, so the same rhythm matches whether you knock it fast or slow.
1. A knock ends after a short silence. The badge then compares your normalized gaps to
   the stored secret: it must have the **same number of taps** and **every gap within
   tolerance**. Match → unlock; otherwise → reject.

There’s no vibration sensor on this badge, so a “knock” is a tap on any of the six touch
pads — which pad doesn’t matter, only the timing does.

## Using it

- **Unlock:** tap your rhythm on any pad. Each tap gives a brief white blip so you know
  it registered. Green flashes + a held green glow = unlocked. Red flashes = rejected.
- **Out of the box** it ships with a default rhythm: **“da-da-daah-da”** (five taps,
  three even beats, a longer gap, then one more).
- **Set your own knock:** hold the button ~1.2 s until the eyes glow **orange** (program
  mode), then tap your new rhythm. Orange flashes confirm it’s saved to EEPROM.
- **Sleep:** hold the button ~3 s, or leave it idle for 15 minutes. The button wakes it.

## Controls

|Action                  |Result                                        |
|------------------------|----------------------------------------------|
|Tap any pad             |Register a knock (starts/continues a rhythm)  |
|Hold button ~1.2 s      |Enter program mode (record a new secret knock)|
|Hold button ~3 s        |Sleep                                         |
|Press button after sleep|Wake                                          |
|Idle 15 minutes         |Auto-sleep                                    |

## The matching algorithm

```c
// gaps[i] = time between tap i and tap i+1
// normalize so the longest gap == 100  -> tempo independent
norm[i] = gaps[i] * 100 / maxGap;

// a knock matches if it has the same tap count and every gap is close enough
if (taps != secretTaps) return false;
for (i...) if (abs(norm[i] - secret[i]) > REJECT_TOLERANCE) return false;
return true;
```

Two things make or break it:

- **Normalization** gives tempo independence — the secret is the *ratio* of gaps, not
  their absolute length.
- **`REJECT_TOLERANCE`** is the security/usability dial. Lower = stricter and harder to
  reproduce; higher = forgiving but easier to fake. Default is 25 (on the 0–100 scale).

## Storage (EEPROM)

The secret persists across power-off and sleep:

|Byte|Contents                                                                  |
|----|--------------------------------------------------------------------------|
|0   |Tap count (2–`MAX_TAPS`); anything else means “none set” → use the default|
|1 … |Normalized gaps, one byte each (`tap count − 1` of them)                  |

A fresh chip reads 0xFF at byte 0, which is out of range, so the badge falls back to the
built-in default rhythm until you program one.

## Tuning

All near the top of the sketch:

|Constant                              |Default    |Effect                                    |
|--------------------------------------|-----------|------------------------------------------|
|`REJECT_TOLERANCE`                    |25         |Per-gap match tolerance (lower = stricter)|
|`KNOCK_COMPLETE_MS`                   |1200       |Silence that ends a knock                 |
|`MIN_TAP_GAP_MS`                      |60         |Debounce between taps                     |
|`MAX_TAPS`                            |12         |Longest accepted rhythm                   |
|`LONG_PRESS_MS` / `VERY_LONG_PRESS_MS`|1200 / 3000|Program-mode / sleep thresholds           |
|`IDLE_SLEEP_MS`                       |900000     |Auto-sleep timeout (15 min)               |

## Building

Compiled and size-checked with:

- **arduino-cli** + [megaTinyCore](https://github.com/SpenceKonde/megaTinyCore) **2.6.11**
- Board: *ATtiny3224/1624/1614/1604/824/814/804/…* → **Chip: ATtiny814**
- Default board options; libraries `EEPROM`, `PTC`, and `tinyNeoPixel_Static` ship with megaTinyCore

```sh
arduino-cli core update-index \
  --additional-urls http://drazzy.com/package_drazzy.com_index.json
arduino-cli core install megaTinyCore:megaavr \
  --additional-urls http://drazzy.com/package_drazzy.com_index.json
arduino-cli compile -b megaTinyCore:megaavr:atxy4:chip=814 .
```

Expected output:

```
Sketch uses 4716 bytes (57%) of program storage space. Maximum is 8192 bytes.
Global variables use 241 bytes (47%) of dynamic memory.
```

Flash via UPDI as usual for this badge.

## Extensions (~3.5 KB of flash free)

- **Multiple knocks** — store a few patterns that each trigger a different animation.
- **Lockout** — after N failed attempts, refuse and flash for a cooldown period.
- **Leaky-compare demo** — pair with the timing-attack firmware: make the match bail on
  the first bad gap with visible timing, so people can see this comparison leaks too.
- **Knock-to-share** — light a QR-ish pattern or a colour code on a successful unlock.

## Hardware

|     |                                                                 |
|-----|-----------------------------------------------------------------|
|MCU  |ATtiny814 (8 KB flash, 512 B SRAM)                               |
|LEDs |18 addressable RGB (two 9-LED “eyes”)                            |
|Input|6 capacitive touch pads + 1 button                               |
|Sleep|Power-down after 15 min idle or a long button press; button wakes|

## Credits

- Badge hardware and platform: **Punk Security** —
  [punk-security/bsides-leeds-2026-badge](https://github.com/punk-security/bsides-leeds-2026-badge)
- Secret knock lock firmware: community project
- Knock-matching approach inspired by the classic Arduino “secret knock” detector
  (normalize inter-event timing, compare within tolerance).

This is a teaching tool — the lock is meant to be fun and instructive, not strong.