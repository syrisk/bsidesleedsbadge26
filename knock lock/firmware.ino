// ============================================================================
//  SECRET KNOCK LOCK  -  rhythm-based auth toy for the BSides Leeds 2026 badge
// ============================================================================
//
//  Tap a secret rhythm on any touch pad ("knock"). If the rhythm matches the
//  stored secret, the badge unlocks (green celebration); otherwise it rejects
//  (red). The secret is the PATTERN of gaps between taps, normalized so it is
//  tempo-independent: knock the same rhythm fast or slow and it still matches,
//  but a different rhythm won't.
//
//  The badge has no vibration sensor, so a "knock" is a tap on any capacitive
//  pad. The secret is stored in EEPROM, so each badge can have its own knock
//  set in the field with no recompile -- the knock itself is the data.
//
//  Teaching point: this is "something you know" expressed as timing. It also
//  shows why timing tolerances matter, and (in matchesSecret) how a shared
//  secret is compared. Out of the box it ships with a default rhythm; reprogram
//  it via a long button press.
//
//  Standalone firmware: flash on its own. ATtiny814, megaTinyCore.
// ============================================================================

#include <avr/io.h>
#include <avr/sleep.h>
#include <avr/pgmspace.h>
#include <EEPROM.h>
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

// ---- Knock tuning ----------------------------------------------------------
static const uint8_t  MAX_TAPS          = 12;     // longest knock we accept
static const uint16_t KNOCK_COMPLETE_MS = 1200;   // silence this long ends a knock
static const uint8_t  MIN_TAP_GAP_MS    = 60;     // debounce between taps
static const uint8_t  NORMALIZE_TO      = 100;    // longest gap scales to this
static const uint8_t  REJECT_TOLERANCE  = 25;     // max per-gap error (of 100) to pass

static const uint16_t LONG_PRESS_MS      = 1200;  // button: enter program mode
static const uint16_t VERY_LONG_PRESS_MS = 3000;  // button: sleep
static const uint8_t  BUTTON_DEBOUNCE_MS = 50;
static const uint8_t  TOUCH_POLL_MS      = 2;
static const uint16_t PROGRAM_WAIT_MS    = 5000;  // give up waiting for a new knock
static const uint32_t IDLE_SLEEP_MS      = 900000UL;

// Default rhythm if EEPROM holds none: "da-da-daah-da" (5 taps, 4 gaps).
static const uint8_t DEFAULT_TAPS = 5;
static const uint8_t DEFAULT_NORM[] PROGMEM = { 50, 50, 100, 50 };

static const uint8_t EE_TAP_COUNT = 0;   // EEPROM[0] = tap count
static const uint8_t EE_GAPS      = 1;   // EEPROM[1..] = normalized gaps

// ---- Globals ---------------------------------------------------------------
cap_sensor_t touchButtons[NUM_TOUCH_BUTTONS];
byte pixelBuffer[NUM_LEDS * 3];
tinyNeoPixel ledStrip = tinyNeoPixel(NUM_LEDS, LED_DATA_PIN, NEO_GRB, pixelBuffer);

volatile bool shouldProcessPtc = false;
uint8_t  prevTouch = 0;
uint32_t lastActivityMs = 0;

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

// True on a fresh touch (any pad), with debounce against the previous tap.
bool tapEdge(uint32_t lastTapMs, uint32_t now) {
  const uint8_t mask = getPressedTouchMask();
  const uint8_t edge = mask & ~prevTouch;
  prevTouch = mask;
  return edge != 0 && (now - lastTapMs) >= MIN_TAP_GAP_MS;
}

// ---- LEDs ------------------------------------------------------------------
void setAllLeds(uint8_t r, uint8_t g, uint8_t b, bool show = false) {
  for (uint8_t i = 0; i < NUM_LEDS; ++i) ledStrip.setPixelColor(i, r, g, b);
  if (show) ledStrip.show();
}

void flashColor(uint8_t r, uint8_t g, uint8_t b, uint8_t times) {
  for (uint8_t i = 0; i < times; ++i) {
    setAllLeds(r, g, b, true); delay(70);
    setAllLeds(0, 0, 0, true); delay(70);
  }
}

// Brief acknowledgement that a tap registered.
void blipTap() {
  setAllLeds(6, 6, 6, true); delay(20);
  setAllLeds(0, 0, 0, true);
}

// ---- RTC / sleep (ported from the stock firmware) --------------------------
void initRtc() { while (RTC.STATUS > 0) {} RTC.CLKSEL = RTC_CLKSEL_INT1K_gc; }

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
  _PROTECTED_WRITE(WDT.CTRLA, WDT_PERIOD_8CLK_gc);   // wake -> reboot
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

