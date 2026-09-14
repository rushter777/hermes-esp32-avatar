#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>

#define TFT_CS      41
#define TFT_DC      42
#define TFT_RST     47
#define TFT_MOSI    21
#define TFT_SCLK    14
#define NEXT_BUTTON 0

Adafruit_ST7789 tft(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);
GFXcanvas16 eyeCanvas(200, 90);

const uint16_t BACKGROUND  = 0x0021;
const uint16_t OUTER_DARK  = 0x0842;
const uint16_t PLATE       = 0x18E4;
const uint16_t TEXTURE_A   = 0x2105;
const uint16_t TEXTURE_B   = 0x18C4;
const uint16_t EDGE        = 0x426C;
const uint16_t EDGE_HOT    = 0x6371;
const uint16_t RECESS      = 0x0862;
const uint16_t RECESS_DARK = 0x0841;
const uint16_t AMBER_DIM   = 0xABA3;
const uint16_t AMBER       = 0xFD87;
const uint16_t HOT_AMBER   = 0xFE8F;
const uint16_t ICE         = 0x6EBF;

enum Expression {
  NEUTRAL,
  HAPPY,
  AMUSED,
  CURIOUS,
  SKEPTICAL,
  CONCERNED,
  ANNOYED,
  TIRED,
  SURPRISED,
  SPEAKING_DEMO,
  LISTENING,
  PROCESSING
};

struct EyeShape {
  float leftHeight;
  float rightHeight;
  float leftWidth;
  float rightWidth;
  float leftY;
  float rightY;
  float gazeX;
  float leftSlope;
  float rightSlope;
  float radiusTop;
  float radiusBottom;
  float heat;
  float cool;
};

const char *expressionNames[] = {
  "NEUTRAL", "HAPPY", "AMUSED", "CURIOUS", "SKEPTICAL",
  "CONCERNED", "ANNOYED", "TIRED", "SURPRISED", "SPEAKING",
  "LISTENING", "PROCESSING"
};

Expression expression = NEUTRAL;
EyeShape currentShape;
EyeShape startShape;
EyeShape targetShape;
unsigned long transitionStarted = 0;
const unsigned long TRANSITION_MS = 240;

bool lastButtonState = HIGH;
unsigned long lastButtonChange = 0;
unsigned long nextBlink = 0;
unsigned long blinkStarted = 0;
bool blinking = false;
bool autoMode = true;
unsigned long expressionStarted = 0;
unsigned long nextExpressionAt = 0;
unsigned long nextSpeechAt = 0;
unsigned long speechUntil = 0;
int lastMouthFrame = -1;
Expression expressionDeck[10];
int deckPosition = 10;

EyeShape shapeFor(Expression e) {
  // Height, width, vertical position, gaze, edge slope and corner softness
  // are independent channels. Neutral is intentionally near the top of range.
  switch (e) {
    case HAPPY:
      return {12, 12, 60, 60, 134, 134, 0, 0, 0, 6, 0, 0.20, 0.0};
    case AMUSED:
      return {30, 10, 58, 48, 133, 135, 0, 0, 0, 6, 6, 0.0, 0.0};
    case CURIOUS:
      return {34, 22, 50, 60, 132, 135, 4, 0, 0, 10, 10, 0.10, 0.0};
    case SKEPTICAL:
      return {27, 12, 60, 48, 132, 137, -4, 4, 4, 4, 4, -0.20, 0.0};
    case CONCERNED:
      return {24, 24, 58, 58, 135, 135, 0, -6, -6, 7, 7, -0.10, 0.0};
    case ANNOYED:
      return {12, 12, 62, 62, 134, 134, 0, 4, 4, 0, 6, 0.50, 0.0};
    case TIRED:
      return {10, 10, 54, 54, 138, 138, -3, -4, -4, 3, 3, -0.60, 0.0};
    case SURPRISED:
      return {38, 38, 38, 38, 134, 134, 0, 0, 0, 18, 18, 1.0, 0.0};
    case SPEAKING_DEMO:
      return {27, 27, 56, 56, 134, 134, 0, 0, 0, 7, 7, 0.10, 0.0};
    case LISTENING:
      return {30, 30, 54, 54, 131, 131, 0, 0, 0, 6, 6, 0.0, 0.85};
    case PROCESSING:
      return {14, 14, 64, 64, 136, 136, 0, 0, 0, 3, 3, -0.30, 1.0};
    default:
      return {26, 26, 56, 56, 134, 134, 0, 0, 0, 6, 6, 0.0, 0.0};
  }
}

