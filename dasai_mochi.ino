/* ==========================================================================
   DASAI MOCHI - ESP32 Interactive Desktop Pet
   ==========================================================================
   Fitur:
   - 3 ekspresi utama: IDLE (melirik 4 arah), DIELUS (mata hati + blush),
     NGANTUK (kumis + hidung, otomatis setelah 1 menit diam, HARUS ditekan
     tombol buat bangun, dengan animasi mata membuka pelan-pelan)
   - Marah kalau tombol di-spam klik
   - Efek suara pakai BUZZER AKTIF (ON/OFF pakai pola ketukan, bukan nada
     tinggi-rendah): ada suara pas dielus, ketiduran, bangun, marah, pas
     milih angka di kalkulator, dan musik latar (detak ringan) + suara
     lompat di game Dino Run
   - Jam manual + Kalkulator sederhana
   - Mini game: Dino Run

   NAVIGASI -- HANYA 2 TOMBOL:
   - Tombol 1 "MENU" (GPIO 32): tap SINGKAT = konfirmasi/masuk ke pilihan
     yang lagi ditunjuk. Tekan TAHAN (>600ms) = kembali ke layar sebelumnya.
   - Tombol 2 "OK" (GPIO 25): geser-geser pilihan (menu/track/angka), dan
     jadi tombol lompat di game Dino Run.

   CATATAN BUG TOUCH SENSOR:
   Kalau modul touch kamu justru nyala/aktif pas TIDAK disentuh dan mati
   pas DISENTUH (kebalikan dari standar TTP223), ubah baris ini:
       #define TOUCH_ACTIVE_HIGH true
   jadi:
       #define TOUCH_ACTIVE_HIGH false

   LIBRARY: Adafruit GFX Library, Adafruit SSD1306

   WIRING:
   - OLED SSD1306 128x64 (I2C)  -> SDA 21, SCL 22
   - Buzzer AKTIF (punya osilator sendiri) -> pin (+) ke GPIO 17, pin (-) ke GND
   - Tombol MENU                -> GPIO 32 (ke GND, INPUT_PULLUP)
   - Tombol OK                  -> GPIO 25 (ke GND, INPUT_PULLUP)
   - Touch sensor                -> GPIO 27
   ========================================================================== */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ---------------------------- PIN CONFIG ----------------------------------
#define OLED_SDA   21
#define OLED_SCL   22
#define SCREEN_W   128
#define SCREEN_H   64
#define OLED_ADDR  0x3C

#define BUZZER_PIN 17

#define BTN_MENU   32
#define BTN_OK     25
#define TOUCH_PIN  27

#define TOUCH_ACTIVE_HIGH true // ganti ke false kalau modul touch kamu kebalik (lihat catatan di atas)

// ---------------------------- OBJEK GLOBAL ---------------------------------
Adafruit_SSD1306 display(SCREEN_W, SCREEN_H, &Wire, -1);

// ---------------------------- STATE MACHINE --------------------------------
enum AppState { STATE_MOCHI, STATE_MENU, STATE_CLOCK, STATE_CALC, STATE_GAME_DINO };
AppState state = STATE_MOCHI;

enum Mood { MOOD_NORMAL, MOOD_SLEEPY, MOOD_HAPPY, MOOD_ANGRY };
Mood mood = MOOD_NORMAL;

unsigned long lastInteractionMs = 0;
unsigned long moodUntilMs = 0;
const unsigned long SLEEP_TIMEOUT      = 60000; // 1 menit diam -> ngantuk
const unsigned long HAPPY_DURATION     = 1500;
const unsigned long ANGRY_DURATION     = 3000;
const unsigned long WAKE_ANIM_DURATION = 700;

bool isAsleep = false;
bool isWakingUp = false;
unsigned long wakeStartMs = 0;

