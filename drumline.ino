// ============================================================================
//  DRUMLINE  -  a rhythm mini-game for the BSides Leeds 2026 "Artie" badge
// ============================================================================
//
//  Notes (colours) scroll around the eye-rings toward a fixed "hit zone" LED.
//  Tap the matching colour pad while a note sits in the hit zone to clear it.
//  Wrong colour, or letting a note slip past, costs a life. Survive the song.
//
//  Reuses helpers already in firmware.ino: colorForIndex, setAllLeds,
//  getPressedTouchMask, randomColorIndex, enableRebootOnButton,
//  disableRebootOnButton, showSuccess, showFailure, the *_MASK constants,
//  RgbColor and the COLOR_* palette.
//
//  PLACEMENT: paste this whole block into firmware.ino just after
//  playFindTheSequenceTwoPlayer() and before the animation section.
//  Everything it needs from the file is already defined above that point.
//
//  THREE integration edits are listed at the bottom of this file.
// ============================================================================

static const uint8_t DRUMLINE_LANE_LENGTH = 9;   // one eye-ring = 9 positions
static const uint8_t DRUMLINE_SONG_NOTES   = 20;  // notes to clear to win
static const uint8_t DRUMLINE_LIVES        = 5;   // misses + wrong taps allowed
static const uint8_t DRUMLINE_SPAWN_GAP    = 2;   // empty ticks between notes
static const uint16_t DRUMLINE_START_TICK_MS = 320; // slow at first...
static const uint16_t DRUMLINE_MIN_TICK_MS   = 150; // ...ramps to this

// Ring order, hit zone first (index 0). Same physical order as the spin
// animation, so notes appear to circle into the target. If you place this
// block AFTER the SPIN_LEDS_LEFT / SPIN_LEDS_RIGHT arrays you can delete these
// two and use those instead to save ~18 bytes of flash.
const uint8_t DRUMLINE_LEFT_ORDER[] PROGMEM  = { 7, 8, 0, 1, 2, 3, 4, 5, 6 };
const uint8_t DRUMLINE_RIGHT_ORDER[] PROGMEM = { 10, 9, 17, 16, 15, 14, 13, 12, 11 };

// Reduce a (possibly multi-pad) touch mask to a single colour, red-priority.
// Either eye's pad for a colour counts, matching the other games' behaviour.
bool maskToAnyColor(uint8_t mask, uint8_t &colorIndex)
{
  if (mask & (LEFT_RED_MASK | RIGHT_RED_MASK)) {
    colorIndex = 0;
    return true;
  }
  if (mask & (LEFT_GREEN_MASK | RIGHT_GREEN_MASK)) {
    colorIndex = 1;
    return true;
  }
  if (mask & (LEFT_BLUE_MASK | RIGHT_BLUE_MASK)) {
    colorIndex = 2;
    return true;
  }
  return false;
}

// Non-blocking hit/miss feedback: recolour just the hit-zone LEDs. It stays
// until the next full lane render, so it never disturbs the rhythm timing.
void blipHitZone(RgbColor color)
{
  const uint8_t leftLed = pgm_read_byte(&DRUMLINE_LEFT_ORDER[0]);
  const uint8_t rightLed = pgm_read_byte(&DRUMLINE_RIGHT_ORDER[0]);
  ledStrip.setPixelColor(leftLed, color.red, color.green, color.blue);
  ledStrip.setPixelColor(rightLed, color.red, color.green, color.blue);
  ledStrip.show();
}

void showDrumlineLane(const uint8_t lane[])
{
  setAllLeds(COLOR_OFF);

  for (uint8_t position = 0; position < DRUMLINE_LANE_LENGTH; ++position) {
    const uint8_t leftLed = pgm_read_byte(&DRUMLINE_LEFT_ORDER[position]);
    const uint8_t rightLed = pgm_read_byte(&DRUMLINE_RIGHT_ORDER[position]);

    if (lane[position] != 0) {
      const RgbColor color = colorForIndex(lane[position] - 1);
      ledStrip.setPixelColor(leftLed, color.red, color.green, color.blue);
      ledStrip.setPixelColor(rightLed, color.red, color.green, color.blue);
    } else if (position == 0) {
      // Dim marker so the player can see where to watch.
      ledStrip.setPixelColor(leftLed, 3, 3, 3);
      ledStrip.setPixelColor(rightLed, 3, 3, 3);
    }
  }

  ledStrip.show();
}

