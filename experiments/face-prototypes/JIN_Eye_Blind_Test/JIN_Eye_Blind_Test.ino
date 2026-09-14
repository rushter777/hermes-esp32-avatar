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

const uint16_t BACKGROUND  = 0x0021;
const uint16_t PLATE       = 0x18E4;
const uint16_t PLATE_EDGE  = 0x3188;
const uint16_t RECESS      = 0x0862;
const uint16_t RECESS_DARK = 0x0841;
const uint16_t AMBER_DIM   = 0xABA3;
const uint16_t AMBER       = 0xFD87;

int order[3] = {3, 4, 6};
int viewIndex = 0;
bool lastButtonState = HIGH;
unsigned long lastButtonChange = 0;

int roundHalfUp(int pct, int maximum) {
  if (pct <= 0) return 0;
  return (pct * maximum + 50) / 100;
}

void drawMouth() {
  const int heights[5] = {5, 7, 9, 7, 5};
  tft.fillRoundRect(70, 207, 100, 49, 10, RECESS_DARK);
  for (int i = 0; i < 5; i++) {
    int h = heights[i];
    tft.fillRoundRect(79 + i * 18, 233 - h, 10, h, min(3, h / 2), AMBER_DIM);
  }
}

void drawPlate() {
  tft.fillScreen(BACKGROUND);
  tft.fillRoundRect(20, 48, 200, 220, 28, PLATE);
  tft.drawRoundRect(20, 48, 200, 220, 28, PLATE_EDGE);
  drawMouth();
}

void drawSolidPane(int x, int h) {
  if (h <= 0) return;
  int y = 135 - h;
  int radius = min(2, h / 2);
  if (radius > 0) tft.fillRoundRect(x, y, 56, h, radius, AMBER);
  else tft.fillRect(x, y, 56, h, AMBER);
}

void drawRoundedSlots() {
  int h = roundHalfUp(70, 14);
  tft.fillRoundRect(44, 118, 62, 20, 7, RECESS_DARK);
  tft.drawRoundRect(44, 118, 62, 20, 7, RECESS);
  tft.fillRoundRect(134, 118, 62, 20, 7, RECESS_DARK);
  tft.drawRoundRect(134, 118, 62, 20, 7, RECESS);
  drawSolidPane(47, h);
  drawSolidPane(137, h);
  tft.fillRect(189, 125, 3, 6, BACKGROUND);
}

void drawSegmentedSlots() {
  int h = roundHalfUp(70, 14);
  int y = 135 - h;
  tft.fillRoundRect(44, 118, 62, 20, 7, RECESS_DARK);
  tft.drawRoundRect(44, 118, 62, 20, 7, RECESS);
  tft.fillRoundRect(134, 118, 62, 20, 7, RECESS_DARK);
  tft.drawRoundRect(134, 118, 62, 20, 7, RECESS);

  // Four 12 px cells with 2 px gaps span 54 px inside the same 56 px pane.
  for (int eye = 0; eye < 2; eye++) {
    int firstX = eye == 0 ? 48 : 138;
    for (int cell = 0; cell < 4; cell++) {
      int x = firstX + cell * 14;
      int radius = min(2, h / 2);
      if (radius > 0) tft.fillRoundRect(x, y, 12, h, radius, AMBER);
      else tft.fillRect(x, y, 12, h, AMBER);
    }
  }
  tft.fillRect(189, 125, 3, 6, BACKGROUND);
}

void drawWideTrough() {
  int h = roundHalfUp(70, 10);
  int y = 128 - h / 2;
  tft.fillRoundRect(44, 120, 152, 16, 8, RECESS_DARK);
  tft.drawRoundRect(44, 120, 152, 16, 8, RECESS);
  tft.fillRoundRect(58, y, 40, h, min(2, h / 2), AMBER);
  tft.fillRoundRect(142, y, 40, h, min(2, h / 2), AMBER);
  tft.fillRect(189, 125, 3, 6, BACKGROUND);
}

void showLetter(char letter) {
  tft.fillScreen(BACKGROUND);
  tft.setTextColor(AMBER, BACKGROUND);
  tft.setTextSize(7);
  tft.setCursor(99, 126);
  tft.print(letter);
  delay(900);
}

void showCandidate(int candidate) {
  char letter = 'A' + viewIndex;
  showLetter(letter);
  drawPlate();
  if (candidate == 3) drawRoundedSlots();
  else if (candidate == 4) drawSegmentedSlots();
  else drawWideTrough();
  Serial.print(letter);
  Serial.println(" displayed");
}

void showReveal() {
  tft.fillScreen(BACKGROUND);
  tft.setTextColor(AMBER, BACKGROUND);
  tft.setTextSize(3);
  tft.setCursor(28, 35);
  tft.print("REVEAL");
  tft.setTextSize(4);
  for (int i = 0; i < 3; i++) {
    tft.setCursor(46, 95 + i * 58);
    tft.print((char)('A' + i));
    tft.print(" = C");
    tft.print(order[i]);
  }
  tft.setTextSize(1);
  tft.setCursor(35, 286);
  tft.print("BOOT: NEW BLIND ROUND");
}

void shuffleOrder() {
  order[0] = 3;
  order[1] = 4;
  order[2] = 6;
  for (int i = 2; i > 0; i--) {
    int j = random(i + 1);
    int temp = order[i];
    order[i] = order[j];
    order[j] = temp;
  }
  // Avoid the unshuffled order so the first run is genuinely blind.
  if (order[0] == 3 && order[1] == 4 && order[2] == 6) {
    order[0] = 4;
    order[1] = 6;
    order[2] = 3;
  }
}

void advanceView() {
  viewIndex++;
  if (viewIndex < 3) showCandidate(order[viewIndex]);
  else if (viewIndex == 3) showReveal();
  else {
    shuffleOrder();
    viewIndex = 0;
    showCandidate(order[0]);
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(NEXT_BUTTON, INPUT_PULLUP);
  randomSeed(micros());
  tft.init(240, 320);
  tft.setRotation(0);
  tft.setTextWrap(false);
  shuffleOrder();
  showCandidate(order[0]);
}

void loop() {
  bool buttonState = digitalRead(NEXT_BUTTON);
  if (buttonState != lastButtonState && millis() - lastButtonChange >= 40) {
    lastButtonChange = millis();
    lastButtonState = buttonState;
    if (buttonState == LOW) advanceView();
  }
}