// ---------------------------- BUZZER / EFEK SUARA ---------------------------
// Buzzer AKTIF cuma bisa ON/OFF (nggak bisa diatur nada tinggi-rendahnya),
// jadi tiap efek dibedain lewat POLA ketukan: sederet durasi bunyi/diam
// bergantian. Indeks genap (0,2,4,...) = bunyi (ON), indeks ganjil = diam.
// Semua non-blocking (dijalankan lewat millis(), bukan delay()).
bool patternRunning = false;
bool patternLoop = false;
const int* activePattern = nullptr;
int patternLen = 0;
int patternIndex = 0;
unsigned long patternStepStartMs = 0;

bool melodyPlaying = false;      // status "harusnya musik latar game lagi jalan"
bool resumeMelodyAfter = false;  // dipakai internal, buat lanjutin musik latar
                                  // setelah efek pendek (mis. lompat) selesai

void playPattern(const int* pattern, int len, bool loop) {
  activePattern = pattern;
  patternLen = len;
  patternIndex = 0;
  patternStepStartMs = millis();
  patternRunning = true;
  patternLoop = loop;
  digitalWrite(BUZZER_PIN, HIGH); // step index 0 selalu ON
}

void stopPattern() {
  patternRunning = false;
  digitalWrite(BUZZER_PIN, LOW);
}

// panggil ini SETIAP loop() (bukan cuma pas di game) supaya semua efek
// suara -- termasuk yang dipicu di menu/kalkulator/mochi -- berhenti
// tepat waktu tanpa nge-block pembacaan tombol
void updateBuzzerPattern() {
  if (!patternRunning) return;
  if (millis() - patternStepStartMs >= (unsigned long)activePattern[patternIndex]) {
    patternIndex++;
    patternStepStartMs = millis();
    if (patternIndex >= patternLen) {
      if (patternLoop) {
        patternIndex = 0;
        digitalWrite(BUZZER_PIN, HIGH);
        return;
      }
      patternRunning = false;
      digitalWrite(BUZZER_PIN, LOW);
      if (resumeMelodyAfter && melodyPlaying) {
        resumeMelodyAfter = false;
        startBackgroundMelodyPattern();
      }
      return;
    }
    bool isOnStep = (patternIndex % 2 == 0);
    digitalWrite(BUZZER_PIN, isOnStep ? HIGH : LOW);
  }
}

// efek pendek sekali-jalan -- kalau musik latar game lagi jalan, dijeda
// dulu terus otomatis lanjut lagi setelah efeknya selesai
void playEffectPattern(const int* pattern, int len) {
  resumeMelodyAfter = melodyPlaying;
  playPattern(pattern, len, false);
}

const int patPet[]      = {70};
const int patSleep[]    = {90, 90, 90};
const int patWake[]     = {60, 70, 60};
const int patAngry[]    = {70, 60, 70, 60, 70, 60, 70};
const int patClick[]    = {25};
const int patJump[]     = {50};
const int patGameOver[] = {350};
const int patHeartbeat[] = {60, 350}; // musik latar game: detak ringan berulang

void playPetSound()      { playEffectPattern(patPet, 1); }
void playSleepSound()    { playEffectPattern(patSleep, 3); }
void playWakeSound()     { playEffectPattern(patWake, 3); }
void playAngrySound()    { playEffectPattern(patAngry, 7); }
void playClickSound()    { playEffectPattern(patClick, 1); }
void playJumpSound()     { playEffectPattern(patJump, 1); }
void playGameOverSound() { playEffectPattern(patGameOver, 1); }

void startBackgroundMelodyPattern() {
  playPattern(patHeartbeat, 2, true);
}

void startBackgroundMelody() {
  melodyPlaying = true;
  startBackgroundMelodyPattern();
}

void stopBackgroundMelody() {
  melodyPlaying = false;
  resumeMelodyAfter = false;
  stopPattern();
}

