# BSides Leeds 2026 Badge — Syrisk Edition

Modified firmware for **Artie the Owl**, the BSides Leeds 2026 conference badge by
[Punk Security](https://github.com/punk-security/bsides-leeds-2026-badge). This fork
adds a new rhythm game (kinda like a crappy version of GuitarHero), unlocks all hidden LED animations from the start, and trades
one two-player game for the flash space to make it all fit.

> **Original badge & hardware:** Punk Security. This is a community-modified firmware —
> flash it at your own risk, and keep a copy of the stock firmware if you want to go back.

## Hardware

|           |                                                                |
|-----------|----------------------------------------------------------------|
|MCU        |ATtiny814 (8 KB flash, 512 B SRAM)                              |
|LEDs       |18 addressable RGB (two 9-LED “eyes”)                           |
|Input      |6 capacitive touch pads (red/green/blue per eye) + 1 wake button|
|Persistence|EEPROM byte 0 stores game progress                              |
|Power      |Deep sleep after 15 min idle; wake button reboots via watchdog  |

## What’s different from stock firmware

- **New game: GuitarZero** — a rhythm game played on both eye-rings (details below).
- **All animations unlocked** — the stock firmware hides four animations (devsecops
  infinity, two nuclear variants, police) until you beat the games. The gates are
  removed; every animation is in the cycle from first boot.
- **Two-player Find the Sequence removed** — its menu entry is deleted so the linker
  strips the code. This is what pays for GuitarZero (see *Flash budget*). The right-eye
  **red** pad therefore does nothing in the menu — that’s intentional, not a fault.
- Game wins are still recorded to EEPROM (including a new bit for GuitarZero), they just
  no longer gate anything.

## Controls

The single button is contextual — what it does depends on which pads you’re holding
when you tap it.

|Action                     |Result                    |
|---------------------------|--------------------------|
|Tap button (no pads held)  |Next animation            |
|Hold button > 1.2 s        |Red flash, then deep sleep|
|Press button after sleep   |Wake                      |
|Hold a pad **+** tap button|Launch a game (below)     |

|Pads held                   |Game                            |
|----------------------------|--------------------------------|
|Left **blue**               |Stop the Light (solo)           |
|Left **red**                |Find the Sequence (solo)        |
|Left **green**              |Follow the Sequence (solo)      |
|Left **red + blue** together|**GuitarZero** (new)              |
|Right **blue**              |Stop the Light (two-player)     |
|Right **green**             |Follow the Sequence (two-player)|
|Right **red**               |— (removed in this fork)        |

During games, the button reboots the badge (quick exit).

## Games

### GuitarZero (new)

Coloured notes scroll around both eye-rings toward a dimly lit **hit zone**. When a
note reaches the zone, tap the matching colour pad — either eye’s pad counts.

- **20 notes** per song, **5 lives**.
- Correct tap: green blip, note cleared. Wrong colour on a live note: red blip, note
  burned, lose a life. Note slips past untouched: lose a life.
- Tapping when the zone is empty is harmless (red blip only) — but mashing won’t help,
  since a wrong colour on a real note costs you.
- Tempo ramps from ~320 ms per step down to 150 ms as the song progresses.

Win with a life left and the badge flashes green and records the win (EEPROM bit 3).

### Stop the Light (solo / 2P)

A red dot races around the ring; tap any pad to stop it on the green target. Each
level is faster. Two-player runs both eyes in parallel — survive longer than your
opponent.

### Follow the Sequence (solo / 2P)

Simon-style: the eyes flash a colour sequence, you repeat it on the pads. Starts at
3 colours, grows to 10. Two-player gives each side its own sequence.

### Find the Sequence (solo)

A hidden 7-colour sequence is previewed for 3 seconds, then you reconstruct it from
memory, one colour at a time. A wrong guess resets your progress and costs one of
10 lives.

## Animations

Tap the button (no pads) to cycle. All modes are available from first boot.

|Mode|Animation                             |
|----|--------------------------------------|
|0   |Knight Rider (green)                  |
|1   |Breathing (red)                       |
|2   |Looping eyes (blue)                   |
|3   |Knight Rider (red)                    |
|4   |Looping eyes (red)                    |
|5   |DevSecOps infinity loop (boot default)|
|6   |Nuclear (green)                       |
|7   |Nuclear “York rose” (red/white)       |
|8   |Police (red/blue)                     |
|9   |Spin (purple)                         |
|10  |9-minute visual timer¹                |

¹ Left eye counts completed minutes, right eye fills through the current minute;
3-second red countdown to start, 5-second green flash when done.

## Progress tracking (EEPROM)

Byte 0 of EEPROM holds a progress bitfield. Bits start at 1 (factory EEPROM reads
0xFF) and are **cleared** when you win:

|Bit|Cleared by winning        |
|---|--------------------------|
|0  |Stop the Light (solo)     |
|1  |Follow the Sequence (solo)|
|2  |Find the Sequence (solo)  |
|3  |GuitarZero                |

In stock firmware these bits gated the hidden animations. Here they’re recorded but
gate nothing — they survive power-off, so they still work as a personal scoreboard.

## Building

Compiled and size-verified with:

- **Arduino IDE / arduino-cli** with [megaTinyCore](https://github.com/SpenceKonde/megaTinyCore) **2.6.11**
- Board: *ATtiny3224/1624/1614/1604/824/814/804/…* → **Chip: ATtiny814**
- All other board options at defaults
- Libraries `EEPROM`, `PTC`, and `tinyNeoPixel_Static` are bundled with megaTinyCore

```sh
arduino-cli core update-index \
  --additional-urls http://drazzy.com/package_drazzy.com_index.json
arduino-cli core install megaTinyCore:megaavr \
  --additional-urls http://drazzy.com/package_drazzy.com_index.json
arduino-cli compile -b megaTinyCore:megaavr:atxy4:chip=814 .
```

Expected output:

```
Sketch uses 8064 bytes (98%) of program storage space. Maximum is 8192 bytes.
Global variables use 240 bytes (46%) of dynamic memory.
```

Flash via UPDI as usual for this badge.

## Flash budget

The ATtiny814 has 8 KB of flash and this firmware uses **8064 bytes, leaving 128
free**. That margin was hard-won — measured numbers from the trim:

|Variant                                  |Flash   |vs 8192      |
|-----------------------------------------|--------|-------------|
|Stock-style build, GuitarZero as dead code |8126    |+66 free     |
|GuitarZero wired into the menu             |8636    |−444 **over**|
|+ reuse spin LED tables (drop duplicates)|8618    |−426 over    |
|+ remove 2P Find the Sequence menu entry |**8064**|**+128 free**|

Cutting animations instead doesn’t work: they share all their LED helpers, so even
removing five modes only clawed back ~370 bytes. Dropping one game subtree was the
only single change big enough. The 2P Find the Sequence *code* is still present in
the source as dead code — restore its three-line menu case if you ever free ~430
bytes elsewhere.

Anything you add must fit in 128 bytes, which in practice means: don’t add features
without removing something first.

## Credits

- Badge hardware, original firmware, games, and animations: **Punk Security** —
  [punk-security/bsides-leeds-2026-badge](https://github.com/punk-security/bsides-leeds-2026-badge)
- GuitarZero game and this build: Syrisk modification
