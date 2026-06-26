/*
  Robot with Natural Animated Eyes - ESP32-C3 Mini + 0.96" OLED (SSD1306) + TTP223 Touch Module

  Actual wiring based on your schematic:
   OLED  -> I2C  : SDA = GPIO21 (Green), SCL = GPIO20 (Blue), VDD = 3.3V (Red), GND (Black)
   Touch -> TTP223: I/O = GPIO10 (Orange), VCC = 3.3V, GND
   Speaker (LOA) -> One terminal to GND, the other to GPIO9
                    (Reserved for future sound effects - currently unused)

  ⚠️ Note: GPIO9 is a boot strapping pin on the ESP32-C3
  (the same pin used by the onboard BOOT function).
  Using it for a speaker is generally safe because it only matters
  during reset or power-up. However, if the board ever fails to boot
  or enters download mode unexpectedly, GPIO9 should be the first pin
  to investigate.

  Eye modes:
   DEFAULT / HAPPY / ANGRY / SLEEPY / SURPRISED / CRYING

  Behavior:
   - Automatically cycles through DEFAULT, HAPPY, ANGRY, SLEEPY,
     and SURPRISED every 15 seconds.
   - Single tap on the touch sensor -> HAPPY mode for 3 seconds,
     then returns to DEFAULT (the 15-second timer restarts).
   - Double tap within 400 ms -> CRYING mode for 3 seconds,
     then returns to DEFAULT.
   - Features random smooth blinking, subtle pupil movement,
     and gentle breathing animation during sleep for a more
     natural and lifelike appearance.

  Required libraries:
   - Adafruit GFX Library
   - Adafruit SSD1306
*/

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

//---------------------------------------pin setting---------------------------------------

#define SDA_PIN        20
#define SCL_PIN        21
#define TOUCH_PIN      10
// #define BUZZER_PIN     9   // // Future update: Speaker support can be added for sound effects and voice playback.

#define SCREEN_WIDTH   128
#define SCREEN_HEIGHT  64
#define OLED_RESET     -1
#define SCREEN_ADDRESS // If nothing appears on the display, try changing the I2C address to 0x3D.

const unsigned long IDLE_SWITCH_INTERVAL      = 15000; // Cycle through eye expressions every 15 seconds
const unsigned long TOUCH_EXPRESSION_DURATION = 3000;  // Duration of the Happy/Crying expression
const unsigned long DOUBLE_TAP_WINDOW         = 400;   // Maximum time between two taps (ms)
const unsigned long DEBOUNCE_TIME             = 50;    // Touch debounce time
const unsigned long FRAME_INTERVAL            = 30;    // Render one animation frame every 30 ms

// ------------------------------------------------------------------

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

enum Mood { MOOD_DEFAULT, MOOD_HAPPY, MOOD_ANGRY, MOOD_SLEEPY, MOOD_SURPRISED, MOOD_CRYING };

Mood currentMood = MOOD_DEFAULT;

Mood idleCycle[] = { MOOD_DEFAULT, MOOD_HAPPY, MOOD_ANGRY, MOOD_SLEEPY, MOOD_SURPRISED };
const int idleCycleLength = sizeof(idleCycle) / sizeof(idleCycle[0]);
int idleCycleIndex = 0;

unsigned long lastIdleSwitch = 0;
unsigned long lastFrameTime = 0;

// ------------------------------------------touch setting -------------------------------------------------
bool lastTouchState = false;
unsigned long lastTouchChangeTime = 0;
bool waitingSecondTap = false;
unsigned long waitingForSecondTap = 0;

bool tempExpressionActive = false;
unsigned long tempExpressionStart = 0;

// ---------------------- Animation: Blinking ----------------------
enum BlinkPhase { BLINK_IDLE, BLINK_CLOSING, BLINK_HOLD, BLINK_OPENING };
BlinkPhase blinkPhase = BLINK_IDLE;
unsigned long blinkPhaseStart = 0;
unsigned long nextBlinkAt = 0;
float blinkAmount = 0.0f; //full open =1 , close =0 