// ---------------------------- TOMBOL (DEBOUNCE + SHORT/LONG PRESS) ----------
struct Button {
  uint8_t pin;
  bool lastReading;
  bool stableState;
  unsigned long lastChangeMs;
  unsigned long pressStartMs;
  bool longFired;
};
Button btnMenu = {BTN_MENU, HIGH, HIGH, 0, 0, false};
Button btnOk   = {BTN_OK,   HIGH, HIGH, 0, 0, false};

const unsigned long DEBOUNCE_MS = 30;
const unsigned long LONG_PRESS_MS = 600;

void updateButton(Button &b, bool &pressDown, bool &shortRelease, bool &longPress) {
  pressDown = false; shortRelease = false; longPress = false;
  bool reading = digitalRead(b.pin);
  if (reading != b.lastReading) b.lastChangeMs = millis();
  if ((millis() - b.lastChangeMs) > DEBOUNCE_MS) {
    if (reading != b.stableState) {
      b.stableState = reading;
      if (b.stableState == LOW) {
        pressDown = true;
        b.pressStartMs = millis();
        b.longFired = false;
      } else {
        if (!b.longFired) shortRelease = true;
      }
    }
  }
  if (b.stableState == LOW && !b.longFired) {
    if (millis() - b.pressStartMs > LONG_PRESS_MS) {
      longPress = true;
      b.longFired = true;
    }
  }
  b.lastReading = reading;
}

// ---------------------------- DETEKSI SPAM KLIK -----------------------------
const uint8_t SPAM_WINDOW = 5;
const unsigned long SPAM_TIME_MS = 1300;
unsigned long clickTimestamps[SPAM_WINDOW] = {0};
uint8_t clickIndex = 0;

void registerClickForSpamCheck() {
  clickTimestamps[clickIndex] = millis();
  clickIndex = (clickIndex + 1) % SPAM_WINDOW;
  unsigned long oldest = clickTimestamps[clickIndex];
  if (oldest != 0 && (millis() - oldest) < SPAM_TIME_MS) {
    if (mood != MOOD_ANGRY) playAngrySound();
    mood = MOOD_ANGRY;
    moodUntilMs = millis() + ANGRY_DURATION;
  }
}

// ---------------------------- TOUCH (ELUS) ----------------------------------
bool lastTouchState = false;

void handleTouch() {
  bool raw = digitalRead(TOUCH_PIN) == HIGH;
  bool touched = TOUCH_ACTIVE_HIGH ? raw : !raw;

  if (isAsleep) { lastTouchState = touched; return; }

  if (touched) {
    lastInteractionMs = millis();
    if (mood != MOOD_ANGRY) {
      if (mood != MOOD_HAPPY) playPetSound(); // baru mulai dielus
      mood = MOOD_HAPPY;
      moodUntilMs = millis() + HAPPY_DURATION;
    }
  }
  lastTouchState = touched;
}

// ---------------------------- ANIMASI (LIRIK 4 ARAH, KEDIP, NAPAS) ----------
float eyeOffsetX = 0, eyeOffsetY = 0;
float eyeTargetX = 0, eyeTargetY = 0;
unsigned long nextLookChangeMs = 0;

bool isBlinking = false;
unsigned long blinkStartMs = 0;
unsigned long nextBlinkMs = 0;
const unsigned long BLINK_DURATION = 120;

float bobOffset = 0;

void updateLookAnimation() {
  if ((long)(millis() - nextLookChangeMs) > 0) {
    int r = random(0, 5);
    switch (r) {
      case 1: eyeTargetX = -8; eyeTargetY = 0;  break;
      case 2: eyeTargetX = 8;  eyeTargetY = 0;  break;
      case 3: eyeTargetX = 0;  eyeTargetY = -6; break;
      case 4: eyeTargetX = 0;  eyeTargetY = 6;  break;
      default: eyeTargetX = 0; eyeTargetY = 0;  break;
    }
    nextLookChangeMs = millis() + random(1200, 3500);
  }
  eyeOffsetX += (eyeTargetX - eyeOffsetX) * 0.08f;
  eyeOffsetY += (eyeTargetY - eyeOffsetY) * 0.08f;
}