float ease(float t) {
  if (t <= 0) return 0;
  if (t >= 1) return 1;
  return t * t * (3.0f - 2.0f * t);
}

float mixValue(float a, float b, float t) {
  return a + (b - a) * t;
}

EyeShape interpolateShape(EyeShape a, EyeShape b, float t) {
  EyeShape r;
  r.leftHeight = mixValue(a.leftHeight, b.leftHeight, t);
  r.rightHeight = mixValue(a.rightHeight, b.rightHeight, t);
  r.leftWidth = mixValue(a.leftWidth, b.leftWidth, t);
  r.rightWidth = mixValue(a.rightWidth, b.rightWidth, t);
  r.leftY = mixValue(a.leftY, b.leftY, t);
  r.rightY = mixValue(a.rightY, b.rightY, t);
  r.gazeX = mixValue(a.gazeX, b.gazeX, t);
  r.leftSlope = mixValue(a.leftSlope, b.leftSlope, t);
  r.rightSlope = mixValue(a.rightSlope, b.rightSlope, t);
  r.radiusTop = mixValue(a.radiusTop, b.radiusTop, t);
  r.radiusBottom = mixValue(a.radiusBottom, b.radiusBottom, t);
  r.heat = mixValue(a.heat, b.heat, t);
  r.cool = mixValue(a.cool, b.cool, t);
  return r;
}

uint16_t lerp565(uint16_t a, uint16_t b, float t) {
  t = max(0.0f, min(1.0f, t));
  int ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31;
  int br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
  int rr = (int)(ar + (br - ar) * t + 0.5f);
  int rg = (int)(ag + (bg - ag) * t + 0.5f);
  int rb = (int)(ab + (bb - ab) * t + 0.5f);
  return (rr << 11) | (rg << 5) | rb;
}

void drawBackgroundAndPlate() {
  tft.fillScreen(BACKGROUND);
  tft.fillRoundRect(12, 16, 222, 298, 31, 0x0000);
  tft.fillRoundRect(6, 8, 228, 300, 32, OUTER_DARK);
  tft.drawRoundRect(6, 8, 228, 300, 32, EDGE_HOT);
  tft.drawRoundRect(8, 10, 224, 296, 30, EDGE);
  tft.fillRoundRect(13, 15, 214, 286, 27, PLATE);
  tft.drawRoundRect(13, 15, 214, 286, 27, EDGE);
  for (int y = 31; y <= 286; y += 6) {
    tft.drawFastHLine(27, y, 186, ((y / 6) & 1) ? TEXTURE_A : TEXTURE_B);
  }
  tft.drawFastHLine(36, 16, 168, EDGE_HOT);
  tft.drawFastVLine(14, 39, 238, EDGE);
  tft.drawFastHLine(36, 299, 168, 0x0021);
  tft.drawFastVLine(226, 39, 238, 0x0021);

  // Smaller, quieter mouth housing so the eyes remain the primary mass.
  tft.fillRoundRect(67, 211, 106, 48, 11, 0x0021);
  tft.drawRoundRect(67, 211, 106, 48, 11, EDGE);
}

