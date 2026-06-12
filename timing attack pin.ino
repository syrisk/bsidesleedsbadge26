// ============================================================================
//  TIMING ATTACK  -  a side-channel teaching toy for the BSides Leeds 2026 badge
// ============================================================================
//
//  The badge holds a secret 4-colour PIN (each digit is red, green or blue, so
//  3^4 = 81 combinations). You enter a guess on the touch pads. The badge then
//  "checks" it with a DELIBERATELY VULNERABLE comparison: it compares one digit
//  at a time and stops at the first mismatch. While it checks, it blinks a dim
//  white "thinking" pulse once per comparison.
//
//  That blink count is the leak. A wrong first digit -> 1 pulse. First digit
//  right, second wrong -> 2 pulses. Three right -> 4 pulses, etc. So you can
//  recover the PIN digit-by-digit by watching how long the badge "thinks",
//  cracking it in ~12 guesses instead of brute-forcing 81. That's a timing
//  side-channel: identical "Access Denied" output, but the TIME taken leaks
//  how much of your guess was correct.
//
//  Standalone firmware: flash it on its own. ATtiny814, megaTinyCore.
// ============================================================================

#include <avr/io.h>
#include <avr/sleep.h>
#include <ptc.h>
#include <tinyNeoPixel_Static.h>

// ---- Hardware (same wiring as the stock badge) -----------------------------
static const uint8_t WAKE_BUTTON_PIN = PIN_PA3;
static const uint8_t LED_DATA_PIN    = PIN_PB3;
static const uint8_t LED_POWER_PIN   = PIN_PB2;

static const uint8_t NUM_LEDS          = 18;
static const uint8_t NUM_TOUCH_BUTTONS = 6;

static const uint8_t TOUCH_BUTTON_PINS[NUM_TOUCH_BUTTONS] = {
  PIN_PA4, PIN_PA5, PIN_PA6, PIN_PA7, PIN_PB0, PIN_PB1,
};

static const uint8_t LEFT_BLUE_MASK   = 1 << 0;
static const uint8_t LEFT_RED_MASK    = 1 << 1;
static const uint8_t LEFT_GREEN_MASK  = 1 << 2;
static const uint8_t RIGHT_GREEN_MASK = 1 << 3;
static const uint8_t RIGHT_RED_MASK   = 1 << 4;
static const uint8_t RIGHT_BLUE_MASK  = 1 << 5;

// ---- Game tuning -----------------------------------------------------------
static const uint8_t  PIN_LENGTH   = 4;
static const uint8_t  THINK_ON_MS  = 200;   // length of one "thinking" pulse...
static const uint8_t  THINK_OFF_MS = 160;   // ...the count of these is the leak
static const uint16_t SHORT_PRESS_MS = 200;
static const uint16_t LONG_PRESS_MS  = 1200;
static const uint16_t MAX_BUTTON_HOLD_MS = 2000;
static const uint8_t  BUTTON_DEBOUNCE_MS = 50;
static const uint8_t  TOUCH_POLL_MS = 10;
static const uint32_t IDLE_SLEEP_MS = 900000UL;   // 15 minutes

// ---- Globals ---------------------------------------------------------------
cap_sensor_t touchButtons[NUM_TOUCH_BUTTONS];
byte pixelBuffer[NUM_LEDS * 3];
tinyNeoPixel ledStrip = tinyNeoPixel(NUM_LEDS, LED_DATA_PIN, NEO_GRB, pixelBuffer);

volatile bool shouldProcessPtc = false;

uint16_t randomState = 0xACE1u;
uint8_t  secret[PIN_LENGTH];
uint8_t  prevTouch = 0;
uint32_t lastActivityMs = 0;

// ---- Pseudo-random (LFSR, seeded from touch timing) ------------------------
void seedFromNow() { randomState ^= (uint16_t)millis(); if (!randomState) randomState = 0xACE1u; }

uint8_t nextRandomByte() {
  const bool bit = randomState & 1;
  randomState >>= 1;
  if (bit) randomState ^= 0xB400u;
  return (uint8_t)randomState;
}

uint8_t randomColorIndex() {
  uint8_t v;
  do { v = nextRandomByte() & 0x03; } while (v > 2);
  return v;
}

// ---- Touch -----------------------------------------------------------------
uint8_t getPressedTouchMask() {
  uint8_t mask = 0;
  for (uint8_t i = 0; i < NUM_TOUCH_BUTTONS; ++i)
    if (ptc_get_node_touched(&touchButtons[i])) mask |= (1 << i);
  return mask;
}

