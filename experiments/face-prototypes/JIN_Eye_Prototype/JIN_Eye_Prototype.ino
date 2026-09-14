#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>

#define TFT_CS    41
#define TFT_DC    42
#define TFT_RST   47
#define TFT_MOSI  21
#define TFT_SCLK  14
#define NEXT_BUTTON 0

Adafruit_ST7789 tft(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);

const uint16_t BACKGROUND  = 0x0021;
const uint16_t PLATE       = 0x18E4;
const uint16_t RECESS      = 0x0862;
const uint16_t RECESS_DARK = 0x0841;
const uint16_t SEAM        = 0x20A0;
const uint16_t AMBER_1     = 0xABA3;
const uint16_t AMBER_2     = 0xF524;
const uint16_t AMBER_3     = 0xFD87;
const uint16_t AMBER_4     = 0xFE09;
const uint16_t HOT_CORE    = 0xFE8F;
const uint16_t LABEL       = 0x7BEF;

const int FACE_X = 20;
const int FACE_Y = 48;
const int FACE_W = 200;
const int FACE_H = 220;

enum Candidate { ROUNDED_SLOT = 3, SEGMENTED_SLOT = 4, WIDE_TROUGH = 6 };

Candidate candidate = ROUNDED_SLOT;
int leftPct = 70;
int rightPct = 70;
bool automaticDemo = false;
unsigned long stepStarted = 0;
int demoStep = 0;
bool blinkPlayedThisStep = false;
bool lastButtonState = HIGH;
unsigned long lastButtonChange = 0;

const int demoCandidates[] = {3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 6, 6, 6, 6, 6};
const int demoLeft[]       = {70, 35, 100, 0, 75, 70, 35, 100, 0, 75, 70, 35, 100, 0, 75};
const int demoRight[]      = {70, 35, 100, 0, 45, 70, 35, 100, 0, 45, 70, 35, 100, 0, 45};
const int DEMO_STEPS = 15;
const unsigned long STEP_MS = 10000;

int roundHalfUp(int pct, int maximum) {
  if (pct <= 0) return 0;
  return (pct * maximum + 50) / 100;
}

void drawRestingMouth() {
  const int heights[5] = {5, 7, 9, 7, 5};
  for (int i = 0; i < 5; i++) {
    int x = 79 + i * 18;
    int h = heights[i];
    int y = 233 - h;
    tft.fillRoundRect(x, y, 10, h, min(4, h / 2), AMBER_1);
  }
}

void drawSharedPlate() {
  tft.fillScreen(BACKGROUND);
  tft.fillRoundRect(FACE_X, FACE_Y, FACE_W, FACE_H, 28, PLATE);
  tft.drawRoundRect(FACE_X, FACE_Y, FACE_W, FACE_H, 28, 0x3188);
  drawRestingMouth();
}

void drawHeader() {
  tft.fillRect(0, 0, 240, 42, BACKGROUND);
  tft.setTextColor(AMBER_3, BACKGROUND);
  tft.setTextSize(2);
  tft.setCursor(8, 7);
  tft.print("C");
  tft.print((int)candidate);
  tft.print("  ");
  tft.print(leftPct);
  tft.print("/");
  tft.print(rightPct);
  tft.print("%");
  tft.setTextSize(1);
  tft.setTextColor(LABEL, BACKGROUND);
  tft.setCursor(8, 29);
  if (candidate == ROUNDED_SLOT) tft.print("ROUNDED SLOT");
  if (candidate == SEGMENTED_SLOT) tft.print("SEGMENTED SLOT");
  if (candidate == WIDE_TROUGH) tft.print("WIDE SHARED TROUGH");
}