void drawShapedEye(int centerX, float centerY, float widthF, float heightF,
                   float slopeF, float radiusTopF, float radiusBottomF,
                   float heat, float cool, bool rightEye, float blinkScalar) {
  // The housing belongs to the faceplate and never follows the gaze.
  // Only the illuminated eye moves within it.
  int recessCenterX = rightEye ? 142 : 58;
  int recessX = recessCenterX - 36;
  int recessY = 17;
  eyeCanvas.fillRoundRect(recessX, recessY, 72, 48, 10, RECESS_DARK);
  eyeCanvas.drawRoundRect(recessX, recessY, 72, 48, 10, RECESS);

  int width = max(2, (int)(widthF + 0.5f));
  int height = max(0, (int)(heightF * blinkScalar + 0.5f));
  int maxRadius = min(width / 2, height / 2);
  int radiusTop = min((int)(radiusTopF + 0.5f), maxRadius);
  int radiusBottom = min((int)(radiusBottomF + 0.5f), maxRadius);
  int localCenterY = (int)(centerY - 93 + 0.5f);
  int x = centerX - width / 2;
  int y = localCenterY - height / 2;

  if (height > 0) {
    uint16_t eyeColor = heat >= 0.0f
        ? lerp565(AMBER, HOT_AMBER, heat)
        : lerp565(AMBER, AMBER_DIM, -heat);
    eyeColor = lerp565(eyeColor, ICE, cool);
    int radiusBase = max(radiusTop, radiusBottom);
    if (radiusBase > 0) eyeCanvas.fillRoundRect(x, y, width, height, radiusBase, eyeColor);
    else eyeCanvas.fillRect(x, y, width, height, eyeColor);

    // GFX has one radius for all corners. Restore a square top or bottom when
    // the expression asks for different corner behavior at each end.
    if (radiusTop < radiusBottom) {
      eyeCanvas.fillRect(x, y, width, radiusBottom, eyeColor);
    } else if (radiusBottom < radiusTop) {
      eyeCanvas.fillRect(x, y + height - radiusTop, width, radiusTop, eyeColor);
    }

    // Shape the upper edge into a readable inward slope without adding eyebrows.
    int slope = (int)(slopeF + (slopeF >= 0 ? 0.5f : -0.5f));
    if (slope != 0) {
      int depth = min(abs(slope), height);
      if ((!rightEye && slope > 0) || (rightEye && slope < 0)) {
        eyeCanvas.fillTriangle(x, y, x + width - 1, y, x + width - 1, y + depth, RECESS_DARK);
      } else {
        eyeCanvas.fillTriangle(x, y, x + width - 1, y, x, y + depth, RECESS_DARK);
      }
    }

    // Keep expression brightness stable. A highlight tied to eye height caused
    // an unrelated flash whenever a transition crossed an arbitrary threshold.
  }
}

void renderEyes(EyeShape shape, float blinkScalar) {
  eyeCanvas.fillScreen(PLATE);
  // Continue the plate's global texture through the canvas with identical
  // phase, colour parity and width. Canvas origin is screen (20, 93).
  for (int globalY = 97; globalY < 183; globalY += 6) {
    int localY = globalY - 93;
    eyeCanvas.drawFastHLine(7, localY, 186,
                           ((globalY / 6) & 1) ? TEXTURE_A : TEXTURE_B);
  }

  int leftCenter = 58 + (int)shape.gazeX;
  int rightCenter = 142 + (int)shape.gazeX;
  drawShapedEye(leftCenter, shape.leftY, shape.leftWidth, shape.leftHeight,
                shape.leftSlope, shape.radiusTop, shape.radiusBottom,
                shape.heat, shape.cool, false, blinkScalar);
  drawShapedEye(rightCenter, shape.rightY, shape.rightWidth, shape.rightHeight,
                shape.rightSlope, shape.radiusTop, shape.radiusBottom,
                shape.heat, shape.cool, true, blinkScalar);

  // A larger deliberate calibration cut, drawn last so it survives the eye render.
  eyeCanvas.fillRect(171, 30, 6, 14, BACKGROUND);
  tft.drawRGBBitmap(20, 93, eyeCanvas.getBuffer(), 200, 90);
}

void drawMouth(int frame) {
  if (frame == lastMouthFrame) return;
  lastMouthFrame = frame;
  tft.fillRoundRect(68, 212, 104, 46, 10, 0x0021);

  const int heights[5][5] = {
    {5, 7, 9, 7, 5},
    {5, 10, 7, 12, 5},
    {9, 16, 11, 20, 9},
    {15, 24, 17, 29, 15},
    {22, 32, 24, 36, 22}
  };
  uint16_t color = frame == 0 ? AMBER_DIM : AMBER;
  for (int i = 0; i < 5; i++) {
    int h = heights[frame][i];
    int x = 76 + i * 18;
    int y = 235 - h / 2;
    tft.fillRoundRect(x, y, 10, h, min(4, h / 2), color);
  }
}

float currentBlinkScalar(unsigned long now) {
  if (!blinking) return 1.0f;
  unsigned long elapsed = now - blinkStarted;
  if (elapsed < 100) return 1.0f - elapsed / 100.0f;
  if (elapsed < 250) return 0.0f;
  if (elapsed < 380) return (elapsed - 250) / 130.0f;
  blinking = false;
  nextBlink = now + random(4000, 7001);
  return 1.0f;
}