void setupTouchButtons() {
  for (uint8_t i = 0; i < NUM_TOUCH_BUTTONS; ++i) {
    if (ptc_add_selfcap_node(&touchButtons[i], 0, PIN_TO_PTC(TOUCH_BUTTON_PINS[i])) != PTC_LIB_SUCCESS)
      while (true) {}
    ptc_node_set_gain(&touchButtons[i], PTC_GAIN_1);
    ptc_node_set_prescaler(&touchButtons[i], PTC_PRESC_DIV4_gc);
    ptc_node_set_oversamples(&touchButtons[i], 4);
    ptc_node_set_thresholds(&touchButtons[i], 80, 10);
  }
}

void setTouchButtonPins(uint8_t mode) {
  for (uint8_t i = 0; i < NUM_TOUCH_BUTTONS; ++i) pinMode(TOUCH_BUTTON_PINS[i], mode);
}

// Reduce a touch mask to a colour (0=red, 1=green, 2=blue), red-priority.
bool maskToColor(uint8_t mask, uint8_t &colorIndex) {
  if (mask & (LEFT_RED_MASK   | RIGHT_RED_MASK))   { colorIndex = 0; return true; }
  if (mask & (LEFT_GREEN_MASK | RIGHT_GREEN_MASK)) { colorIndex = 1; return true; }
  if (mask & (LEFT_BLUE_MASK  | RIGHT_BLUE_MASK))  { colorIndex = 2; return true; }
  return false;
}

// ---- LEDs ------------------------------------------------------------------
void setAllLeds(uint8_t r, uint8_t g, uint8_t b, bool show = false) {
  for (uint8_t i = 0; i < NUM_LEDS; ++i) ledStrip.setPixelColor(i, r, g, b);
  if (show) ledStrip.show();
}

// Right-eye logical LED -> physical strip index (matches the stock layout).
void setRightEyeLed(uint8_t ledIndex, uint8_t r, uint8_t g, uint8_t b) {
  ledStrip.setPixelColor(ledIndex == 0 ? 17 : ledIndex + 8, r, g, b);
}

void colorChannels(uint8_t colorIndex, uint8_t &r, uint8_t &g, uint8_t &b) {
  r = g = b = 0;
  if (colorIndex == 0) r = 30;
  else if (colorIndex == 1) g = 30;
  else if (colorIndex == 2) b = 30;
}

void flashColor(uint8_t r, uint8_t g, uint8_t b, uint8_t times) {
  for (uint8_t i = 0; i < times; ++i) {
    setAllLeds(r, g, b, true); delay(70);
    setAllLeds(0, 0, 0, true); delay(70);
  }
}

// ---- RTC / sleep (ported from the stock firmware) --------------------------
void initRtc() {
  while (RTC.STATUS > 0) {}
  RTC.CLKSEL = RTC_CLKSEL_INT1K_gc;
}

void enableRtcPtc(uint8_t period = RTC_PERIOD_CYC16_gc) {
  RTC.PITINTCTRL = RTC_PI_bm;
  RTC.PITCTRLA = period | RTC_PITEN_bm;
  shouldProcessPtc = true;
}

void disableRtc() { RTC.PITINTCTRL = ~(RTC_PI_bm); shouldProcessPtc = false; }

void enterSleep() {
  disableRtc();
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  PORTA.PIN3CTRL = PORT_PULLUPEN_bm | PORT_ISC_LEVEL_gc;
  digitalWrite(LED_POWER_PIN, LOW);
  ADC0.CTRLA &= ~ADC_ENABLE_bm;
  setTouchButtonPins(OUTPUT);
  sleep_enable();
  sleep_cpu();
  // Woken by the button: reboot through the watchdog -> fresh game.
  _PROTECTED_WRITE(WDT.CTRLA, WDT_PERIOD_8CLK_gc);
  while (true) {}
}

ISR(PORTA_PORT_vect) { PORTA.INTFLAGS = PORT_INT3_bm; }

ISR(RTC_PIT_vect) {
  RTC.PITINTFLAGS = RTC_PI_bm;
  if (shouldProcessPtc) ptc_process(millis());
}

uint16_t measureButtonLowTime(uint16_t maxMs) {
  if (digitalRead(WAKE_BUTTON_PIN) == HIGH) return 0;
  delay(BUTTON_DEBOUNCE_MS);
  uint16_t elapsed = BUTTON_DEBOUNCE_MS;
  while (digitalRead(WAKE_BUTTON_PIN) == LOW) {
    delay(5); elapsed += 5;
    if (elapsed > maxMs) return maxMs;
  }
  return elapsed;
}

