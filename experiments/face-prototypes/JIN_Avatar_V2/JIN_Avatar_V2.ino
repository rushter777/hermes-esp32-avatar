#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <math.h>

#define TFT_CS    41
#define TFT_DC    42
#define TFT_RST   47
#define TFT_MOSI  21
#define TFT_SCLK  14

Adafruit_ST7789 tft(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);

enum JinState {
  READY,
  LISTENING,
  PROCESSING,
  SPEAKING,
  WAITING,
  WARNING_STATE,
  ERROR_STATE
};

enum JinEmotion {
  NEUTRAL,
  HAPPY,
  AMUSED,
  CURIOUS,
  CONCERNED,
  SURPRISED,
  ANNOYED,
  TIRED
};

JinState state = READY;
JinEmotion emotion = NEUTRAL;

const uint16_t GRAPHITE   = 0x0861;
const uint16_t PLATE      = 0x10C3;
const uint16_t BEVEL      = 0x2988;
const uint16_t SHADOW     = 0x0021;
const uint16_t RECESS     = 0x0841;
const uint16_t AMBER      = 0xFD24;
const uint16_t HOT_AMBER  = 0xFE8F;
const uint16_t DIM_AMBER  = 0x5140;
const uint16_t ICE        = 0x6EBF;
const uint16_t MINT       = 0x4714;
const uint16_t CRIMSON    = 0xFA69;
const uint16_t GRILLE     = 0x3A09;

const int PLATE_X = 14;
const int PLATE_Y = 18;
const int PLATE_W = 212;
const int PLATE_H = 286;

unsigned long lastFrame = 0;
unsigned long nextBlink = 0;
unsigned long blinkStarted = 0;
unsigned long stateStarted = 0;
bool blinking = false;
int lastBeatX = -1;
int previousWaveY[180];
int lastMouthFrame = -1;
int lastLeftAperture = -1;
int lastRightAperture = -1;

uint16_t activeColor() {
  if (state == LISTENING) return ICE;
  if (state == WARNING_STATE || state == ERROR_STATE) return CRIMSON;
  return AMBER;
}

int tempoBPM() {
  if (state == PROCESSING || emotion == ANNOYED) return 120;
  if (state == WAITING) return 45;
  if (emotion == CURIOUS) return 72;
  if (emotion == CONCERNED) return 84;
  if (emotion == TIRED) return 48;
  if (emotion == AMUSED) return 66;
  return 60;
}

int baseAperture() {
  if (state == LISTENING) return 80;
  if (state == PROCESSING) return 35;
  if (state == ERROR_STATE) return 5;
  switch (emotion) {
    case HAPPY: return 85;
    case AMUSED: return 75;
    case CURIOUS: return 90;
    case CONCERNED: return 55;
    case SURPRISED: return 100;
    case ANNOYED: return 30;
    case TIRED: return 40;
    default: return 70;
  }
}

void drawBackground() {
  tft.fillScreen(GRAPHITE);
  for (int y = 0; y < 320; y += 3) {
    tft.drawFastHLine(0, y, 240, 0x10A2);
  }
}