void updateBlink() {
  if (!isBlinking && millis() > nextBlinkMs) {
    isBlinking = true;
    blinkStartMs = millis();
  }
  if (isBlinking && (millis() - blinkStartMs) > BLINK_DURATION) {
    isBlinking = false;
    nextBlinkMs = millis() + random(2500, 6000);
  }
}

// ---------------------------- GAMBAR WAJAH MOCHI ----------------------------
void drawEyePill(int x, int y, int w, int h, int color) {
  if (h < 2) h = 2;
  display.fillRoundRect(x - w / 2, y - h / 2, w, h, w / 2, color);
}

void drawSmile(int cx, int cy, int w, int color) {
  int half = w / 2;
  display.drawLine(cx - half, cy, cx - half + 4, cy + 4, color);
  display.drawLine(cx - half + 4, cy + 4, cx, cy + 6, color);
  display.drawLine(cx, cy + 6, cx + half - 4, cy + 4, color);
  display.drawLine(cx + half - 4, cy + 4, cx + half, cy, color);
}

void drawEyesNormal(int cx, int cy) {
  int ex = (int)eyeOffsetX;
  int ey = (int)eyeOffsetY + (int)bobOffset;
  int h = isBlinking ? 3 : 26;
  drawEyePill(cx - 20 + ex, cy + ey, 16, h, SSD1306_WHITE);
  drawEyePill(cx + 20 + ex, cy + ey, 16, h, SSD1306_WHITE);
  drawSmile(cx + ex, cy + 22 + ey, 20, SSD1306_WHITE);
}

void drawHeartEye(int cx, int cy, int size, int color) {
  int r = size / 2;
  display.fillCircle(cx - r / 2, cy - r / 2, r / 2, color);
  display.fillCircle(cx + r / 2, cy - r / 2, r / 2, color);
  display.fillTriangle(cx - r, cy - r / 4, cx + r, cy - r / 4, cx, cy + r, color);
  display.fillCircle(cx - r / 2 - 1, cy - r / 2 - 1, 1, SSD1306_BLACK);
}

void drawBlush(int cx, int cy) {
  display.fillRoundRect(cx - 6, cy - 3, 12, 6, 3, SSD1306_WHITE);
}

void drawCatMouth(int cx, int cy) {
  display.fillRoundRect(cx - 8, cy - 4, 16, 8, 3, SSD1306_WHITE);
  display.fillTriangle(cx - 3, cy - 1, cx + 3, cy - 1, cx, cy + 5, SSD1306_BLACK);
}

void drawHeartFace(int cx, int cy) {
  float pulse = sin(millis() / 200.0) * 1.5f;
  int size = 18 + (int)pulse;
  drawHeartEye(cx - 20, cy - 6, size, SSD1306_WHITE);
  drawHeartEye(cx + 20, cy - 6, size, SSD1306_WHITE);
  drawBlush(cx - 40, cy + 10);
  drawBlush(cx + 40, cy + 10);
  drawCatMouth(cx, cy + 18);
}

void drawEyesAngry(int cx, int cy) {
  display.fillTriangle(cx - 30, cy - 12, cx - 10, cy - 4, cx - 10, cy + 12, SSD1306_WHITE);
  display.fillTriangle(cx - 30, cy - 12, cx - 30, cy + 12, cx - 10, cy + 12, SSD1306_WHITE);
  display.fillTriangle(cx + 30, cy - 12, cx + 10, cy - 4, cx + 10, cy + 12, SSD1306_WHITE);
  display.fillTriangle(cx + 30, cy - 12, cx + 30, cy + 12, cx + 10, cy + 12, SSD1306_WHITE);
  display.drawLine(cx - 12, cy + 26, cx - 4, cy + 20, SSD1306_WHITE);
  display.drawLine(cx - 4,  cy + 20, cx + 4, cy + 26, SSD1306_WHITE);
  display.drawLine(cx + 4,  cy + 26, cx + 12, cy + 20, SSD1306_WHITE);
}