void drawFooter() {
  tft.fillRect(0, 276, 240, 44, BACKGROUND);
  tft.setTextSize(1);
  tft.setTextColor(LABEL, BACKGROUND);
  tft.setCursor(8, 282);
  if (leftPct == 75 && rightPct == 45) tft.print("ASYMMETRY TEST 75/45");
  else if (leftPct == 70) tft.print("70% + SYNCHRONIZED BLINK");
  else tft.print("APERTURE LEGIBILITY TEST");
  tft.setCursor(8, 299);
  tft.print("PRESS BOOT FOR NEXT VIEW");
}

void drawSlotEye(int recessX, int pct, bool segmented, bool rightEye, float blinkScalar) {
  tft.fillRoundRect(recessX, 118, 62, 20, 7, RECESS_DARK);
  tft.drawRoundRect(recessX, 118, 62, 20, 7, RECESS);

  int effectivePct = (int)(pct * blinkScalar + 0.5f);
  int h = roundHalfUp(effectivePct, 14);
  if (h == 0) {
    tft.drawFastHLine(recessX + 3, 128, 56, SEAM);
  } else if (!segmented) {
    int y = 135 - h;
    tft.fillRoundRect(recessX + 3, y, 56, h, 2, AMBER_3);
    if (h >= 12) tft.drawFastHLine(recessX + 5, y + h / 2, 52, HOT_CORE);
  } else {
    int y = 135 - h;
    int firstX = rightEye ? 142 : 52;
    for (int i = 0; i < 4; i++) {
      tft.fillRect(firstX + i * 12, y, 10, h, AMBER_3);
      if (h >= 12) tft.drawFastHLine(firstX + i * 12, y + h / 2, 10, HOT_CORE);
    }
  }

  if (rightEye) tft.fillRect(189, 125, 3, 6, BACKGROUND);
}

void drawTrough(float blinkScalar) {
  tft.fillRoundRect(44, 120, 152, 16, 8, RECESS_DARK);
  tft.drawRoundRect(44, 120, 152, 16, 8, RECESS);

  int pcts[2] = {leftPct, rightPct};
  int centers[2] = {78, 162};
  for (int eye = 0; eye < 2; eye++) {
    int effectivePct = (int)(pcts[eye] * blinkScalar + 0.5f);
    int h = roundHalfUp(effectivePct, 10);
    int width = pcts[eye] > 90 ? 46 : (pcts[eye] <= 30 ? 34 : 40);
    int x = centers[eye] - width / 2;
    if (h == 0) {
      tft.drawFastHLine(centers[eye] - 20, 128, 40, SEAM);
    } else {
      int y = 128 - h / 2;
      tft.fillRoundRect(x, y, width, h, 2, AMBER_3);
      if (h >= 9) tft.drawFastHLine(x + 2, y + h / 2, width - 4, HOT_CORE);
    }
  }
  tft.fillRect(189, 125, 3, 6, BACKGROUND);
}

void drawEyes(float blinkScalar = 1.0f) {
  // Restore only the shared eye region before each blink frame.
  tft.fillRect(40, 114, 160, 28, PLATE);
  if (candidate == ROUNDED_SLOT) {
    drawSlotEye(44, leftPct, false, false, blinkScalar);
    drawSlotEye(134, rightPct, false, true, blinkScalar);
  } else if (candidate == SEGMENTED_SLOT) {
    drawSlotEye(44, leftPct, true, false, blinkScalar);
    drawSlotEye(134, rightPct, true, true, blinkScalar);
  } else {
    drawTrough(blinkScalar);
  }
}

void renderView() {
  drawSharedPlate();
  drawHeader();
  drawFooter();
  drawEyes();
}

void playSynchronizedBlink() {
  // Expression values remain untouched; the shared scalar reaches zero for both.
  for (int i = 10; i >= 0; i--) {
    drawEyes(i / 10.0f);
    delay(10);
  }
  delay(140);
  for (int i = 0; i <= 10; i++) {
    drawEyes(i / 10.0f);
    delay(12);
  }
}

