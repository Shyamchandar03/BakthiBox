// Tamil Devotional Player - ESP32 + DFPlayer Mini + PAM8403 + 1.3" OLED
// Stage 6: buttons + volume knob + OLED with Tamil deity names
//
// Libraries: DFRobotDFPlayerMini, U8g2 (by olikraus)
// Keep tamil_names.h in the SAME folder as this .ino file.
//
// Wiring:
//   ESP32 P17 --[1k]--> DFPlayer RX (pin 2)
//   ESP32 P16 <-------- DFPlayer TX (pin 3)
//   ESP32 GND --------> blue (-) rail
//   Buttons: one leg -> pin, diagonal leg -> blue (-) rail
//     P13 Vinayagar (01)  P14 Murugan (02)  P27 Sivan (03)
//     P26 Amman (04)      P25 Perumal (05)  P33 Ayyappan (06)
//     P32 Play / Pause    -- NEVER use P12 or P0
//   Volume knob (10k): left -> 3V3, middle -> P34, right -> GND
//   OLED: VCC -> 3V3, GND -> blue rail, SCL -> P22, SDA -> P21

#include <DFRobotDFPlayerMini.h>
#include <U8g2lib.h>
#include <Wire.h>
#include "tamil_names.h"

HardwareSerial dfSerial(2);
DFRobotDFPlayerMini player;

// 1.3" OLED (SH1106). For a 0.96" SSD1306 use:
// U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);
U8G2_SH1106_128X64_NONAME_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);

const uint8_t DEITY_PINS[6] = {13, 14, 27, 26, 25, 33};
const char* NAMES[6] = {"Vinayagar", "Murugan", "Sivan", "Amman", "Perumal", "Ayyappan"};
const uint8_t PLAY_PIN = 32;
const uint8_t VOL_PIN = 34;
const int MAX_VOL = 25;          // 0-30; raise once you have proper speakers

int folderCount[7];              // songs in folders 01..06 (-1 = unknown)
int curFolder = 0;               // 0 = nothing playing yet
int curSong = 0;
bool paused = false;
int curVol = -1;

bool lastState[7];               // 0-5 deity buttons, 6 = play/pause
unsigned long lastPress = 0;
unsigned long songStart = 0;
unsigned long lastVolCheck = 0;

// screen state
bool screenDirty = true;
const char* message = nullptr;   // short message shown for a few seconds
unsigned long messageUntil = 0;

// ---------------- Display ----------------

void showMessage(const char* msg, unsigned long ms = 2000) {
  message = msg;
  messageUntil = millis() + ms;
  screenDirty = true;
}

void drawCentered(const char* text, int y) {
  int w = oled.getStrWidth(text);
  oled.drawStr((128 - w) / 2, y, text);
}

void drawScreen() {
  oled.clearBuffer();

  // Top: Tamil name (or "பக்தி" before anything is chosen)
  const unsigned char* bmp = (curFolder > 0) ? DEITY_BITMAPS[curFolder - 1] : NAME_BAKTHI;
  oled.drawXBMP(0, 0, NAME_W, NAME_H, bmp);

  // Middle line: song number / message, plus play-pause icon
  oled.setFont(u8g2_font_6x10_tf);
  if (message && millis() < messageUntil) {
    drawCentered(message, 46);
  } else if (curFolder == 0) {
    drawCentered("Press a button", 46);
  } else {
    char buf[20];
    if (folderCount[curFolder] > 0) snprintf(buf, sizeof(buf), "Song %d / %d", curSong, folderCount[curFolder]);
    else                            snprintf(buf, sizeof(buf), "Song %d", curSong);
    oled.drawStr(2, 46, buf);

    if (paused) {                               // pause icon: two bars
      oled.drawBox(112, 37, 3, 10);
      oled.drawBox(118, 37, 3, 10);
    } else {                                    // play icon: triangle
      oled.drawTriangle(112, 37, 112, 47, 121, 42);
    }
  }

  // Bottom: volume bar
  oled.setFont(u8g2_font_5x7_tf);
  oled.drawStr(0, 63, "VOL");
  oled.drawFrame(18, 56, 110, 8);
  int v = curVol < 0 ? 0 : curVol;
  oled.drawBox(20, 58, (106 * v) / MAX_VOL, 4);

  oled.sendBuffer();
  screenDirty = false;
}

// ---------------- Player ----------------

bool pressed(int idx, uint8_t pin) {
  bool now = digitalRead(pin) == LOW;
  bool fire = now && !lastState[idx] && (millis() - lastPress > 200);
  lastState[idx] = now;
  if (fire) lastPress = millis();
  return fire;
}