void drawSleepyFace(int cx, int cy) {
  int ey = cy - 2;
  display.drawLine(cx - 14, ey, cx - 26, ey - 8, SSD1306_WHITE);
  display.drawLine(cx - 14, ey, cx - 24, ey + 2, SSD1306_WHITE);
  display.drawLine(cx - 14, ey, cx - 22, ey + 10, SSD1306_WHITE);
  display.drawLine(cx + 14, ey, cx + 26, ey - 8, SSD1306_WHITE);
  display.drawLine(cx + 14, ey, cx + 24, ey + 2, SSD1306_WHITE);
  display.drawLine(cx + 14, ey, cx + 22, ey + 10, SSD1306_WHITE);
  display.fillRoundRect(cx - 6, ey - 2, 12, 10, 4, SSD1306_WHITE);
  display.fillTriangle(cx - 4, ey + 5, cx + 4, ey + 5, cx, ey + 11, SSD1306_BLACK);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 54);
  display.print("Tekan tombol utk bangunin");
}

void drawEyesWaking(int cx, int cy) {
  float progress = (float)(millis() - wakeStartMs) / (float)WAKE_ANIM_DURATION;
  if (progress > 1.0f) progress = 1.0f;
  int h = 4 + (int)(22 * progress);
  drawEyePill(cx - 20, cy, 16, h, SSD1306_WHITE);
  drawEyePill(cx + 20, cy, 16, h, SSD1306_WHITE);
  if (progress > 0.4f) drawSmile(cx, cy + 22, 20, SSD1306_WHITE);
}

void drawMochiFace() {
  int cx = SCREEN_W / 2;
  int cy = SCREEN_H / 2;

  if (isAsleep) {
    if (isWakingUp) {
      drawEyesWaking(cx, cy);
      if (millis() - wakeStartMs > WAKE_ANIM_DURATION) {
        isWakingUp = false; isAsleep = false; mood = MOOD_NORMAL;
        lastInteractionMs = millis();
      }
    } else {
      drawSleepyFace(cx, cy);
    }
    return;
  }

  if ((mood == MOOD_HAPPY || mood == MOOD_ANGRY) && millis() > moodUntilMs) {
    mood = MOOD_NORMAL;
    lastInteractionMs = millis();
  }

  if (mood == MOOD_NORMAL) {
    unsigned long idleMs = millis() - lastInteractionMs;
    if (idleMs > SLEEP_TIMEOUT) {
      isAsleep = true;
      mood = MOOD_SLEEPY;
      playSleepSound();
    }
  }

  updateBlink();
  bobOffset = sin(millis() / 600.0) * 1.5f;
  if (mood == MOOD_NORMAL) updateLookAnimation();

  switch (mood) {
    case MOOD_HAPPY: drawHeartFace(cx, cy); break;
    case MOOD_ANGRY: drawEyesAngry(cx, cy); break;
    default:         drawEyesNormal(cx, cy); break;
  }
}

// ---------------------------- JAM -------------------------------------------
int clockHour = 0, clockMinute = 0;
unsigned long clockBaseMillis = 0;
bool clockSetMode = false;
uint8_t clockSetField = 0;

void getCurrentClock(int &h, int &m) {
  unsigned long elapsedMin = (millis() - clockBaseMillis) / 60000UL;
  unsigned long totalMin = (unsigned long)clockHour * 60 + clockMinute + elapsedMin;
  totalMin %= (24UL * 60UL);
  h = totalMin / 60;
  m = totalMin % 60;
}