bool playDrumline()
{
  enableRebootOnButton();

  uint8_t lane[DRUMLINE_LANE_LENGTH];
  for (uint8_t i = 0; i < DRUMLINE_LANE_LENGTH; ++i) {
    lane[i] = 0;                 // 0 = empty, otherwise colourIndex + 1
  }

  uint8_t lives = DRUMLINE_LIVES;
  uint8_t notesSpawned = 0;
  uint8_t notesResolved = 0;     // each note is hit, mis-tapped, or missed once
  uint8_t spawnCountdown = 0;
  uint8_t prevMask = getPressedTouchMask();

  // Brief "get ready": show the empty lane with its hit-zone marker.
  showDrumlineLane(lane);
  delay(700);

  while (notesResolved < DRUMLINE_SONG_NOTES && lives > 0) {
    // Tempo ramps up as the song progresses.
    uint16_t tickMs = DRUMLINE_START_TICK_MS;
    if ((uint16_t)notesResolved * 8 < (DRUMLINE_START_TICK_MS - DRUMLINE_MIN_TICK_MS)) {
      tickMs = DRUMLINE_START_TICK_MS - (uint16_t)notesResolved * 8;
    } else {
      tickMs = DRUMLINE_MIN_TICK_MS;
    }

    showDrumlineLane(lane);

    // ---- Input window for this tick --------------------------------------
    uint16_t elapsedMs = 0;
    while (elapsedMs < tickMs) {
      const uint8_t pressed = getPressedTouchMask();
      const uint8_t edge = pressed & ~prevMask;   // newly-touched pads only
      prevMask = pressed;

      uint8_t tappedColor = 0;
      if (edge != 0 && maskToAnyColor(edge, tappedColor)) {
        if (lane[0] != 0) {
          const uint8_t noteColor = lane[0] - 1;
          if (tappedColor == noteColor) {
            // Correct: clear the note and score it.
            lane[0] = 0;
            ++notesResolved;
            blipHitZone(COLOR_GREEN);
          } else {
            // Wrong colour on a live note: burn the note and a life.
            lane[0] = 0;
            ++notesResolved;
            if (lives > 0) --lives;
            blipHitZone(COLOR_RED);
          }
        } else {
          // Tapped with nothing in the zone: harmless mistime, just a blip.
          blipHitZone(COLOR_RED);
        }
      }

      delay(5);
      elapsedMs += 5;
    }

    // ---- A note that crossed the hit zone untouched is a miss ------------
    if (lane[0] != 0) {
      lane[0] = 0;
      ++notesResolved;
      if (lives > 0) --lives;
    }

    // ---- Advance the lane one step toward the hit zone -------------------
    for (uint8_t i = 0; i < DRUMLINE_LANE_LENGTH - 1; ++i) {
      lane[i] = lane[i + 1];
    }
    lane[DRUMLINE_LANE_LENGTH - 1] = 0;

    // ---- Spawn the next note (with spacing) ------------------------------
    if (notesSpawned < DRUMLINE_SONG_NOTES) {
      if (spawnCountdown == 0) {
        lane[DRUMLINE_LANE_LENGTH - 1] = randomColorIndex() + 1;
        ++notesSpawned;
        spawnCountdown = DRUMLINE_SPAWN_GAP;
      } else {
        --spawnCountdown;
      }
    }
  }

  const bool wonGame = lives > 0;

  if (wonGame) {
    state = state & B11110111;   // bit 3 = Drumline cleared
    showSuccess();
  } else {
    showFailure();
  }

  disableRebootOnButton();
  return wonGame;
}

// ============================================================================
//  INTEGRATION  -  three small edits to the rest of firmware.ino
// ============================================================================
//
//  1) LAUNCH IT.  All six single-pad slots are already taken (3 left = solo
//     games, 3 right = two-player). Drumline launches from a two-pad CHORD.
//     In handleWakeButtonPress(), add one case to the switch (pressedMask):
//
//        case (LEFT_RED_MASK | LEFT_BLUE_MASK):
//          playDrumline();
//          break;
//
//     i.e. hold the left-eye RED and BLUE pads together, then tap the button.
//     (Pick any free chord you like; just keep it distinct from the singles.)
//
//  2) THE WIN BIT is already handled above (state & B11110111). It persists to
//     EEPROM via showSuccess(), exactly like the other games.
//
//  3) OPTIONAL UNLOCK ANIMATION gated on beating Drumline. In
//     runAnimationMode(), add a case before `default:` (e.g. case 11):
//
//        case 11:
//          if ((state & B00001000) != 0) { return 0; }   // bit 3 still set
//          return spinMode(step, 0, 3, 1, 0, 3, 1, 75);  // green/teal spin
//
//     And if you want the devsecops "you beat everything" boot animation to
//     require Drumline too, change its guard in case 5 from B00000111 to
//     B00001111 so all four game bits must be clear.
// ============================================================================
