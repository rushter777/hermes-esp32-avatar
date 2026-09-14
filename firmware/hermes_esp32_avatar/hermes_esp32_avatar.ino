#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <ESP_I2S.h>
#include <ESPmDNS.h>
#include <SPI.h>
#include <WiFi.h>
#include "secrets.h"

#define TFT_CS      41
#define TFT_DC      42
#define TFT_RST     47
#define TFT_MOSI    21
#define TFT_SCLK    14
#define NEXT_BUTTON 0

#define AMP_BCLK 38
#define AMP_LRC  39
#define AMP_DIN  40

const uint16_t AUDIO_PORT = 3333;
const uint32_t AUDIO_RATE = 16000;
// Fixed measured latency from the first queued I2S sample to the speaker.
// Later mouth frames are scheduled from sample position, not packet arrival.
const uint32_t AUDIO_OUTPUT_LATENCY_MS = 32;
const uint8_t MOUTH_EVENT_QUEUE_SIZE = 128;
// Timing-isolation test: draw neutral eyes once and give the mouth exclusive
// use of the display during audio playback.
const bool STATIC_EYES = true;

Adafruit_ST7789 tft(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);
GFXcanvas16 eyeCanvas(200, 90);
GFXcanvas16 mouthCanvas(104, 46);
I2SClass audio;
WiFiServer audioServer(AUDIO_PORT);

volatile uint8_t audioMouthFrame = 0;
volatile bool audioStreaming = false;
volatile bool audioServerReady = false;

struct MouthEvent {
  uint32_t atMs;
  uint8_t frame;
};

MouthEvent mouthEvents[MOUTH_EVENT_QUEUE_SIZE];
volatile uint8_t mouthEventRead = 0;
volatile uint8_t mouthEventWrite = 0;

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
bool autoMode = false;
unsigned long expressionStarted = 0;
unsigned long nextExpressionAt = 0;
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
  // Compose the entire mouth offscreen, then transfer it as one frame. This
  // prevents the cleared cavity from flashing between individual bar draws.
  mouthCanvas.fillScreen(0x0021);

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
    int x = 8 + i * 18;
    int y = 235 - h / 2;
    mouthCanvas.fillRoundRect(x, y - 212, 10, h, min(4, h / 2), color);
  }
  tft.drawRGBBitmap(68, 212, mouthCanvas.getBuffer(), 104, 46);
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
      startExpression(pickRandomExpression());
      break;
  }
}