void drawClockScreen() {
  int h, m;
  getCurrentClock(h, m);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(clockSetMode ? "== ATUR JAM ==" : "== JAM ==");
  display.setTextSize(3);
  char buf[6];
  snprintf(buf, sizeof(buf), "%02d:%02d", h, m);
  display.setCursor(20, 24);
  if (clockSetMode) {
    bool showBlink = ((millis() / 300) % 2) == 0;
    if (clockSetField == 0 && !showBlink) snprintf(buf, sizeof(buf), "  :%02d", m);
    else if (clockSetField == 1 && !showBlink) snprintf(buf, sizeof(buf), "%02d:  ", h);
  }
  display.print(buf);
  display.setTextSize(1);
  display.setCursor(0, 54);
  if (clockSetMode) display.print(clockSetField == 0 ? "OK:+jam  MENU:pindah" : "OK:+menit  MENU:simpan");
  else display.print("MENU:atur  tahan:keluar");
}

void handleInputClock(bool menuShort, bool menuLong, bool okShort) {
  if (clockSetMode) {
    if (okShort) {
      playClickSound();
      if (clockSetField == 0) clockHour = (clockHour + 1) % 24;
      else clockMinute = (clockMinute + 1) % 60;
    }
    if (menuShort) {
      if (clockSetField == 0) clockSetField = 1;
      else { clockBaseMillis = millis(); clockSetMode = false; }
    }
    if (menuLong) { clockBaseMillis = millis(); clockSetMode = false; }
  } else {
    if (menuShort) {
      int h, m; getCurrentClock(h, m);
      clockHour = h; clockMinute = m;
      clockSetMode = true; clockSetField = 0;
    }
    if (menuLong) state = STATE_MENU;
  }
}

// ---------------------------- KALKULATOR -------------------------------------
enum CalcStage { CALC_NUM1, CALC_OP, CALC_NUM2, CALC_RESULT };
CalcStage calcStage = CALC_NUM1;
int calcNum1 = 0, calcNum2 = 0, calcOpIndex = 0;
const char calcOps[4] = {'+', '-', '*', '/'};
float calcResult = 0;

void resetCalc() {
  calcStage = CALC_NUM1; calcNum1 = 0; calcNum2 = 0; calcOpIndex = 0; calcResult = 0;
}

void handleInputCalc(bool menuShort, bool menuLong, bool okShort) {
  switch (calcStage) {
    case CALC_NUM1:
      if (okShort)   { playClickSound(); calcNum1 = (calcNum1 + 1) % 1000; }
      if (menuShort) calcStage = CALC_OP;
      if (menuLong)  { state = STATE_MENU; }
      break;
    case CALC_OP:
      if (okShort)   { playClickSound(); calcOpIndex = (calcOpIndex + 1) % 4; }
      if (menuShort) calcStage = CALC_NUM2;
      if (menuLong)  calcStage = CALC_NUM1;
      break;
    case CALC_NUM2:
      if (okShort) { playClickSound(); calcNum2 = (calcNum2 + 1) % 1000; }
      if (menuShort) {
        switch (calcOps[calcOpIndex]) {
          case '+': calcResult = (float)calcNum1 + calcNum2; break;
          case '-': calcResult = (float)calcNum1 - calcNum2; break;
          case '*': calcResult = (float)calcNum1 * calcNum2; break;
          case '/': calcResult = (calcNum2 != 0) ? (float)calcNum1 / calcNum2 : 0; break;
        }
        calcStage = CALC_RESULT;
      }
      if (menuLong) calcStage = CALC_OP;
      break;
    case CALC_RESULT:
      if (okShort)  resetCalc();
      if (menuLong) { resetCalc(); state = STATE_MENU; }
      break;
  }
}