void showBrightnessSwatches() {
  automaticDemo = false;
  tft.fillScreen(BACKGROUND);
  tft.setTextSize(2);
  tft.setTextColor(LABEL, BACKGROUND);
  tft.setCursor(12, 12);
  tft.print("AMBER LEVELS");
  const uint16_t colors[4] = {AMBER_1, AMBER_2, AMBER_3, AMBER_4};
  for (int i = 0; i < 4; i++) {
    int y = 58 + i * 55;
    tft.fillRoundRect(18, y, 150, 32, 7, colors[i]);
    tft.setTextColor(LABEL, BACKGROUND);
    tft.setCursor(180, y + 11);
    tft.print(i + 1);
  }
}

void showPlateSwatches() {
  automaticDemo = false;
  tft.fillScreen(BACKGROUND);
  tft.setTextSize(2);
  tft.setTextColor(LABEL, BACKGROUND);
  tft.setCursor(12, 12);
  tft.print("PLATE COLORS");
  const uint16_t colors[3] = {0x18E5, 0x18E4, 0x18E3};
  for (int i = 0; i < 3; i++) {
    int y = 65 + i * 70;
    tft.fillRoundRect(15, y, 170, 45, 8, colors[i]);
    tft.setTextColor(LABEL, BACKGROUND);
    tft.setCursor(196, y + 16);
    tft.print(5 - i);
  }
}

void applyDemoStep(int step) {
  candidate = (Candidate)demoCandidates[step];
  leftPct = demoLeft[step];
  rightPct = demoRight[step];
  stepStarted = millis();
  blinkPlayedThisStep = false;
  renderView();
}

void applySerialCommand(String cmd) {
  cmd.trim();
  cmd.toUpperCase();
  if (cmd == "AUTO") {
    automaticDemo = true;
    demoStep = 0;
    applyDemoStep(demoStep);
  } else if (cmd == "3" || cmd == "4" || cmd == "6") {
    automaticDemo = false;
    candidate = (Candidate)cmd.toInt();
    renderView();
  } else if (cmd == "0" || cmd == "35" || cmd == "70" || cmd == "100") {
    automaticDemo = false;
    leftPct = rightPct = cmd.toInt();
    renderView();
  } else if (cmd == "A" || cmd == "ASYM") {
    automaticDemo = false;
    leftPct = 75;
    rightPct = 45;
    renderView();
  } else if (cmd == "BLINK") {
    playSynchronizedBlink();
  } else if (cmd == "AMBER") {
    showBrightnessSwatches();
  } else if (cmd == "PLATE") {
    showPlateSwatches();
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(NEXT_BUTTON, INPUT_PULLUP);
  tft.init(240, 320);
  tft.setRotation(0);
  tft.setTextWrap(false);
  applyDemoStep(0);
  Serial.println("Jin eye comparison prototype ready");
  Serial.println("Commands: AUTO, 3, 4, 6, 0, 35, 70, 100, A, BLINK, AMBER, PLATE");
}

void loop() {
  if (Serial.available()) applySerialCommand(Serial.readStringUntil('\n'));

  bool buttonState = digitalRead(NEXT_BUTTON);
  if (buttonState != lastButtonState && millis() - lastButtonChange >= 35) {
    lastButtonChange = millis();
    lastButtonState = buttonState;
    if (buttonState == LOW) {
      automaticDemo = false;
      demoStep = (demoStep + 1) % DEMO_STEPS;
      applyDemoStep(demoStep);
    }
  }

  if (automaticDemo) {
    unsigned long elapsed = millis() - stepStarted;
    if (leftPct == 70 && rightPct == 70 && !blinkPlayedThisStep && elapsed >= 5000) {
      blinkPlayedThisStep = true;
      playSynchronizedBlink();
    }
    if (millis() - stepStarted >= STEP_MS) {
      demoStep = (demoStep + 1) % DEMO_STEPS;
      applyDemoStep(demoStep);
    }
  }
}