void drawFaceplate() {
  // Soft outer ember and a deeper, fuller graphite chassis.
  tft.drawRoundRect(PLATE_X - 2, PLATE_Y - 2, PLATE_W + 4, PLATE_H + 4, 29, DIM_AMBER);
  tft.fillRoundRect(PLATE_X, PLATE_Y, PLATE_W, PLATE_H, 27, PLATE);
  tft.drawRoundRect(PLATE_X, PLATE_Y, PLATE_W, PLATE_H, 27, BEVEL);
  tft.drawRoundRect(PLATE_X + 3, PLATE_Y + 3, PLATE_W - 6, PLATE_H - 6, 24, SHADOW);
  tft.drawFastHLine(PLATE_X + 27, PLATE_Y, PLATE_W - 54, HOT_AMBER);
  tft.drawFastVLine(PLATE_X, PLATE_Y + 27, PLATE_H - 54, BEVEL);

  // Deep shutter housings.
  tft.fillRoundRect(32, 95, 72, 26, 8, SHADOW);
  tft.drawRoundRect(32, 95, 72, 26, 8, BEVEL);
  tft.fillRoundRect(136, 95, 72, 26, 8, SHADOW);
  tft.drawRoundRect(136, 95, 72, 26, 8, BEVEL);
  tft.fillRect(203, 93, 4, 8, GRAPHITE);

  // Separate recessed mouth instrument and speaker grille.
  tft.fillRoundRect(65, 157, 110, 59, 10, RECESS);
  tft.drawRoundRect(65, 157, 110, 59, 10, SHADOW);
  tft.fillRoundRect(48, 221, 144, 38, 7, SHADOW);
  tft.drawRoundRect(48, 221, 144, 38, 7, BEVEL);
  for (int i = 0; i < 3; i++) {
    tft.fillRoundRect(58, 229 + i * 10, 124, 5, 2, GRILLE);
  }

  // The lower band belongs to the clock waveform, not the mouth.
  tft.drawFastHLine(29, 294, 182, SHADOW);
}

void drawEyeSlot(int x, int percent, uint16_t color, bool rightEye) {
  tft.fillRoundRect(x, 95, 72, 26, 8, SHADOW);
  tft.drawRoundRect(x, 95, 72, 26, 8, BEVEL);

  if (state == ERROR_STATE) {
    tft.drawFastHLine(x + 7, 108, 24, CRIMSON);
    tft.drawFastHLine(x + 40, 108, 24, CRIMSON);
  } else if (percent > 0) {
    int height = max(2, (percent * 16) / 100);
    int y = 108 - height / 2;
    tft.fillRoundRect(x + 5, y - 4, 62, height + 8, 5, DIM_AMBER);
    tft.fillRoundRect(x + 7, y - 2, 58, height + 4, 4, color);
    tft.fillRoundRect(x + 9, y, 54, height, 3, color);
    tft.drawFastHLine(x + 10, y + height / 2, 52, HOT_AMBER);
    if (emotion == CURIOUS) tft.drawFastVLine(x + 36, y, height, SHADOW);
  }

  if (rightEye) tft.fillRect(203, 93, 4, 8, GRAPHITE);
}

void drawEyes(int aperture) {
  int left = aperture;
  int right = aperture;
  if (emotion == AMUSED) right = 45;
  if (state == SPEAKING) {
    left = min(100, left + 15);
    right = min(100, right + 15);
  }
  if (blinking) left = right = 0;

  if (left != lastLeftAperture || right != lastRightAperture) {
    drawEyeSlot(32, left, activeColor(), false);
    drawEyeSlot(136, right, activeColor(), true);
    lastLeftAperture = left;
    lastRightAperture = right;
  }
}

void drawMouth(int frame) {
  if (frame == lastMouthFrame) return;
  lastMouthFrame = frame;
  tft.fillRoundRect(66, 158, 108, 57, 9, RECESS);

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
    int x = 72 + i * 20;
    int y = 187 - h / 2;
    tft.fillRoundRect(x - 2, y - 3, 14, h + 6, 6, DIM_AMBER);
    tft.fillRoundRect(x, y, 10, h, min(5, h / 2), frame == 0 ? DIM_AMBER : color);
    if (frame >= 3) tft.drawFastVLine(x + 5, y + 2, max(1, h - 4), HOT_AMBER);
  }
}

void drawGrille(int level) {
  uint16_t color = level > 0 ? activeColor() : GRILLE;
  tft.fillRoundRect(49, 222, 142, 36, 6, SHADOW);
  for (int i = 0; i < 3; i++) {
    int width = level > 0 ? 30 + (level * (i + 1)) / 2 : 124;
    width = min(width, 124);
    tft.fillRoundRect(58, 229 + i * 10, width, 5, 2, color);
  }
}