void drawCalcScreen() {
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("== KALKULATOR ==");
  display.setTextSize(2);
  display.setCursor(4, 20);
  char line[24];
  switch (calcStage) {
    case CALC_NUM1: snprintf(line, sizeof(line), "%d", calcNum1); break;
    case CALC_OP:   snprintf(line, sizeof(line), "%d %c", calcNum1, calcOps[calcOpIndex]); break;
    case CALC_NUM2: snprintf(line, sizeof(line), "%d %c %d", calcNum1, calcOps[calcOpIndex], calcNum2); break;
    case CALC_RESULT:
      if (calcResult == (long)calcResult) snprintf(line, sizeof(line), "= %ld", (long)calcResult);
      else snprintf(line, sizeof(line), "= %.2f", calcResult);
      break;
  }
  display.print(line);
  display.setTextSize(1);
  display.setCursor(0, 54);
  switch (calcStage) {
    case CALC_NUM1:   display.print("OK:+1  MENU:lanjut"); break;
    case CALC_OP:     display.print("OK:operator  MENU:lanjut"); break;
    case CALC_NUM2:   display.print("OK:+1  MENU:hitung"); break;
    case CALC_RESULT: display.print("OK:baru  tahan MENU:keluar"); break;
  }
}

// ---------------------------- MENU UTAMA ------------------------------------
const char* mainMenuItems[] = {"Dino Run", "Jam", "Kalkulator", "Kembali ke Mochi"};
const uint8_t mainMenuCount = 4;
int mainMenuCursor = 0;

void drawMainMenu() {
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("== MENU ==");
  for (uint8_t i = 0; i < mainMenuCount; i++) {
    display.setCursor(10, 14 + i * 11);
    if (i == mainMenuCursor) display.print("> "); else display.print("  ");
    display.println(mainMenuItems[i]);
  }
}

void handleInputMainMenu(bool menuShort, bool menuLong, bool okShort) {
  if (okShort) { playClickSound(); mainMenuCursor = (mainMenuCursor + 1) % mainMenuCount; }
  if (menuShort) {
    if (mainMenuCursor == 0)      { state = STATE_GAME_DINO; resetDinoGame(); }
    else if (mainMenuCursor == 1) { state = STATE_CLOCK; clockSetMode = false; }
    else if (mainMenuCursor == 2) { state = STATE_CALC; resetCalc(); }
    else                           state = STATE_MOCHI;
  }
  if (menuLong) state = STATE_MOCHI;
}

// ---------------------------- GAME: DINO RUN --------------------------------
struct Obstacle { float x; bool active; };
const uint8_t DINO_OBSTACLE_COUNT = 2;
Obstacle dinoObstacles[DINO_OBSTACLE_COUNT];
float dinoY, dinoVelY;
bool dinoJumping = false;
const int DINO_GROUND_Y = 54;
const int DINO_X = 14;
int dinoScore = 0;
bool dinoGameOver = false;
unsigned long dinoLastMs = 0;

void resetDinoGame() {
  dinoY = DINO_GROUND_Y; dinoVelY = 0; dinoJumping = false; dinoScore = 0; dinoGameOver = false;
  for (uint8_t i = 0; i < DINO_OBSTACLE_COUNT; i++) { dinoObstacles[i].x = SCREEN_W + i * 70; dinoObstacles[i].active = true; }
  dinoLastMs = millis();
  startBackgroundMelody();
}

void updateDinoGame(bool jumpPress, bool exitLong) {
  unsigned long now = millis();
  float dt = (now - dinoLastMs) / 16.0f;
  dinoLastMs = now;
  if (dt <= 0) dt = 1;

  if (dinoGameOver) {
    if (jumpPress) resetDinoGame();
    if (exitLong) { stopBackgroundMelody(); state = STATE_MENU; }
    return;
  }
  if (exitLong) { stopBackgroundMelody(); state = STATE_MENU; return; }
  if (jumpPress && !dinoJumping) { dinoJumping = true; dinoVelY = -6.5; playJumpSound(); }
  if (dinoJumping) {
    dinoY += dinoVelY * dt;
    dinoVelY += 0.4f * dt;
    if (dinoY >= DINO_GROUND_Y) { dinoY = DINO_GROUND_Y; dinoJumping = false; dinoVelY = 0; }
  }
  for (uint8_t i = 0; i < DINO_OBSTACLE_COUNT; i++) {
    dinoObstacles[i].x -= 2.2f * dt;
    if (dinoObstacles[i].x < -10) { dinoObstacles[i].x = SCREEN_W + random(20, 60); dinoScore++; }
    if (dinoObstacles[i].x < DINO_X + 8 && dinoObstacles[i].x + 8 > DINO_X - 4) {
      if (dinoY > DINO_GROUND_Y - 10) {
        dinoGameOver = true;
        stopBackgroundMelody();
        playGameOverSound();
      }
    }
  }
}

