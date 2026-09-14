#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>

#define TFT_CS    41
#define TFT_DC    42
#define TFT_RST   47
#define TFT_MOSI  21
#define TFT_SCLK  14

Adafruit_ST7789 tft(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);

enum JinState { READY, LISTENING, PROCESSING, SPEAKING, WAITING, WARNING_STATE, ERROR_STATE };
enum JinEmotion { NEUTRAL, HAPPY, AMUSED, CURIOUS, CONCERNED, SURPRISED, ANNOYED, TIRED };

JinState state = READY;
JinEmotion emotion = NEUTRAL;

const uint16_t BACKGROUND = 0x0021;
const uint16_t PLATE      = 0x10C3;
const uint16_t BEVEL      = 0x2988;
const uint16_t SHADOW     = 0x0021;
const uint16_t RECESS     = 0x0841;
const uint16_t AMBER      = 0xFD24;
const uint16_t HOT_AMBER  = 0xFE8F;
const uint16_t DIM_AMBER  = 0x5140;
const uint16_t ICE        = 0x6EBF;
const uint16_t CRIMSON    = 0xFA69;

const int PLATE_X = 12;
const int PLATE_Y = 14;
const int PLATE_W = 216;
const int PLATE_H = 292;

// One small off-screen strip contains both eyes. Pushing the strip as one image
// prevents the left and right blink from appearing on separate display updates.
GFXcanvas16 eyeStrip(196, 44);

unsigned long lastFrame = 0;
unsigned long nextBlink = 0;
unsigned long blinkStarted = 0;
bool blinking = false;
int lastEyePercent = -1;
int lastMouthFrame = -1;

uint16_t activeColor() {
  if (state == LISTENING) return ICE;
  if (state == WARNING_STATE || state == ERROR_STATE) return CRIMSON;
  return AMBER;
}

int baseAperture() {
  if (state == LISTENING) return 82;
  if (state == PROCESSING) return 38;
  if (state == ERROR_STATE) return 8;
  switch (emotion) {
    case HAPPY: return 86;
    case AMUSED: return 68;
    case CURIOUS: return 92;
    case CONCERNED: return 54;
    case SURPRISED: return 100;
    case ANNOYED: return 30;
    case TIRED: return 42;
    default: return 72;
  }
}

void drawBackground() {
  tft.fillScreen(BACKGROUND);
  for (int y = 0; y < 320; y += 4) tft.drawFastHLine(0, y, 240, 0x0841);
}

void drawFaceplate() {
  tft.fillRoundRect(PLATE_X, PLATE_Y, PLATE_W, PLATE_H, 28, PLATE);
  tft.drawRoundRect(PLATE_X, PLATE_Y, PLATE_W, PLATE_H, 28, BEVEL);
  tft.drawRoundRect(PLATE_X + 2, PLATE_Y + 2, PLATE_W - 4, PLATE_H - 4, 26, SHADOW);

  // Jin's single small asymmetric index notch. This is decoration, not another feature.
  tft.fillRect(211, 82, 3, 8, DIM_AMBER);

  // One mouth recess only. No grille and no second waveform.
  tft.fillRoundRect(57, 187, 126, 70, 12, RECESS);
  tft.drawRoundRect(57, 187, 126, 70, 12, SHADOW);
}

int blinkAperture(unsigned long now, int normal) {
  if (!blinking) return normal;
  unsigned long elapsed = now - blinkStarted;
  if (elapsed < 60) return normal * (60 - elapsed) / 60;
  if (elapsed < 100) return 0;
  if (elapsed < 190) return normal * (elapsed - 100) / 90;
  blinking = false;
  nextBlink = now + random(4000, 7001);
  return normal;
}

void beginBlink(unsigned long now) {
  if (!blinking && now >= nextBlink) {
    blinking = true;
    blinkStarted = now;
  }
}