void audioNetworkTask(void *parameter) {
  // The Mac sends mono signed 16-bit PCM at 16 kHz. Duplicate each sample to
  // both I2S slots so the MAX98357A works regardless of its channel selection.
  int16_t mono[256];
  int16_t stereo[512];
  float envelope = 0.0f;

  audioServer.begin();
  audioServer.setNoDelay(true);
  audioServerReady = true;

  for (;;) {
    WiFiClient client = audioServer.accept();
    if (!client) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    client.setNoDelay(true);
    client.setTimeout(2000);

    uint8_t header[8];
    size_t got = client.readBytes(header, sizeof(header));
    bool valid = got == sizeof(header) &&
                 header[0] == 'J' && header[1] == 'I' &&
                 header[2] == 'N' && header[3] == '1';
    uint32_t rate = (uint32_t)header[4] |
                    ((uint32_t)header[5] << 8) |
                    ((uint32_t)header[6] << 16) |
                    ((uint32_t)header[7] << 24);
    if (!valid || rate != AUDIO_RATE) {
      client.stop();
      continue;
    }

    audioStreaming = true;
    envelope = 0.0f;
    mouthEventRead = mouthEventWrite = 0;
    uint32_t playbackEpoch = millis() + AUDIO_OUTPUT_LATENCY_MS;
    uint32_t samplesScheduled = 0;

    while (client.connected() || client.available()) {
      int availableBytes = client.available();
      if (availableBytes < (int)sizeof(mono) && client.connected()) {
        vTaskDelay(pdMS_TO_TICKS(1));
        continue;
      }

      int wanted = min(availableBytes & ~1, (int)sizeof(mono));
      if (wanted < 2) break;
      int received = client.readBytes((uint8_t *)mono, wanted);
      if (received <= 0) continue;
      int sampleCount = received / sizeof(int16_t);
      int peak = 0;

      for (int i = 0; i < sampleCount; i++) {
        int value = mono[i];
        int magnitude = value == INT16_MIN ? 32767 : abs(value);
        if (magnitude > peak) peak = magnitude;
        stereo[i * 2] = mono[i];
        stereo[i * 2 + 1] = mono[i];
      }

      audio.write(stereo, sampleCount * 2 * sizeof(int16_t));

      float instant = peak / 32767.0f;
      if (instant > envelope) envelope += (instant - envelope) * 0.80f;
      else envelope += (instant - envelope) * 0.35f;

      // A small noise floor keeps silence at the resting mouth frame.
      uint8_t nextFrame;
      if (envelope < 0.025f) nextFrame = 0;
      else if (envelope < 0.08f) nextFrame = 1;
      else if (envelope < 0.18f) nextFrame = 2;
      else if (envelope < 0.36f) nextFrame = 3;
      else nextFrame = 4;

      // Timestamp this mouth frame from its sample position in the stream.
      // This prevents TCP packet timing from creating cumulative A/V drift.
      uint8_t nextWrite = (mouthEventWrite + 1) % MOUTH_EVENT_QUEUE_SIZE;
      if (nextWrite != mouthEventRead) {
        mouthEvents[mouthEventWrite].atMs = playbackEpoch +
            (uint32_t)(((uint64_t)samplesScheduled * 1000ULL) / AUDIO_RATE);
        mouthEvents[mouthEventWrite].frame = nextFrame;
        mouthEventWrite = nextWrite;
      }
      samplesScheduled += sampleCount;
    }

    int16_t silence[2] = {0, 0};
    for (int i = 0; i < 320; i++) audio.write(silence, sizeof(silence));
    audioMouthFrame = 0;
    audioStreaming = false;
    client.stop();
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

  audio.setPins(AMP_BCLK, AMP_LRC, AMP_DIN);
  if (!audio.begin(I2S_MODE_STD, AUDIO_RATE,
                   I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
    Serial.println("I2S amplifier initialization failed");
    while (true) delay(1000);
  }

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting Jin to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print('.');
  }
  Serial.println();
  Serial.print("Jin IP: ");
  Serial.println(WiFi.localIP());
  if (MDNS.begin("jin")) Serial.println("Audio address: jin.local:3333");
  xTaskCreatePinnedToCore(audioNetworkTask, "jin-audio", 8192, nullptr, 2, nullptr, 0);
  nextBlink = millis() + 3000;
  nextExpressionAt = millis() + 600;
  Serial.println("Jin V5 animated expression prototype ready");
  Serial.println("Expressions and blinks now run automatically");
  Serial.println("Press BOOT to choose another expression immediately");
  Serial.println("Serial: L=listening, P=processing, N=neutral, A=auto");
}

void loop() {
  unsigned long now = millis();
  static bool wasStreaming = false;
  static unsigned long lastEyeRender = 0;

  while (mouthEventRead != mouthEventWrite &&
         (int32_t)(now - mouthEvents[mouthEventRead].atMs) >= 0) {
    audioMouthFrame = mouthEvents[mouthEventRead].frame;
    mouthEventRead = (mouthEventRead + 1) % MOUTH_EVENT_QUEUE_SIZE;
  }

  if (audioStreaming != wasStreaming) {
    wasStreaming = audioStreaming;
    if (!STATIC_EYES) {
      if (wasStreaming) {
        autoMode = false;
        startExpression(SPEAKING_DEMO);
      } else {
        autoMode = true;
        startExpression(NEUTRAL);
      }
    }
  }

  while (Serial.available()) {
    char command = (char)Serial.read();
    if (!STATIC_EYES) applySerialCommand(command);
  }

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

  if (!STATIC_EYES && !blinking && now >= nextBlink) {
    blinking = true;
    blinkStarted = now;
  }

  float progress = ease(min(1.0f, (now - transitionStarted) / (float)TRANSITION_MS));
  currentShape = interpolateShape(startShape, targetShape, progress);

  // Audio owns the mouth completely. Silence is always the flat resting frame.
  // Draw it before the larger eye bitmap so eye work cannot delay the response.
  if (audioStreaming) {
    drawMouth(audioMouthFrame);
  } else {
    drawMouth(0);
  }

  // The speaking eyes are held steady. Refresh them less often while audio is
  // active, leaving most display time available for the mouth meter.
  unsigned long eyeInterval = audioStreaming ? 100 : 35;
  if (!STATIC_EYES && now - lastEyeRender >= eyeInterval) {
    renderEyes(currentShape, currentBlinkScalar(now));
    lastEyeRender = now;
  }
  delay(audioStreaming ? 8 : 35);
}