const unsigned long BLINK_CLOSE_MS = 90;
const unsigned long BLINK_HOLD_MS  = 70;
const unsigned long BLINK_OPEN_MS  = 110;

// ---------------------- Animation: Pupil Movement ----------------------
float lookX = 0, lookY = 0;
float targetLookX = 0, targetLookY = 0;
unsigned long nextLookChangeAt = 0;

// ---------------------- Animation Helper Functions ----------------------
bool moodHasPupil(Mood m) {
  return (m == MOOD_DEFAULT || m == MOOD_ANGRY || m == MOOD_SURPRISED || m == MOOD_CRYING);
}
bool moodBlinks(Mood m) {
  return (m != MOOD_SLEEPY);
}

void scheduleNextBlink() {
  nextBlinkAt = millis() + random(2000, 5500);
}

void scheduleNextLook() {
  nextLookChangeAt = millis() + random(1500, 3500);
}

void updateBlink() {
  unsigned long now = millis();

  if (!moodBlinks(currentMood)) {
    blinkPhase = BLINK_IDLE;
    blinkAmount = 0;
    return;
  }

  switch (blinkPhase) {
    case BLINK_IDLE:
      blinkAmount = 0;
      if (now >= nextBlinkAt) {
        blinkPhase = BLINK_CLOSING;
        blinkPhaseStart = now;
      }
      break;
    case BLINK_CLOSING: {
      float t = (float)(now - blinkPhaseStart) / BLINK_CLOSE_MS;
      if (t >= 1.0f) { blinkAmount = 1.0f; blinkPhase = BLINK_HOLD; blinkPhaseStart = now; }
      else blinkAmount = t;
      break;
    }
    case BLINK_HOLD: {
      blinkAmount = 1.0f;
      if (now - blinkPhaseStart >= BLINK_HOLD_MS) { blinkPhase = BLINK_OPENING; blinkPhaseStart = now; }
      break;
    }
    case BLINK_OPENING: {
      float t = (float)(now - blinkPhaseStart) / BLINK_OPEN_MS;
      if (t >= 1.0f) { blinkAmount = 0.0f; blinkPhase = BLINK_IDLE; scheduleNextBlink(); }
      else blinkAmount = 1.0f - t;
      break;
    }
  }
}

void forceQuickBlink() {
  
// Creates a smooth transition when changing expressions.
  blinkPhase = BLINK_CLOSING;
  blinkPhaseStart = millis();
}

void updateLook() {
  unsigned long now = millis();

  if (!moodHasPupil(currentMood)) {
    lookX = 0; lookY = 0;
    return;
  }

  float rangeX = 4, rangeY = 3;
  if (currentMood == MOOD_ANGRY)     { rangeX = 2.5; rangeY = 2; }
  if (currentMood == MOOD_SURPRISED) { rangeX = 2.5; rangeY = 2; }
  if (currentMood == MOOD_CRYING)    { rangeX = 2;   rangeY = 1.5; }

  if (now >= nextLookChangeAt) {
    targetLookX = random(-100, 101) / 100.0f * rangeX;
    targetLookY = random(-100, 101) / 100.0f * rangeY;
    scheduleNextLook();
  }
  
// Smoothly move toward the target (lerp)
  lookX += (targetLookX - lookX) * 0.06f;
  lookY += (targetLookY - lookY) * 0.06f;
}

//--------eye function----------------------

void clearScreen() { display.clearDisplay(); }


void drawEyeBoxWithBlink(int x, int y, int w, int h, int radius, float blink) {
  display.fillRoundRect(x, y, w, h, radius, SSD1306_WHITE);
  if (blink > 0.02f) {
    int coverH = (int)(h * blink);
    display.fillRect(x - 2, y - 2, w + 4, coverH + 2, SSD1306_BLACK);
  }
}

void drawPupil(int cx, int cy, int r, float offX, float offY) {
  display.fillCircle(cx + (int)offX, cy + (int)offY, r, SSD1306_BLACK);
 
  display.fillCircle(cx + (int)offX - r / 3, cy + (int)offY - r / 3, max(1, r / 4), SSD1306_WHITE);
}