void drawDinoGame() {
  display.drawLine(0, DINO_GROUND_Y + 8, SCREEN_W, DINO_GROUND_Y + 8, SSD1306_WHITE);
  display.fillRoundRect(DINO_X - 4, (int)dinoY - 8, 12, 16, 3, SSD1306_WHITE);
  for (uint8_t i = 0; i < DINO_OBSTACLE_COUNT; i++)
    display.fillRect((int)dinoObstacles[i].x, DINO_GROUND_Y - 4, 6, 12, SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print("Score: "); display.print(dinoScore);
  if (dinoGameOver) {
    display.setCursor(24, 28); display.print("GAME OVER");
    display.setCursor(0, 40); display.print("OK:ulang  tahan MENU:keluar");
  }
}

// ---------------------------- INPUT: LAYAR MOCHI -----------------------------
void handleInputMochi(bool menuDown, bool okDown, bool menuShort, bool okShort) {
  if (isAsleep) {
    if (menuDown || okDown) {
      if (!isWakingUp) { isWakingUp = true; wakeStartMs = millis(); playWakeSound(); }
    }
    return;
  }
  if (menuShort) { registerClickForSpamCheck(); state = STATE_MENU; mainMenuCursor = 0; }
  if (okShort)   { registerClickForSpamCheck(); }
}

// ---------------------------- SETUP & LOOP ----------------------------------
void setup() {
  Serial.begin(115200);
  pinMode(BTN_MENU, INPUT_PULLUP);
  pinMode(BTN_OK,   INPUT_PULLUP);
  pinMode(TOUCH_PIN, INPUT_PULLDOWN);
  pinMode(BUZZER_PIN, OUTPUT);

  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) Serial.println("OLED gagal init!");
  display.clearDisplay();
  display.display();

  randomSeed(analogRead(0));
  lastInteractionMs = millis();
  clockBaseMillis = millis();
  nextBlinkMs = millis() + random(2000, 4000);
  nextLookChangeMs = millis() + 1000;
}

void loop() {
  updateBuzzerPattern();

  bool menuDown, menuShort, menuLong;
  bool okDown, okShort, okLong;
  updateButton(btnMenu, menuDown, menuShort, menuLong);
  updateButton(btnOk,   okDown,   okShort,   okLong);

  if (menuDown || okDown) lastInteractionMs = millis();

  handleTouch();

  switch (state) {
    case STATE_MOCHI:     handleInputMochi(menuDown, okDown, menuShort, okShort); break;
    case STATE_MENU:      handleInputMainMenu(menuShort, menuLong, okShort);      break;
    case STATE_CLOCK:     handleInputClock(menuShort, menuLong, okShort);         break;
    case STATE_CALC:      handleInputCalc(menuShort, menuLong, okShort);          break;
    case STATE_GAME_DINO: updateDinoGame(okDown, menuLong);                        break;
  }

  display.clearDisplay();
  switch (state) {
    case STATE_MOCHI:     drawMochiFace();   break;
    case STATE_MENU:      drawMainMenu();    break;
    case STATE_CLOCK:     drawClockScreen(); break;
    case STATE_CALC:      drawCalcScreen();  break;
    case STATE_GAME_DINO: drawDinoGame();    break;
  }
  display.display();
  delay(10);
}