// ---- Game ------------------------------------------------------------------
void generateSecret() {
  for (uint8_t i = 0; i < PIN_LENGTH; ++i) secret[i] = randomColorIndex();
}

// Show the guess being built: entered digits in colour, the next slot as a dim
// marker, on both eyes. Slots map to ring LEDs 0..PIN_LENGTH-1.
void showGuessSlots(const uint8_t guess[], uint8_t entered) {
  setAllLeds(0, 0, 0);
  for (uint8_t s = 0; s < PIN_LENGTH; ++s) {
    uint8_t r, g, b;
    if (s < entered) {
      colorChannels(guess[s], r, g, b);
    } else if (s == entered) {
      r = g = b = 4;            // dim marker = "enter this digit next"
    } else {
      continue;
    }
    ledStrip.setPixelColor(s, r, g, b);
    setRightEyeLed(s, r, g, b);
  }
  ledStrip.show();
}

// One unit of "processing time", shown as a dim white pulse. The NUMBER of
// these pulses is exactly the number of digit comparisons performed.
void thinkPulse() {
  setAllLeds(8, 8, 8, true); delay(THINK_ON_MS);
  setAllLeds(0, 0, 0, true); delay(THINK_OFF_MS);
}

// The vulnerable check: early-return on first mismatch, time proportional to
// the length of the correct prefix. Returns true only on a full match.
bool checkGuessTimed(const uint8_t guess[]) {
  bool full = true;
  for (uint8_t i = 0; i < PIN_LENGTH; ++i) {
    thinkPulse();                       // <-- the timing leak
    if (guess[i] != secret[i]) { full = false; break; }
  }
  return full;
}

// Light one green LED per guess used (capped at the ring) as a "score".
void showScore(uint16_t guesses) {
  setAllLeds(0, 0, 0);
  uint8_t n = guesses > NUM_LEDS ? NUM_LEDS : (uint8_t)guesses;
  for (uint8_t i = 0; i < n; ++i) ledStrip.setPixelColor(i, 0, 20, 0);
  ledStrip.show();
  delay(2500);
  setAllLeds(0, 0, 0, true);
}

// Blocking input: returns 0..2 for a colour tap, -1 for a button "clear".
// Handles idle-sleep and long-press-sleep internally.
int8_t readInput() {
  while (true) {
    const uint16_t held = measureButtonLowTime(MAX_BUTTON_HOLD_MS);
    if (held > LONG_PRESS_MS) enterSleep();
    if (held > SHORT_PRESS_MS) { lastActivityMs = millis(); return -1; }

    const uint8_t mask = getPressedTouchMask();
    const uint8_t edge = mask & ~prevTouch;
    prevTouch = mask;

    uint8_t color;
    if (edge && maskToColor(edge, color)) {
      seedFromNow();
      lastActivityMs = millis();
      return (int8_t)color;
    }

    if (millis() - lastActivityMs > IDLE_SLEEP_MS) enterSleep();
    delay(TOUCH_POLL_MS);
  }
}

// Play one PIN until cracked; returns the number of guesses it took.
uint16_t playUntilCracked() {
  generateSecret();
  flashColor(30, 12, 0, 2);               // orange double-flash = new PIN armed

  uint16_t guesses = 0;
  while (true) {
    uint8_t guess[PIN_LENGTH];
    uint8_t entered = 0;
    showGuessSlots(guess, 0);

    while (entered < PIN_LENGTH) {
      const int8_t in = readInput();
      if (in < 0) { entered = 0; }        // button = clear this guess
      else        { guess[entered++] = (uint8_t)in; }
      showGuessSlots(guess, entered);
    }

    delay(250);                            // brief pause on the full guess
    ++guesses;

    if (checkGuessTimed(guess)) {          // full match -> cracked
      flashColor(0, 30, 0, 6);             // green: access granted
      return guesses;
    }
    flashColor(30, 0, 0, 3);               // red: access denied (identical every time)
  }
}

void setup() {
  pinMode(PIN_PA1, OUTPUT);
  pinMode(PIN_PA2, OUTPUT);
  pinMode(LED_POWER_PIN, OUTPUT);
  pinMode(LED_DATA_PIN, OUTPUT);
  pinMode(WAKE_BUTTON_PIN, INPUT_PULLUP);
  digitalWrite(LED_POWER_PIN, HIGH);

  initRtc();
  setupTouchButtons();
  seedFromNow();
}

void loop() {
  ledStrip.begin();
  enableRtcPtc();
  lastActivityMs = millis();

  while (true) {
    const uint16_t guesses = playUntilCracked();
    showScore(guesses);                    // fewer LEDs = you used the timing leak
  }
}