// ---- Knock capture & matching ----------------------------------------------
// Capture a knock that has already started (tap #1 was at firstTapMs). Fills
// normOut[] with NORMALIZE_TO-scaled inter-tap gaps and returns the tap count.
uint8_t recordKnock(uint32_t firstTapMs, uint8_t normOut[]) {
  uint32_t tapTimes[MAX_TAPS];
  tapTimes[0] = firstTapMs;
  uint8_t taps = 1;
  blipTap();

  uint32_t lastTap = firstTapMs;
  while (taps < MAX_TAPS) {
    bool got = false;
    while (true) {
      const uint32_t now = millis();
      if (tapEdge(lastTap, now)) { tapTimes[taps] = now; got = true; break; }
      if (now - lastTap > KNOCK_COMPLETE_MS) break;     // silence ends the knock
      delay(TOUCH_POLL_MS);
    }
    if (!got) break;
    lastTap = tapTimes[taps];
    ++taps;
    blipTap();
  }

  if (taps < 2) return taps;

  // Inter-tap gaps, then normalize so the longest gap == NORMALIZE_TO. This is
  // what makes the match tempo-independent: only the rhythm's shape matters.
  uint16_t gaps[MAX_TAPS - 1];
  uint16_t maxGap = 1;
  for (uint8_t i = 0; i < taps - 1; ++i) {
    gaps[i] = (uint16_t)(tapTimes[i + 1] - tapTimes[i]);
    if (gaps[i] > maxGap) maxGap = gaps[i];
  }
  for (uint8_t i = 0; i < taps - 1; ++i)
    normOut[i] = (uint8_t)(((uint32_t)gaps[i] * NORMALIZE_TO) / maxGap);

  return taps;
}

uint8_t loadSecret(uint8_t normOut[]) {
  const uint8_t taps = EEPROM.read(EE_TAP_COUNT);
  if (taps < 2 || taps > MAX_TAPS) {                    // none stored -> default
    for (uint8_t i = 0; i < DEFAULT_TAPS - 1; ++i)
      normOut[i] = pgm_read_byte(&DEFAULT_NORM[i]);
    return DEFAULT_TAPS;
  }
  for (uint8_t i = 0; i < taps - 1; ++i) normOut[i] = EEPROM.read(EE_GAPS + i);
  return taps;
}

void saveSecret(const uint8_t norm[], uint8_t taps) {
  EEPROM.update(EE_TAP_COUNT, taps);
  for (uint8_t i = 0; i < taps - 1; ++i) EEPROM.update(EE_GAPS + i, norm[i]);
}

// Same tap count, and every normalized gap within tolerance.
bool matchesSecret(const uint8_t norm[], uint8_t taps) {
  uint8_t secret[MAX_TAPS - 1];
  const uint8_t secretTaps = loadSecret(secret);
  if (taps != secretTaps) return false;
  for (uint8_t i = 0; i < taps - 1; ++i) {
    const int16_t d = (int16_t)norm[i] - (int16_t)secret[i];
    if (d > REJECT_TOLERANCE || d < -REJECT_TOLERANCE) return false;
  }
  return true;
}

// ---- Outcomes --------------------------------------------------------------
void unlock() {
  flashColor(0, 30, 0, 6);
  setAllLeds(0, 25, 0, true);     // hold green = unlocked
  delay(1500);
  setAllLeds(0, 0, 0, true);
}

void reject() { flashColor(30, 0, 0, 3); }

// Long-press flow: learn and store a new secret knock.
void programNewKnock() {
  setAllLeds(30, 12, 0, true);    // orange = program mode, waiting for a knock
  prevTouch = getPressedTouchMask();

  const uint32_t start = millis();
  uint32_t firstTap = 0;
  bool got = false;
  while (millis() - start < PROGRAM_WAIT_MS) {
    if (tapEdge(0, millis())) { firstTap = millis(); got = true; break; }
    delay(TOUCH_POLL_MS);
  }
  setAllLeds(0, 0, 0, true);
  if (!got) { reject(); return; }                 // nobody knocked

  uint8_t norm[MAX_TAPS - 1];
  const uint8_t taps = recordKnock(firstTap, norm);
  if (taps < 2) { reject(); return; }             // need at least two taps

  saveSecret(norm, taps);
  flashColor(30, 12, 0, 4);                        // orange confirm = saved
}

// ---- Arduino entry points --------------------------------------------------
void setup() {
  pinMode(PIN_PA1, OUTPUT);
  pinMode(PIN_PA2, OUTPUT);
  pinMode(LED_POWER_PIN, OUTPUT);
  pinMode(LED_DATA_PIN, OUTPUT);
  pinMode(WAKE_BUTTON_PIN, INPUT_PULLUP);
  digitalWrite(LED_POWER_PIN, HIGH);

  initRtc();
  setupTouchButtons();
}

void loop() {
  ledStrip.begin();
  enableRtcPtc();
  lastActivityMs = millis();
  prevTouch = getPressedTouchMask();

  while (true) {
    const uint16_t held = measureButtonLowTime(VERY_LONG_PRESS_MS + 200);
    if (held > VERY_LONG_PRESS_MS) enterSleep();
    else if (held > LONG_PRESS_MS) { programNewKnock(); lastActivityMs = millis(); prevTouch = getPressedTouchMask(); continue; }

    if (tapEdge(0, millis())) {                    // a knock attempt begins
      const uint32_t t0 = millis();
      lastActivityMs = t0;
      uint8_t norm[MAX_TAPS - 1];
      const uint8_t taps = recordKnock(t0, norm);
      if (taps >= 2 && matchesSecret(norm, taps)) unlock();
      else reject();
      prevTouch = getPressedTouchMask();
      lastActivityMs = millis();
      continue;
    }

    if (millis() - lastActivityMs > IDLE_SLEEP_MS) enterSleep();
    delay(TOUCH_POLL_MS);
  }
}