void startExpression(Expression next) {
  startShape = currentShape;
  targetShape = shapeFor(next);
  expression = next;
  transitionStarted = millis();
  expressionStarted = millis();
  nextExpressionAt = millis() + random(600, 1201);
  lastMouthFrame = -1;
  Serial.print("Expression: ");
  Serial.println(expressionNames[(int)expression]);
}

void shuffleExpressionDeck() {
  for (int i = 0; i < 10; i++) expressionDeck[i] = (Expression)i;
  for (int i = 9; i > 0; i--) {
    int j = random(0, i + 1);
    Expression swap = expressionDeck[i];
    expressionDeck[i] = expressionDeck[j];
    expressionDeck[j] = swap;
  }

  // Do not let the reshuffled deck begin with the state just shown.
  if (expressionDeck[0] == expression) {
    Expression swap = expressionDeck[0];
    expressionDeck[0] = expressionDeck[1];
    expressionDeck[1] = swap;
  }
  deckPosition = 0;
}

Expression pickRandomExpression() {
  if (deckPosition >= 10) shuffleExpressionDeck();
  return expressionDeck[deckPosition++];
}

void applySerialCommand(char command) {
  if (command >= 'a' && command <= 'z') command -= 32;
  switch (command) {
    case 'L':
      autoMode = false;
      startExpression(LISTENING);
      break;
    case 'P':
      autoMode = false;
      startExpression(PROCESSING);
      break;
    case 'N':
      autoMode = false;
      startExpression(NEUTRAL);
      break;
    case 'A':
      autoMode = true;
      nextSpeechAt = millis() + 300;
      startExpression(pickRandomExpression());
      break;
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(NEXT_BUTTON, INPUT_PULLUP);
  randomSeed(micros());
  tft.init(240, 320);
  tft.setRotation(0);
  tft.setTextWrap(false);
  drawBackgroundAndPlate();
  currentShape = shapeFor(NEUTRAL);
  startShape = currentShape;
  targetShape = currentShape;
  drawMouth(0);
  renderEyes(currentShape, 1.0f);
  nextBlink = millis() + 3000;
  nextExpressionAt = millis() + 600;
  nextSpeechAt = millis() + 700;
  Serial.println("Jin V5 animated expression prototype ready");
  Serial.println("Expressions and blinks now run automatically");
  Serial.println("Press BOOT to choose another expression immediately");
  Serial.println("Serial: L=listening, P=processing, N=neutral, A=auto");
}

void loop() {
  unsigned long now = millis();

  while (Serial.available()) applySerialCommand((char)Serial.read());

  bool buttonState = digitalRead(NEXT_BUTTON);
  if (buttonState != lastButtonState && now - lastButtonChange >= 40) {
    lastButtonChange = now;
    lastButtonState = buttonState;
    if (buttonState == LOW) {
      autoMode = true;
      startExpression(pickRandomExpression());
    }
  }

  if (autoMode && now >= nextExpressionAt) {
    startExpression(pickRandomExpression());
  }

  // Independent speech demonstration. Later this frame value will come from
  // the TTS audio envelope instead of this timed pattern.
  if (autoMode && now >= nextSpeechAt && now >= speechUntil) {
    speechUntil = now + random(900, 2101);
    nextSpeechAt = speechUntil + random(500, 1501);
    lastMouthFrame = -1;
  }

  if (!blinking && now >= nextBlink) {
    blinking = true;
    blinkStarted = now;
  }

  float progress = ease(min(1.0f, (now - transitionStarted) / (float)TRANSITION_MS));
  currentShape = interpolateShape(startShape, targetShape, progress);
  renderEyes(currentShape, currentBlinkScalar(now));

  bool mouthActive = autoMode && (now < speechUntil || expression == SPEAKING_DEMO);
  if (mouthActive) {
    const uint8_t speechPattern[] = {1, 3, 2, 4, 2, 1, 0, 2, 4, 3, 1, 0};
    int frame = speechPattern[((now / 90) % 12)];
    drawMouth(frame);
  } else {
    drawMouth(0);
  }
  delay(35);
}