void drawDefaultEyes(float blink, float lx, float ly) {
  int lx1 = 20, ly1 = 18, w = 35, h = 35;
  int rx1 = 73, ry1 = 18;
  drawEyeBoxWithBlink(lx1, ly1, w, h, 10, blink);
  drawEyeBoxWithBlink(rx1, ry1, w, h, 10, blink);
  if (blink < 0.85f) {
    drawPupil(lx1 + w / 2, ly1 + h / 2, 7, lx, ly);
    drawPupil(rx1 + w / 2, ry1 + h / 2, 7, lx, ly);
  }
}

void drawHappyEyes(float blink) {
// Happy eyes: upward curve (like ^)
  display.fillRoundRect(15, 25, 40, 22, 12, SSD1306_WHITE);
  display.fillRect(15, 10, 40, 18, SSD1306_BLACK);
  display.fillRoundRect(73, 25, 40, 22, 12, SSD1306_WHITE);
  display.fillRect(73, 10, 40, 18, SSD1306_BLACK);
  if (blink > 0.3f) {
    int coverH = (int)(20 * blink);
    display.fillRect(13, 25, 44, coverH, SSD1306_BLACK);
    display.fillRect(71, 25, 44, coverH, SSD1306_BLACK);
  }
}

void drawAngryEyes(float blink, float lx, float ly) {
  int lx1 = 20, ly1 = 22, w = 35, h = 28;
  int rx1 = 73, ry1 = 22;
  drawEyeBoxWithBlink(lx1, ly1, w, h, 6, blink);
  drawEyeBoxWithBlink(rx1, ry1, w, h, 6, blink);
  if (blink < 0.85f) {
    drawPupil(lx1 + w / 2, ly1 + h / 2 + 2, 6, lx, ly);
    drawPupil(rx1 + w / 2, ry1 + h / 2 + 2, 6, lx, ly);
  }
  // Angry eyebrows are always drawn above the eyes (even while blinking)
  display.fillTriangle(15, 18, 55, 18, 15, 30, SSD1306_BLACK);
  display.fillTriangle(113, 18, 73, 18, 113, 30, SSD1306_BLACK);
}

void drawSleepyEyes() {
// Gentle breathing: the eye line height slowly oscillates.
  float breathe = (sin(millis() / 1400.0f) + 1.0f) / 2.0f; // 0..1
  int h = 4 + (int)(breathe * 3);
  int y = 33 - h / 2;
  display.fillRoundRect(18, y, 38, h, h / 2, SSD1306_WHITE);
  display.fillRoundRect(72, y, 38, h, h / 2, SSD1306_WHITE);
}

void drawSurprisedEyes(float blink, float lx, float ly) {
  int cxL = 38, cxR = 90, cy = 35, r = 22;
  display.fillCircle(cxL, cy, r, SSD1306_WHITE);
  display.fillCircle(cxR, cy, r, SSD1306_WHITE);
  if (blink > 0.02f) {
    int coverH = (int)(r * 2 * blink);
    display.fillRect(cxL - r - 2, cy - r - 2, r * 2 + 4, coverH + 2, SSD1306_BLACK);
    display.fillRect(cxR - r - 2, cy - r - 2, r * 2 + 4, coverH + 2, SSD1306_BLACK);
  }
  if (blink < 0.85f) {
    drawPupil(cxL, cy, 8, lx, ly);
    drawPupil(cxR, cy, 8, lx, ly);
  }
}

void drawCryingEyes(float blink, float lx, float ly) {
  int lx1 = 20, ly1 = 20, w = 35, h = 26;
  int rx1 = 73, ry1 = 20;
  drawEyeBoxWithBlink(lx1, ly1, w, h, 10, blink);
  drawEyeBoxWithBlink(rx1, ry1, w, h, 10, blink);
  if (blink < 0.85f) {
    drawPupil(lx1 + w / 2, ly1 + h / 2, 6, lx, ly);
    drawPupil(rx1 + w / 2, ry1 + h / 2, 6, lx, ly);
  }

// Animated tears: one tear per eye falls downward in a continuous loop.
  unsigned long t = millis() % 1400;
  float p = t / 1400.0f; // 0..1
  int dropY = 46 + (int)(p * 14);
  int alpha = (p < 0.85f) ? 1 : 0; 
  if (alpha) {
    display.fillTriangle(lx1 + 10, dropY, lx1 + 18, dropY, lx1 + 14, dropY + 10, SSD1306_WHITE);
    display.fillCircle(lx1 + 14, dropY + 10, 4, SSD1306_WHITE);
    display.fillTriangle(rx1 + 10, dropY, rx1 + 18, dropY, rx1 + 14, dropY + 10, SSD1306_WHITE);
    display.fillCircle(rx1 + 14, dropY + 10, 4, SSD1306_WHITE);
  }
}