void drawEyes(int percent) {
  if (percent == lastEyePercent) return;
  lastEyePercent = percent;

  eyeStrip.fillScreen(PLATE);
  uint16_t color = activeColor();
  const int slotY = 8;
  const int slotW = 70;
  const int slotH = 28;
  const int leftX = 5;
  const int rightX = 121;

  eyeStrip.fillRoundRect(leftX, slotY, slotW, slotH, 8, SHADOW);
  eyeStrip.drawRoundRect(leftX, slotY, slotW, slotH, 8, BEVEL);
  eyeStrip.fillRoundRect(rightX, slotY, slotW, slotH, 8, SHADOW);
  eyeStrip.drawRoundRect(rightX, slotY, slotW, slotH, 8, BEVEL);

  if (state == ERROR_STATE) {
    eyeStrip.drawFastHLine(leftX + 7, 22, 23, CRIMSON);
    eyeStrip.drawFastHLine(leftX + 40, 22, 23, CRIMSON);
    eyeStrip.drawFastHLine(rightX + 7, 22, 23, CRIMSON);
    eyeStrip.drawFastHLine(rightX + 40, 22, 23, CRIMSON);
  } else if (percent > 0) {
    int h = max(1, percent * 18 / 100);
    int y = 22 - h / 2;
    for (int x : {leftX, rightX}) {
      eyeStrip.fillRoundRect(x + 5, y - 3, 60, h + 6, 5, DIM_AMBER);
      eyeStrip.fillRoundRect(x + 7, y - 1, 56, h + 2, 4, color);
      eyeStrip.drawFastHLine(x + 9, y + h / 2, 52, HOT_AMBER);
      if (emotion == CURIOUS) eyeStrip.drawFastVLine(x + 35, y, h, SHADOW);
    }
  }

  // The two eyes cross the SPI bus together in one compact transfer.
  tft.drawRGBBitmap(22, 78, eyeStrip.getBuffer(), 196, 44);
}

void drawMouth(int frame) {
  if (frame == lastMouthFrame) return;
  lastMouthFrame = frame;
  tft.fillRoundRect(58, 188, 124, 68, 11, RECESS);

  if (frame == 0) {
    tft.fillRoundRect(77, 220, 86, 3, 1, DIM_AMBER);
    return;
  }

  const int heights[5][5] = {
    {2, 2, 2, 2, 2},
    {4, 8, 4, 8, 4},
    {8, 14, 8, 20, 8},
    {14, 22, 14, 30, 14},
    {22, 32, 22, 38, 22}
  };
  uint16_t color = activeColor();
  for (int i = 0; i < 5; i++) {
    int h = heights[frame][i];
    int x = 76 + i * 18;
    int y = 222 - h / 2;
    tft.fillRoundRect(x, y, 10, h, min(5, h / 2), color);
    if (frame >= 3) tft.drawFastVLine(x + 5, y + 2, max(1, h - 4), HOT_AMBER);
  }
}

void updateAnimation(unsigned long now) {
  beginBlink(now);
  int aperture = blinkAperture(now, baseAperture());
  drawEyes(aperture);

  if (state == PROCESSING) drawMouth(1 + ((now / 220) % 3));
  else if (state == SPEAKING) drawMouth(1 + ((now / 95) % 4));
  else drawMouth(0);
}

void powerOnSequence() {
  drawBackground();
  delay(350);
  drawFaceplate();
  drawEyes(0);
  drawMouth(0);
  delay(180);
  for (int aperture : {25, 50, 72}) {
    lastEyePercent = -1;
    drawEyes(aperture);
    delay(110);
  }
}

void applyCommand(String command) {
  command.trim();
  command.toUpperCase();

  if (command == "READY") state = READY;
  else if (command == "LISTENING") state = LISTENING;
  else if (command == "PROCESSING" || command == "THINKING") state = PROCESSING;
  else if (command == "SPEAKING") state = SPEAKING;
  else if (command == "WAITING") state = WAITING;
  else if (command == "WARNING") state = WARNING_STATE;
  else if (command == "ERROR") state = ERROR_STATE;
  else if (command == "NEUTRAL") emotion = NEUTRAL;
  else if (command == "HAPPY") emotion = HAPPY;
  else if (command == "AMUSED") emotion = AMUSED;
  else if (command == "CURIOUS") emotion = CURIOUS;
  else if (command == "CONCERNED") emotion = CONCERNED;
  else if (command == "SURPRISED") emotion = SURPRISED;
  else if (command == "ANNOYED") emotion = ANNOYED;
  else if (command == "TIRED") emotion = TIRED;
  else return;

  lastEyePercent = -1;
  lastMouthFrame = -1;
}

void setup() {
  Serial.begin(115200);
  delay(500);
  randomSeed(micros());
  tft.init(240, 320);
  tft.setRotation(0);
  tft.setTextWrap(false);

  powerOnSequence();
  nextBlink = millis() + random(4000, 7001);
  Serial.println("Jin avatar V3 ready");
  Serial.println("Commands: READY LISTENING PROCESSING SPEAKING WAITING WARNING ERROR");
  Serial.println("Emotions: NEUTRAL HAPPY AMUSED CURIOUS CONCERNED SURPRISED ANNOYED TIRED");
}

void loop() {
  unsigned long now = millis();
  if (Serial.available()) applyCommand(Serial.readStringUntil('\n'));
  if (now - lastFrame >= 20) {
    lastFrame = now;
    updateAnimation(now);
  }
}