void updateBlink(unsigned long now) {
  if (!blinking && now >= nextBlink) {
    blinking = true;
    blinkStarted = now;
    lastLeftAperture = lastRightAperture = -1;
  }
  if (blinking && now - blinkStarted >= 130) {
    blinking = false;
    nextBlink = now + random(4000, 7001);
    lastLeftAperture = lastRightAperture = -1;
  }
}

void updateBeat(unsigned long now) {
  uint16_t eraseColor = PLATE;
  for (int i = 0; i < 180; i++) {
    if (previousWaveY[i] >= 0) tft.drawPixel(30 + i, previousWaveY[i], eraseColor);
  }

  if (state == ERROR_STATE) return;

  unsigned long period = 60000UL / tempoBPM();
  float phase = 2.0f * PI * (now % period) / period;
  int amplitude = state == SPEAKING ? 8 : (state == PROCESSING ? 4 : 2);
  uint16_t color = activeColor();

  for (int i = 0; i < 180; i++) {
    float xPhase = (2.0f * PI * i / 60.0f) - phase;
    int y = 282 + (int)(sinf(xPhase) * amplitude);
    previousWaveY[i] = y;
    tft.drawPixel(30 + i, y, color);
  }

  int pulseX = (int)((now % period) * 179 / period);
  tft.fillCircle(30 + pulseX, previousWaveY[pulseX], 1, HOT_AMBER);
}

void updateAnimation(unsigned long now) {
  updateBlink(now);
  drawEyes(baseAperture());
  updateBeat(now);

  if (state == PROCESSING) {
    drawMouth(1 + ((now / 180) % 3));
    drawGrille(0);
  } else if (state == SPEAKING) {
    int frame = 1 + ((now / 95) % 4);
    drawMouth(frame);
    drawGrille(frame * 18);
  } else {
    drawMouth(0);
    drawGrille(0);
  }
}

void powerOnSequence() {
  drawBackground();
  for (int i = 0; i < 180; i++) previousWaveY[i] = -1;
  delay(450);

  for (int beat = 0; beat < 4; beat++) {
    int x = 48 + beat * 48;
    tft.fillRect(x, 294, beat == 0 ? 5 : 2, 1, AMBER);
    delay(150);
  }

  tft.drawFastHLine(14, 160, 212, AMBER);
  delay(120);
  drawFaceplate();

  int steps[] = {0, 40, 70};
  for (int i = 0; i < 3; i++) {
    lastLeftAperture = lastRightAperture = -1;
    drawEyes(steps[i]);
    delay(130);
  }

  blinking = true;
  lastLeftAperture = lastRightAperture = -1;
  drawEyes(0);
  delay(100);
  blinking = false;
  lastLeftAperture = lastRightAperture = -1;
  drawEyes(70);
  drawMouth(0);
  drawGrille(0);
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

  stateStarted = millis();
  lastMouthFrame = -1;
  lastLeftAperture = lastRightAperture = -1;
  drawFaceplate();
}

void setup() {
  Serial.begin(115200);
  delay(800);
  randomSeed(micros());

  tft.init(240, 320);
  tft.setRotation(0);
  tft.setTextWrap(false);

  powerOnSequence();
  nextBlink = millis() + random(4000, 7001);
  stateStarted = millis();

  Serial.println("Jin avatar ready");
  Serial.println("Commands: READY LISTENING PROCESSING SPEAKING WAITING WARNING ERROR");
  Serial.println("Emotions: NEUTRAL HAPPY AMUSED CURIOUS CONCERNED SURPRISED ANNOYED TIRED");
}

void loop() {
  unsigned long now = millis();

  if (Serial.available()) {
    applyCommand(Serial.readStringUntil('\n'));
  }

  if (now - lastFrame >= 25) {
    lastFrame = now;
    updateAnimation(now);
  }
}