void playSong(int f, int s) {
  curFolder = f;
  curSong = s;
  paused = false;
  songStart = millis();
  player.playFolder(f, s);
  Serial.printf("%s: folder %02d, song %03d\n", NAMES[f - 1], f, s);
  screenDirty = true;
}

void nextSong() {
  int s = curSong + 1;
  if (folderCount[curFolder] > 0 && s > folderCount[curFolder]) s = 1;
  playSong(curFolder, s);
}

int readKnob() {
  long sum = 0;
  for (int i = 0; i < 8; i++) sum += analogRead(VOL_PIN);
  return map(sum / 8, 0, 4095, 0, MAX_VOL);
}

void updateVolume() {
  int v = readKnob();
  if (v == curVol) return;
  if (curVol >= 0 && abs(v - curVol) == 1) {   // ignore jitter at a step boundary
    delay(5);
    if (readKnob() != v) return;
  }
  curVol = v;
  player.volume(curVol);
  Serial.printf("Volume: %d\n", curVol);
  screenDirty = true;
}

// ---------------- Setup / Loop ----------------

void setup() {
  Serial.begin(115200);

  Wire.begin(21, 22);                          // SDA = P21, SCL = P22
  oled.begin();
  oled.clearBuffer();
  oled.drawXBMP(0, 0, NAME_W, NAME_H, NAME_BAKTHI);
  oled.setFont(u8g2_font_6x10_tf);
  drawCentered("Starting...", 52);
  oled.sendBuffer();

  dfSerial.begin(9600, SERIAL_8N1, 16, 17);
  for (int i = 0; i < 6; i++) pinMode(DEITY_PINS[i], INPUT_PULLUP);
  pinMode(PLAY_PIN, INPUT_PULLUP);
  analogReadResolution(12);

  delay(2000);
  Serial.println("Checking DFPlayer...");
  if (!player.begin(dfSerial, true, true)) {
    Serial.println("DFPlayer NOT RESPONDING - check P16/P17 and GND");
    oled.clearBuffer();
    oled.setFont(u8g2_font_6x10_tf);
    drawCentered("DFPlayer not found", 28);
    drawCentered("Check wiring/SD card", 44);
    oled.sendBuffer();
    while (true) delay(1000);
  }
  Serial.println("DFPlayer OK!");
  updateVolume();
  delay(500);

  for (int f = 1; f <= 6; f++) {
    int c = -1;
    for (int tries = 0; tries < 3 && c < 0; tries++) {
      delay(300);
      c = player.readFileCountsInFolder(f);
    }
    folderCount[f] = c;
    if (c < 0) Serial.printf("Folder %02d (%s): count unknown (will still try)\n", f, NAMES[f - 1]);
    else       Serial.printf("Folder %02d (%s): %d songs\n", f, NAMES[f - 1], c);
  }
  Serial.println("Ready - press a button");
  screenDirty = true;
}

void loop() {
  // Volume knob
  if (millis() - lastVolCheck > 200) {
    lastVolCheck = millis();
    updateVolume();
  }

  // Deity buttons
  for (int i = 0; i < 6; i++) {
    if (pressed(i, DEITY_PINS[i])) {
      int f = i + 1;
      Serial.printf("Button %s pressed\n", NAMES[i]);
      if (folderCount[f] == 0) {
        Serial.printf("Folder %02d is empty\n", f);
        curFolder = f; curSong = 0;
        showMessage("No songs in folder");
        continue;
      }
      if (f == curFolder && curSong > 0) nextSong();
      else playSong(f, 1);
    }
  }

  // Play / Pause
  if (pressed(6, PLAY_PIN)) {
    if (curFolder == 0 || curSong == 0) {
      showMessage("Choose a deity first");
    } else if (paused) {
      player.start();
      paused = false;
      Serial.println("Resume");
      screenDirty = true;
    } else {
      player.pause();
      paused = true;
      Serial.println("Pause");
      screenDirty = true;
    }
  }

  // DFPlayer events
  if (player.available()) {
    uint8_t type = player.readType();
    int value = player.read();
    if (type == DFPlayerPlayFinished && !paused && curSong > 0
        && millis() - songStart > 2000) {
      nextSong();
    } else if (type == DFPlayerError && curSong > 0) {
      Serial.printf("DFPlayer error %d\n", value);
      if (curSong > 1) playSong(curFolder, 1);  // ran past the last song -> back to 1
      else showMessage("Song file not found");
    }
  }

  // Clear an expired message
  if (message && millis() >= messageUntil) {
    message = nullptr;
    screenDirty = true;
  }

  if (screenDirty) drawScreen();
}
