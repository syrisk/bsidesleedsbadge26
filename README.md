# BSides Leeds 2026 Badge — Firmware Collection

A set of alternative firmwares for **Artie the Owl**, the BSides Leeds 2026
conference badge by [Punk Security](https://github.com/punk-security/bsides-leeds-2026-badge).
Each one re-flashes the badge to do something different: a beefed-up version of
the stock games, or a standalone security-themed teaching demo.

Every firmware here targets the same hardware and flashes the same way — see
[Hardware](#hardware) and [Building & flashing](#building--flashing) below.
Each project folder also has its own README with the full details.

> **Original badge & hardware:** Punk Security. These are community-modified
> firmwares — flash them at your own risk, and keep a copy of the stock firmware
> if you want to go back.

## What’s in here

|Folder                                          |Firmware         |What it does                                                                                                                                                                 |Flash use   |
|------------------------------------------------|-----------------|-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------|------------|
|[`hackedoriginal`](./hackedoriginal)            |Syrisk Edition   |Modified stock firmware: adds the **GuitarZero** rhythm game, unlocks all hidden LED animations from boot, drops one two-player game to make room                            |8064 B (98%)|
|[`knock lock`](./knock%20lock)                  |Secret Knock Lock|Turns the badge into a tempo-independent **rhythm lock** — tap a secret rhythm to unlock; reprogrammable in the field, secret stored in EEPROM                               |4716 B (57%)|
|[`timing attack demo`](./timing%20attack%20demo)|Timing Attack    |A hands-on **side-channel demo**: guards a colour PIN with a deliberately vulnerable check whose duration leaks how close you are, so it cracks in ~12 guesses instead of ~41|4268 B (52%)|

The two standalone demos (knock lock, timing attack) each *replace* the games
and animations entirely. `hackedoriginal` keeps the stock feature set and extends
it. Pick one and flash it; they aren’t meant to coexist.

### `hackedoriginal` — Syrisk Edition

A fork of the stock badge firmware that:

- Adds **GuitarZero**, a rhythm game played on both eye-rings (tap the matching
  colour pad as notes reach the hit zone).
- **Unlocks every LED animation** from first boot — the stock firmware gated four
  of them (DevSecOps infinity, two nuclear variants, police) behind game wins.
- Removes the two-player *Find the Sequence* game to free the flash the new game
  needed (the build lands at 98% with 128 bytes to spare).

Game wins are still recorded to EEPROM as a personal scoreboard; they just no
longer gate anything.

### `knock lock` — Secret Knock Lock

Tap a rhythm on any touch pad; if its *shape* matches the stored secret, the badge
unlocks. Matching is **tempo-independent** — gaps between taps are normalized so
the longest equals 100, so the same rhythm works fast or slow. Ships with a default
rhythm and can be reprogrammed on the badge (no recompile) via a long button press.

> It’s a party trick and a teaching toy, not real security — a short human-tapped
> rhythm has little entropy and is easy to shoulder-surf.

### `timing attack demo` — Timing Attack

The badge guards a 4-digit colour PIN (red/green/blue per digit, 81 combinations)
and checks guesses with a comparison that **stops at the first wrong digit**,
blinking one dim white pulse per comparison. The pulse count reveals how many
leading digits are right, so you can solve the PIN digit-by-digit. Same “Access
Denied” every time — only the *time taken* differs. A live demonstration of why
non-constant-time comparison leaks.

## Hardware

All three firmwares run on the stock badge unmodified:

|           |                                                                 |
|-----------|-----------------------------------------------------------------|
|MCU        |ATtiny814 (8 KB flash, 512 B SRAM)                               |
|LEDs       |18 addressable RGB (two 9-LED “eyes”)                            |
|Input      |6 capacitive touch pads (red/green/blue per eye) + 1 wake button |
|Persistence|EEPROM (game progress / knock secret, depending on firmware)     |
|Sleep      |Power-down after 15 min idle or a long button press; button wakes|

## Building & flashing

Each folder is a self-contained Arduino sketch (`firmware.ino`). All were compiled
and size-checked with the same toolchain:

- **arduino-cli** with [megaTinyCore](https://github.com/SpenceKonde/megaTinyCore) **2.6.11**
- Board: *ATtiny3224/1624/1614/1604/824/814/804/…* → **Chip: ATtiny814**
- All other board options at defaults
- Libraries `EEPROM`, `PTC`, and `tinyNeoPixel_Static` ship with megaTinyCore

```sh
# one-time core setup
arduino-cli core update-index \
  --additional-urls http://drazzy.com/package_drazzy.com_index.json
arduino-cli core install megaTinyCore:megaavr \
  --additional-urls http://drazzy.com/package_drazzy.com_index.json

# compile a firmware (run from inside the folder you want)
arduino-cli compile -b megaTinyCore:megaavr:atxy4:chip=814 .
```

Flash the result over **UPDI**, as usual for this badge.

## Credits

- Badge hardware, original firmware, games, and animations: **Punk Security** —
  [punk-security/bsides-leeds-2026-badge](https://github.com/punk-security/bsides-leeds-2026-badge)
- GuitarZero, the knock lock, and the timing-attack demo: Syrisk project

The knock lock and timing-attack firmwares are teaching tools — their weaknesses
are intentional, and that’s the lesson.