void renderFrame() {
  clearScreen();
  switch (currentMood) {
    case MOOD_DEFAULT:   drawDefaultEyes(blinkAmount, lookX, lookY);   break;
    case MOOD_HAPPY:     drawHappyEyes(blinkAmount);                  break;
    case MOOD_ANGRY:     drawAngryEyes(blinkAmount, lookX, lookY);     break;
    case MOOD_SLEEPY:    drawSleepyEyes();                            break;
    case MOOD_SURPRISED: drawSurprisedEyes(blinkAmount, lookX, lookY); break;
    case MOOD_CRYING:    drawCryingEyes(blinkAmount, lookX, lookY);    break;
  }
  display.display();
}

// --------------------change mood --------------------

void setMood(Mood m) {
  currentMood = m;
  forceQuickBlink();  
  scheduleNextLook();
}

void triggerExpression(Mood m) {
  setMood(m);
  tempExpressionActive = true;
  tempExpressionStart = millis();
}

// ---------------------- Touch Logic ----------------------

void handleTouch() {
  bool rawState = (digitalRead(TOUCH_PIN) == HIGH);
  unsigned long now = millis();

  if (rawState != lastTouchState && (now - lastTouchChangeTime) > DEBOUNCE_TIME) {
    lastTouchChangeTime = now;
    lastTouchState = rawState;

    if (rawState == false) {
      if (waitingSecondTap && (now - waitingForSecondTap) <= DOUBLE_TAP_WINDOW) {
        waitingSecondTap = false;
        triggerExpression(MOOD_CRYING);
      } else {
        waitingSecondTap = true;
        waitingForSecondTap = now;
      }
    }
  }

  if (waitingSecondTap && (now - waitingForSecondTap) > DOUBLE_TAP_WINDOW) {
    waitingSecondTap = false;
    triggerExpression(MOOD_HAPPY);
  }
}

// ---------------------- Automatic Expression Cycle ----------------------
void handleIdleCycle() {
  if (tempExpressionActive) return;
  unsigned long now = millis();
  if (now - lastIdleSwitch >= IDLE_SWITCH_INTERVAL) {
    lastIdleSwitch = now;
    idleCycleIndex = (idleCycleIndex + 1) % idleCycleLength;
    setMood(idleCycle[idleCycleIndex]);
  }
}

void handleTempExpressionTimeout() {
  if (tempExpressionActive && (millis() - tempExpressionStart >= TOUCH_EXPRESSION_DURATION)) {
    tempExpressionActive = false;
    idleCycleIndex = 0;
    lastIdleSwitch = millis();
    setMood(MOOD_DEFAULT);
  }
}

// ---------------------- Setup / Loop ----------------------

void setup() {
  pinMode(TOUCH_PIN, INPUT);
  randomSeed(analogRead(0));

  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(400000); 

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.begin(115200);
    Serial.println("OLED not found! Check the wiring and I2C address.");
    while (true) { delay(1000); }
  }

  display.clearDisplay();
  display.display();

  lastIdleSwitch = millis();
  scheduleNextBlink();
  scheduleNextLook();
  renderFrame();
}

void loop() {
  handleTouch();
  handleTempExpressionTimeout();
  handleIdleCycle();

  unsigned long now = millis();
  if (now - lastFrameTime >= FRAME_INTERVAL) {
    lastFrameTime = now;
    updateBlink();
    updateLook();
    renderFrame();
  }
}
