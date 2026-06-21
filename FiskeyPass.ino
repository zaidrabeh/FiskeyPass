// =============================================================================
// FiskeyPass.ino — v4.0.1 Main Firmware
// =============================================================================
//
// ARCHITECTURE (state machine):
//   BOOT → SPLASH → PIN_ENTRY → MAIN_MENU
//                                  ├→ PASS_LIST → PASS_DETAIL
//                                  └→ SETTINGS  → sub-screens
//   Special: RETURN held at boot → WEB_PORTAL (AP mode, bypasses PIN)
//
// INCLUDE ORDER IS CRITICAL:
//   Project_Config.h MUST be line 1 — it defines USER_SETUP_LOADED before
//   TFT_eSPI.h, preventing the library from loading its own pin config.
// =============================================================================

#include "Project_Config.h"      // ← ALWAYS FIRST (defines USER_SETUP_LOADED)

#include <TFT_eSPI.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <esp_system.h>          // esp_random()
#include <mbedtls/base64.h>
#include <esp_wifi.h>

#include "SplashImage.h"
#include "Crypto.h"
#include "VaultManager.h"
#include "BleKeyboard.h"
#include "WebPortal.h"

// Vault unlock flag — set true ONLY after successful PIN verification via web UI
static bool vaultUnlocked = false;

// =============================================================================
// SECTION 1 — DISPLAY CONSTANTS (landscape 160×128)
// =============================================================================

// Colours (RGB565)
#define COL_BG       TFT_BLACK        // Background
#define COL_TITLE    0xFCC0           // Gold  — screen titles
#define COL_HILIGHT  0x2E8B           // Teal  — selected row highlight bar
#define COL_TEXT     TFT_WHITE        // Normal text
#define COL_DIM      0x7BEF           // Light-grey — dimmed / secondary text
#define COL_GREEN    TFT_GREEN        // OK / connected indicator
#define COL_RED      TFT_RED          // Error / wrong PIN
#define COL_DIVIDER  0x39E7           // Dark-grey — divider lines

// Status bar occupies top 12 px; usable area starts at y=13
#define STATUS_BAR_H  12
#define USABLE_Y      (STATUS_BAR_H + 2)

// Menu geometry
#define MENU_ITEM_H   16              // px per menu row
#define MENU_VISIBLE  6               // max rows visible at once
#define MENU_START_Y  (USABLE_Y + 4)

// =============================================================================
// SECTION 2 — STATE MACHINE
// =============================================================================

enum AppState {
  STATE_BOOT,
  STATE_SPLASH,
  STATE_PIN_ENTRY,        // Normal boot: user enters PIN
  STATE_PIN_CREATE,       // First boot: user sets a new PIN
  STATE_LOCKOUT,          // 5 wrong PINs → 60 s wait
  STATE_MAIN_MENU,
  STATE_PASS_LIST,
  STATE_PASS_DETAIL,
  STATE_SETTINGS,
  STATE_SETTINGS_INFO,
  STATE_SETTINGS_DISP,
  STATE_BLE_PAIR,         // Host requested passkey entry during BLE pairing
  STATE_WEB_PORTAL        // AP captive portal mode
};

AppState appState    = STATE_BOOT;
AppState prevState   = STATE_BOOT;   // used by RETURN button to go back

// =============================================================================
// SECTION 3 — GLOBAL OBJECTS
// =============================================================================

TFT_eSPI  tft;
DNSServer dnsServer;
AsyncWebServer webServer(80);

// =============================================================================
// SECTION 4 — RUNTIME STATE VARIABLES
// =============================================================================

// --- PIN entry ---
char     pinBuffer[PIN_LENGTH + 1] = {0}; // digits typed so far
uint8_t  pinDigitIndex = 0;               // which digit we are setting (0-3)
uint8_t  pinDigitValue[PIN_LENGTH] = {0}; // charset index for each box
int      pinAttempts   = 0;               // wrong-PIN counter
unsigned long lockoutStart = 0;           // millis() when lockout began

// PIN charset. The unlock/create PIN is alphanumeric. Trimmed to digits +
// lowercase (36 chars → 36^6 ≈ 2.2e9, still ~2000x the old 10^6 space) to keep
// button cycling fast; add A-Z for 62^6 if you prefer max entropy over speed.
// BLE pairing keeps a numeric-only charset (BLE passkeys are 6-digit numbers).
// pinDigitValue[] holds an index into activeCharset.
static const char  PIN_CHARSET[] = "0123456789abcdefghijklmnopqrstuvwxyz";
static const char  NUM_CHARSET[] = "0123456789";
const char* activeCharset    = PIN_CHARSET;
int         activeCharsetLen = sizeof(PIN_CHARSET) - 1;

// Web-portal PIN brute-force guard (mirrors the device-side lockout so the
// /api/vault-unlock route cannot be hammered through the 10^6 PIN space)
int      webPinAttempts  = 0;
unsigned long webLockoutStart = 0;

// Session PIN (kept in RAM to re-encrypt on vault save; cleared on reset/reboot)
char sessionPin[PIN_LENGTH + 1] = {0};

// RTC Bridge for Web Portal soft-reboot
RTC_DATA_ATTR char rtcSessionPin[PIN_LENGTH + 1] = {0};
RTC_DATA_ATTR bool rtcPinValid = false;

// --- Menu navigation ---
int selectedIndex    = 0;
int menuScrollOffset = 0;
int selectedPassIdx  = 0;   // which vault entry we drilled into
bool passRevealed    = false; // whether password is shown in detail view

// --- Display timeout ---
unsigned long lastActivity = 0;   // millis() of last button press
bool screenOn = true;

// --- Web portal ---
bool portalRunning = false;
bool securityLockout = false;

// =============================================================================
// SECTION 5 — BUTTON HELPERS
// =============================================================================

// Debounced button state — call in loop(), returns true on falling edge (press)
struct Button {
  const int8_t  pin;
  bool          lastState;
  unsigned long lastPress;
  unsigned long repeatAt;     // next auto-repeat fire time (0 = not repeating)

  bool pressed() {
    bool cur = (digitalRead(pin) == LOW);
    if (cur && !lastState && (millis() - lastPress > DEBOUNCE_MS)) {
      lastState  = true;
      lastPress  = millis();
      lastActivity = millis(); // reset display timeout on any press
      return true;
    }
    if (!cur) lastState = false;
    return false;
  }

  // Returns true while button is held (does NOT debounce — caller decides how long)
  bool held() {
    return (digitalRead(pin) == LOW);
  }

  // Like pressed(), but auto-repeats while held: fires once on press, then after
  // an initial delay repeats rapidly. Used for fast value cycling in PIN entry.
  bool repeat() {
    bool cur = (digitalRead(pin) == LOW);
    if (cur && !lastState && (millis() - lastPress > DEBOUNCE_MS)) {
      lastState = true;
      lastPress = millis();
      lastActivity = millis();
      repeatAt = millis() + 450;   // initial hold delay before auto-repeat
      return true;
    }
    if (cur && lastState && repeatAt && millis() >= repeatAt) {
      repeatAt = millis() + 90;    // auto-repeat interval
      lastActivity = millis();
      return true;
    }
    if (!cur) { lastState = false; repeatAt = 0; }
    return false;
  }
};

// Four button instances
Button btnUp     = {PIN_BTN_UP,     false, 0, 0};
Button btnDown   = {PIN_BTN_DOWN,   false, 0, 0};
Button btnSelect = {PIN_BTN_SELECT, false, 0, 0};
Button btnReturn = {PIN_BTN_RETURN, false, 0, 0};

// =============================================================================
// SECTION 6 — DISPLAY UTILITY FUNCTIONS
// =============================================================================

// ── Status bar ────────────────────────────────────────────────────────────────
// Top 12 px: BLE icon (left), "FiskeyPass" text (right), divider line below.
void drawStatusBar() {
  // Background
  tft.fillRect(0, 0, tft.width(), STATUS_BAR_H, COL_BG);

  // BLE icon — a simple "B" glyph; replace with bitmap if desired
  tft.setTextSize(1);
  if (bleKeyboard.bleIsConnected()) {
    tft.setTextColor(COL_GREEN, COL_BG);
    tft.setCursor(2, 2);
    tft.print("\xA2"); // Bluetooth-like symbol via GLCD
    tft.setCursor(12, 2);
    tft.setTextColor(COL_DIM, COL_BG);
    tft.print(connectedBleMac);
  } else if (bleKeyboard.bleIsInitialized()) {
    tft.setTextColor(COL_DIM, COL_BG);
    tft.setCursor(2, 2);
    tft.print("B");
  }
  // In portal mode show WiFi indicator
  if (portalRunning) {
    tft.setTextColor(0x07FF, COL_BG); // cyan
    tft.setCursor(2, 2);
    tft.print("W");
  }

  // "FiskeyPass" label — right-aligned
  tft.setTextColor(COL_TITLE, COL_BG);
  const char* label = "FiskeyPass";
  int lw = strlen(label) * 6; // 6 px per char at size 1
  tft.setCursor(tft.width() - lw - 2, 2);
  tft.print(label);

  // Divider line
  tft.drawFastHLine(0, STATUS_BAR_H, tft.width(), COL_DIVIDER);
}

// ── Screen title below status bar ─────────────────────────────────────────────
void drawTitle(const char* title) {
  tft.setTextColor(COL_TITLE, COL_BG);
  tft.setTextSize(1);
  tft.setCursor(4, USABLE_Y + 2);
  tft.print(title);
  // Thin underline beneath title
  tft.drawFastHLine(0, USABLE_Y + 12, tft.width(), COL_DIVIDER);
}

// ── Generic scrollable menu ───────────────────────────────────────────────────
// items[]  : array of C-strings
// count    : total items
// selected : currently highlighted index
// scrollOff: first visible index
void drawMenu(const char* const* items, int count,
              int selected, int& scrollOff,
              const char* title = nullptr) {

  tft.fillScreen(COL_BG);
  drawStatusBar();

  if (title) drawTitle(title);

  int itemStartY = title ? (USABLE_Y + 16) : MENU_START_Y;

  // Clamp scroll offset so selected is always visible
  if (selected < scrollOff) scrollOff = selected;
  if (selected >= scrollOff + MENU_VISIBLE) scrollOff = selected - MENU_VISIBLE + 1;
  if (scrollOff < 0) scrollOff = 0;
  if (count <= MENU_VISIBLE) scrollOff = 0;

  int endIdx = scrollOff + MENU_VISIBLE;
  if (endIdx > count) endIdx = count;

  for (int i = scrollOff; i < endIdx; i++) {
    int y = itemStartY + (i - scrollOff) * MENU_ITEM_H;
    bool isSelected = (i == selected);

    if (isSelected) {
      tft.fillRect(0, y - 1, tft.width(), MENU_ITEM_H, COL_HILIGHT);
      tft.setTextColor(TFT_WHITE, COL_HILIGHT);
    } else {
      tft.setTextColor(COL_TEXT, COL_BG);
    }

    tft.setTextSize(1);
    tft.setCursor(4, y + 2);

    // Prefix selected row with ">" indicator
    if (isSelected) tft.print("> ");
    else            tft.print("  ");

    // Truncate long names to fit screen width (max ~24 chars at size 1)
    char buf[28];
    strlcpy(buf, items[i], sizeof(buf));
    tft.print(buf);
  }

  // Scroll indicators
  if (scrollOff > 0) {
    tft.setTextColor(COL_DIM, COL_BG);
    tft.setCursor(tft.width() - 8, itemStartY);
    tft.print("^");
  }
  if (endIdx < count) {
    tft.setTextColor(COL_DIM, COL_BG);
    tft.setCursor(tft.width() - 8,
                  itemStartY + (MENU_VISIBLE - 1) * MENU_ITEM_H);
    tft.print("v");
  }
}

// ── Centred message ───────────────────────────────────────────────────────────
void drawCentredMsg(const char* line1, const char* line2 = nullptr,
                    uint16_t col1 = COL_TEXT, uint16_t col2 = COL_DIM) {
  tft.fillScreen(COL_BG);
  drawStatusBar();

  int y = tft.height() / 2 - (line2 ? 12 : 6);

  tft.setTextSize(1);
  tft.setTextColor(col1, COL_BG);
  int x = (tft.width() - (int)strlen(line1) * 6) / 2;
  tft.setCursor(max(x, 2), y);
  tft.print(line1);

  if (line2) {
    tft.setTextColor(col2, COL_BG);
    x = (tft.width() - (int)strlen(line2) * 6) / 2;
    tft.setCursor(max(x, 2), y + 14);
    tft.print(line2);
  }
}

// ── Display timeout management ────────────────────────────────────────────────
void checkDisplayTimeout() {
  if (!deviceConfig.displayTimeoutEnabled) return;
  if (screenOn && (millis() - lastActivity > DISPLAY_TIMEOUT_MS)) {
    // Blank screen by filling black (backlight always-on per hardware)
    tft.fillScreen(COL_BG);
    screenOn = false;
  }
}

void wakeScreen() {
  if (!screenOn) {
    screenOn = true;
    lastActivity = millis();
    // Caller is responsible for redrawing the current screen
  }
}

// =============================================================================
// SECTION 7 — FORWARD DECLARATIONS
// (Implementations are in Parts 2, 3, and 4)
// =============================================================================

void runBootSequence();
void drawSplash();
void enterPinEntry(bool isFirstBoot);
void loopPinEntry(bool isFirstBoot);
void enterWebPortalMode();
void drawMainMenu();
void loopMainMenu();
void drawPassList();
void loopPassList();
void drawPassDetail(int idx);
void loopPassDetail(int idx);
void drawSettings();
void loopSettings();
void drawSettingsInfo();
void loopSettingsInfo();
void drawSettingsDisp();
void loopSettingsDisp();
void drawLockout();
void loopLockout();
void setupWebPortalRoutes();
void loopWebPortal();
void drawBlePair();
void loopBlePair();
void drawPinScreen(bool isCreating, uint8_t activeDigit, const char* title,
                   const char* statusMsg = nullptr, uint16_t statusCol = 0x7BEF);

// =============================================================================
// SECTION 8 — setup()
// =============================================================================

void setup() {
  Serial.begin(115200);
  Serial.println(F("\n[BOOT] FiskeyPass v4.0.1 starting"));

  // ── GPIO init ──────────────────────────────────────────────────────────────
  pinMode(PIN_BTN_UP,     INPUT_PULLUP);
  pinMode(PIN_BTN_DOWN,   INPUT_PULLUP);
  pinMode(PIN_BTN_SELECT, INPUT_PULLUP);
  pinMode(PIN_BTN_RETURN, INPUT_PULLUP);

  // ── TFT init ───────────────────────────────────────────────────────────────
  tft.init();
  tft.setRotation(1);              // Landscape: 160 wide × 128 tall
  tft.fillScreen(COL_BG);

  // ── LittleFS init (robust: format raw partition if needed) ─────────────────
  if (!initLittleFS()) {
    drawCentredMsg("LittleFS FAILED", "Erase flash & retry", COL_RED, COL_DIM);
    while (true) delay(1000);
  }

  // ── Load config (PIN hash, timeout preference) ────────────────────────────
  bool configExists = loadConfig();   // sets deviceConfig.firstBoot if missing

  // ── Check for Web Portal Flag ───────────────────────────────────────────
  if (LittleFS.exists("/portal.flag")) {
    LittleFS.remove("/portal.flag");
    memset(rtcSessionPin, 0, sizeof(rtcSessionPin));
    memset(sessionPin, 0, sizeof(sessionPin));

    Serial.println(F("[BOOT] Portal flag found — Entering Web Portal Mode (v4.0.1)"));
    appState = STATE_WEB_PORTAL;
    enterWebPortalMode();
    return;
  }

  // ── Normal boot path ───────────────────────────────────────────────────────
  appState = STATE_SPLASH;
  runBootSequence();
}

// =============================================================================
// SECTION 9 — loop()
// =============================================================================

void loop() {
  // Wake screen on any button activity
  bool anyBtn = (digitalRead(PIN_BTN_UP)     == LOW ||
                 digitalRead(PIN_BTN_DOWN)   == LOW ||
                 digitalRead(PIN_BTN_SELECT) == LOW ||
                 digitalRead(PIN_BTN_RETURN) == LOW);
  if (anyBtn && !screenOn) {
    wakeScreen();
    delay(DEBOUNCE_MS);   // consume the waking press
    return;
  }

  // BLE host requested passkey entry — interrupt the UI to collect the code
  if (bleKeyboard.pairPinRequested && appState != STATE_BLE_PAIR &&
      bleKeyboard.bleIsInitialized()) {
    appState = STATE_BLE_PAIR;
    pinDigitIndex = 0;
    memset(pinDigitValue, 0, sizeof(pinDigitValue));
    wakeScreen();
    drawBlePair();
  }

  // Route to the active state handler
  switch (appState) {
    case STATE_PIN_CREATE: loopPinEntry(true);   break;
    case STATE_PIN_ENTRY:  loopPinEntry(false);  break;
    case STATE_LOCKOUT:    loopLockout();         break;
    case STATE_MAIN_MENU:  loopMainMenu();        break;
    case STATE_PASS_LIST:  loopPassList();        break;
    case STATE_PASS_DETAIL:loopPassDetail(selectedPassIdx); break;
    case STATE_SETTINGS:   loopSettings();        break;
    case STATE_SETTINGS_INFO: loopSettingsInfo(); break;
    case STATE_SETTINGS_DISP: loopSettingsDisp(); break;
    case STATE_BLE_PAIR:   loopBlePair();         break;
    case STATE_WEB_PORTAL: loopWebPortal();       break;
    default: break;
  }

  checkDisplayTimeout();
}
// =============================================================================
// PART 2 — BOOT SEQUENCE · SPLASH · PIN ENTRY · LOCKOUT
// =============================================================================

// =============================================================================
// 2.1  Boot sequence (called once from setup after LittleFS is ready)
// =============================================================================

void runBootSequence() {
  // Show splash then transition to PIN entry (or PIN creation on first boot)
  drawSplash();
  unsigned long splashStart = millis();
  while (millis() - splashStart < SPLASH_DISPLAY_MS) {
    delay(10);
  }

  if (deviceConfig.firstBoot) {
    // No config.json found — user must set a new PIN before anything else
    appState = STATE_PIN_CREATE;
    enterPinEntry(true);
  } else {
    appState = STATE_PIN_ENTRY;
    enterPinEntry(false);
  }
}

// =============================================================================
// 2.2  Splash screen
// =============================================================================

void drawSplash() {
  tft.fillScreen(COL_BG);

#ifdef FISKEYPASS_HAS_SPLASH
  // Push the pre-converted 160×128 RGB565 bitmap from SplashImage.h
  // (user must run image2cpp on whoami.jpg resized to 160×128)
  tft.pushImage(0, 0, WHOAMI_WIDTH, WHOAMI_HEIGHT, whoami);
#else
  // ── Text-based splash (default until splash image is converted) ────────────

  // Decorative top/bottom gold bars
  tft.fillRect(0,  0, tft.width(), 4, COL_TITLE);
  tft.fillRect(0, tft.height() - 4, tft.width(), 4, COL_TITLE);

  // Big centred title — use font 2 (16 px) for "FISKEYPASS"
  tft.setTextSize(2);
  tft.setTextColor(COL_TITLE, COL_BG);
  const char* brand = "FISKEYPASS";
  int bw = strlen(brand) * 12;   // 12 px per char at size 2
  tft.setCursor((tft.width() - bw) / 2, 28);
  tft.print(brand);

  // Subtitle
  tft.setTextSize(1);
  tft.setTextColor(COL_DIM, COL_BG);
  const char* sub = "Encrypted Password Vault";
  int sw = strlen(sub) * 6;
  tft.setCursor((tft.width() - sw) / 2, 54);
  tft.print(sub);

  // Version
  tft.setTextColor(COL_HILIGHT, COL_BG);
  const char* ver = FISKEYPASS_VERSION_STR;
  int vw = strlen(ver) * 6;
  tft.setCursor((tft.width() - vw) / 2, 70);
  tft.print(ver);

  // Loading bar animation — fills across 2 seconds
  int barW = 100;
  int barX = (tft.width() - barW) / 2;
  int barY = 92;
  tft.drawRect(barX, barY, barW, 6, COL_DIVIDER);
  for (int i = 0; i <= barW - 2; i++) {
    tft.fillRect(barX + 1, barY + 1, i, 4, COL_HILIGHT);
    delay(SPLASH_DISPLAY_MS / (barW - 1));
  }
  return;   // delay already consumed inside loop above
#endif

  // For bitmap splash the outer loop in runBootSequence() handles the delay
}

// =============================================================================
// 2.3  PIN entry — shared for both "create new PIN" and "verify existing PIN"
// =============================================================================

// Reset the digit array and index to start fresh
void enterPinEntry(bool isFirstBoot) {
  activeCharset = PIN_CHARSET;   // alphanumeric for unlock/create
  activeCharsetLen = sizeof(PIN_CHARSET) - 1;
  pinDigitIndex = 0;
  memset(pinDigitValue, 0, sizeof(pinDigitValue));
  memset(pinBuffer,     0, sizeof(pinBuffer));

  if (isFirstBoot) {
    drawPinScreen(true, 0, "Set New PIN");
  } else {
    drawPinScreen(false, 0, "Enter PIN");
  }
}

// ── Draw the 6-digit PIN UI ──────────────────────────────────────────────────
//   isCreating : true = "Set PIN" UI, false = "Enter PIN" UI
//   activeDigit: which box is being edited (0-3)
//   title      : screen header string
void drawPinScreen(bool isCreating, uint8_t activeDigit, const char* title,
                   const char* statusMsg, uint16_t statusCol) {
  tft.fillScreen(COL_BG);
  drawStatusBar();
  drawTitle(title);

  // Instruction text
  tft.setTextSize(1);
  tft.setTextColor(COL_DIM, COL_BG);
  const char* instr = isCreating
      ? "UP/DOWN cycle, SELECT confirm"
      : "UP/DOWN cycle, SELECT confirm";
  int ix = (tft.width() - (int)strlen(instr) * 6) / 2;
  tft.setCursor(max(ix, 2), 32);
  tft.print(instr);

  // ── 6 digit boxes ─────────────────────────────────────────────────────────
  // Each box: 18 px wide, 24 px tall, spaced 6 px apart
  // Centre the group horizontally
  const int boxW = 18, boxH = 24, gap = 6;
  int totalW = PIN_LENGTH * boxW + (PIN_LENGTH - 1) * gap;
  int startX = (tft.width() - totalW) / 2;
  int boxY   = 48;

  for (int i = 0; i < PIN_LENGTH; i++) {
    int bx = startX + i * (boxW + gap);
    bool isActive = (i == activeDigit);

    // Box border — teal if active, grey otherwise
    uint16_t borderCol = isActive ? COL_HILIGHT : COL_DIVIDER;
    tft.drawRect(bx, boxY, boxW, boxH, borderCol);
    tft.fillRect(bx + 1, boxY + 1, boxW - 2, boxH - 2, COL_BG);

    // Digit content
    tft.setTextSize(2);
    if (i < pinDigitIndex) {
      // Already confirmed — show masked '*'
      tft.setTextColor(COL_TEXT, COL_BG);
      tft.setCursor(bx + 5, boxY + 5);
      tft.print("*");
    } else if (i == activeDigit) {
      // Current digit being set — show live value
      tft.setTextColor(COL_TITLE, COL_BG);
      tft.setCursor(bx + 5, boxY + 5);
      tft.print(activeCharset[pinDigitValue[i]]);
    }
    // Future digits remain blank
  }

  // Position indicator: which character of PIN_LENGTH is being entered
  {
    char pos[20];
    snprintf(pos, sizeof(pos), "Char %d of %d", activeDigit + 1, PIN_LENGTH);
    tft.setTextSize(1);
    tft.setTextColor(COL_DIM, COL_BG);
    int px = (tft.width() - (int)strlen(pos) * 6) / 2;
    tft.setCursor(max(px, 2), 76);
    tft.print(pos);
  }

  // Status / error message area at bottom
  if (statusMsg) {
    tft.setTextSize(1);
    tft.setTextColor(statusCol, COL_BG);
    int sx = (tft.width() - (int)strlen(statusMsg) * 6) / 2;
    tft.setCursor(max(sx, 2), 90);
    tft.print(statusMsg);
  }

  // Attempt counter (only relevant in verify mode)
  if (!isCreating && pinAttempts > 0) {
    char buf[24];
    snprintf(buf, sizeof(buf), "Attempts: %d/%d", pinAttempts, MAX_PIN_ATTEMPTS);
    tft.setTextSize(1);
    tft.setTextColor(COL_RED, COL_BG);
    int ax = (tft.width() - (int)strlen(buf) * 6) / 2;
    tft.setCursor(max(ax, 2), 105);
    tft.print(buf);
  }
}

// ── Second PIN buffer for "confirm new PIN" step ──────────────────────────────
static uint8_t pinConfirmValue[PIN_LENGTH] = {0};
static bool    awaitingConfirm = false;   // true once first pass of PIN is done

// ── loopPinEntry — called every loop() tick while in PIN_ENTRY / PIN_CREATE ──
void loopPinEntry(bool isFirstBoot) {
  activeCharset = PIN_CHARSET;   // alphanumeric unlock/create
  activeCharsetLen = sizeof(PIN_CHARSET) - 1;
  bool up  = btnUp.repeat();     // hold to fast-cycle characters
  bool dn  = btnDown.repeat();
  bool sel = btnSelect.pressed();
  bool ret = btnReturn.pressed();

  if (!up && !dn && !sel && !ret) return;   // nothing to do

  // ── First-boot: two-pass new PIN creation ────────────────────────────────
  if (isFirstBoot) {
    if (up) {
      pinDigitValue[pinDigitIndex] = (pinDigitValue[pinDigitIndex] + 1) % activeCharsetLen;
      drawPinScreen(true, pinDigitIndex, awaitingConfirm ? "Confirm PIN" : "Set New PIN");
    }
    if (dn) {
      pinDigitValue[pinDigitIndex] = (pinDigitValue[pinDigitIndex] + activeCharsetLen - 1) % activeCharsetLen;
      drawPinScreen(true, pinDigitIndex, awaitingConfirm ? "Confirm PIN" : "Set New PIN");
    }
    if (sel) {
      // Confirm this digit and advance
      pinDigitIndex++;
      if (pinDigitIndex < PIN_LENGTH) {
        drawPinScreen(true, pinDigitIndex, awaitingConfirm ? "Confirm PIN" : "Set New PIN");
      } else {
        // All characters entered
        char entered[PIN_LENGTH + 1];
        for (int i = 0; i < PIN_LENGTH; i++) entered[i] = activeCharset[pinDigitValue[i]];
        entered[PIN_LENGTH] = '\0';

        if (!awaitingConfirm) {
          // First pass — store and ask to confirm
          memcpy(pinConfirmValue, pinDigitValue, sizeof(pinDigitValue));
          awaitingConfirm = true;
          pinDigitIndex = 0;
          memset(pinDigitValue, 0, sizeof(pinDigitValue));
          drawPinScreen(true, 0, "Confirm PIN", "Re-enter PIN to confirm", COL_DIM);
        } else {
          // Second pass — compare
          bool match = true;
          for (int i = 0; i < PIN_LENGTH; i++) {
            if (pinDigitValue[i] != pinConfirmValue[i]) { match = false; break; }
          }
          if (match) {
            // Save PIN hash to config
            if (setNewPin(entered)) {
              strlcpy(sessionPin, entered, sizeof(sessionPin));
              drawCentredMsg("PIN Set!", "Loading vault...", COL_GREEN, COL_DIM);
              delay(800);
              // Vault starts empty on first boot — no load needed
              vaultCount = 0;

              // Init BLE now that we are past portal mode
              bleKeyboard.bleInit();
              appState = STATE_MAIN_MENU;
              drawMainMenu();
            } else {
              drawPinScreen(true, 0, "Set New PIN", "Save failed! Try again", COL_RED);
              awaitingConfirm = false;
              pinDigitIndex   = 0;
              memset(pinDigitValue, 0, sizeof(pinDigitValue));
            }
          } else {
            // Mismatch
            awaitingConfirm = false;
            pinDigitIndex   = 0;
            memset(pinDigitValue, 0, sizeof(pinDigitValue));
            drawPinScreen(true, 0, "Set New PIN", "PINs didn't match!", COL_RED);
          }
        }
      }
    }
    // RETURN on create screen resets current digit to 0
    if (ret && pinDigitIndex > 0) {
      pinDigitIndex--;
      pinDigitValue[pinDigitIndex] = 0;
      drawPinScreen(true, pinDigitIndex, awaitingConfirm ? "Confirm PIN" : "Set New PIN");
    }
    return;
  }

  // ── Normal PIN verify ────────────────────────────────────────────────────
  if (up) {
    pinDigitValue[pinDigitIndex] = (pinDigitValue[pinDigitIndex] + 1) % activeCharsetLen;
    drawPinScreen(false, pinDigitIndex, "Enter PIN");
  }
  if (dn) {
    pinDigitValue[pinDigitIndex] = (pinDigitValue[pinDigitIndex] + activeCharsetLen - 1) % activeCharsetLen;
    drawPinScreen(false, pinDigitIndex, "Enter PIN");
  }
  if (sel) {
    pinDigitIndex++;
    if (pinDigitIndex < PIN_LENGTH) {
      drawPinScreen(false, pinDigitIndex, "Enter PIN");
    } else {
      // All characters entered — verify
      char entered[PIN_LENGTH + 1];
      for (int i = 0; i < PIN_LENGTH; i++) entered[i] = activeCharset[pinDigitValue[i]];
      entered[PIN_LENGTH] = '\0';

      if (verifyPin(entered)) {
        // ── Correct PIN ──────────────────────────────────────────────────
        pinAttempts = 0;
        strlcpy(sessionPin, entered, sizeof(sessionPin));

        drawCentredMsg("PIN OK", "Decrypting vault...", COL_GREEN, COL_DIM);
        delay(400);

        bool vaultOk = loadVault(sessionPin);
        if (!vaultOk && LittleFS.exists(VAULT_FILE_PATH)) {
          // File exists but decryption failed — should not happen if PIN is correct
          drawCentredMsg("Vault Error", "Data may be corrupt", COL_RED, COL_DIM);
          delay(1500);
        }

        bleKeyboard.bleInit();
        appState = STATE_MAIN_MENU;
        drawMainMenu();

      } else {
        // ── Wrong PIN ───────────────────────────────────────────────────
        pinAttempts++;
        pinDigitIndex = 0;
        memset(pinDigitValue, 0, sizeof(pinDigitValue));

        if (pinAttempts >= MAX_PIN_ATTEMPTS) {
          // Lockout
          lockoutStart = millis();
          appState = STATE_LOCKOUT;
          drawLockout();
        } else {
          char msg[24];
          snprintf(msg, sizeof(msg), "Wrong PIN (%d left)", MAX_PIN_ATTEMPTS - pinAttempts);
          drawPinScreen(false, 0, "Enter PIN", msg, COL_RED);
        }
      }
    }
  }
  // RETURN backs up one digit
  if (ret && pinDigitIndex > 0) {
    pinDigitIndex--;
    pinDigitValue[pinDigitIndex] = 0;
    drawPinScreen(false, pinDigitIndex, "Enter PIN");
  }
}

// =============================================================================
// 2.3b  BLE pairing passkey entry (KeyboardOnly IO cap)
//   The host shows a random 6-digit code; the user types it here to authorise
//   the bond. Reuses the PIN digit buffers (idle while the vault is unlocked).
// =============================================================================

void drawBlePair() {
  activeCharset = NUM_CHARSET;   // BLE passkeys are numeric
  activeCharsetLen = sizeof(NUM_CHARSET) - 1;
  drawPinScreen(false, pinDigitIndex, "BLE Pairing", "Type code shown on host", COL_DIM);
}

void loopBlePair() {
  activeCharset = NUM_CHARSET;
  activeCharsetLen = sizeof(NUM_CHARSET) - 1;
  // Host aborted/disconnected — bail back to the menu
  if (!bleKeyboard.pairPinRequested) {
    pinDigitIndex = 0;
    memset(pinDigitValue, 0, sizeof(pinDigitValue));
    appState = STATE_MAIN_MENU;
    drawMainMenu();
    return;
  }

  bool up  = btnUp.repeat();     // hold to fast-cycle digits
  bool dn  = btnDown.repeat();
  bool sel = btnSelect.pressed();
  bool ret = btnReturn.pressed();
  if (!up && !dn && !sel && !ret) return;

  if (up) { pinDigitValue[pinDigitIndex] = (pinDigitValue[pinDigitIndex] + 1) % activeCharsetLen; drawBlePair(); }
  if (dn) { pinDigitValue[pinDigitIndex] = (pinDigitValue[pinDigitIndex] + activeCharsetLen - 1) % activeCharsetLen; drawBlePair(); }

  if (sel) {
    pinDigitIndex++;
    if (pinDigitIndex < PIN_LENGTH) {
      drawBlePair();
    } else {
      uint32_t code = 0;
      for (int i = 0; i < PIN_LENGTH; i++) code = code * 10 + pinDigitValue[i];
      bleKeyboard.blePairSubmit(code);
      drawCentredMsg("Pairing...", "Check host device", COL_GREEN, COL_DIM);
      delay(800);
      pinDigitIndex = 0;
      memset(pinDigitValue, 0, sizeof(pinDigitValue));
      appState = STATE_MAIN_MENU;
      drawMainMenu();
    }
  }

  if (ret) {
    if (pinDigitIndex > 0) {
      pinDigitIndex--;
      pinDigitValue[pinDigitIndex] = 0;
      drawBlePair();
    } else {
      // Cancel pairing
      bleKeyboard.blePairCancel();
      pinDigitIndex = 0;
      memset(pinDigitValue, 0, sizeof(pinDigitValue));
      appState = STATE_MAIN_MENU;
      drawMainMenu();
    }
  }
}

// =============================================================================
// 2.4  Lockout screen (60 second countdown)
// =============================================================================

void drawLockout() {
  tft.fillScreen(COL_BG);
  drawStatusBar();
  drawTitle("Locked");

  tft.setTextSize(1);
  tft.setTextColor(COL_RED, COL_BG);
  const char* msg = "Too many wrong PINs";
  int mx = (tft.width() - (int)strlen(msg) * 6) / 2;
  tft.setCursor(max(mx, 2), 38);
  tft.print(msg);
}

void loopLockout() {
  unsigned long elapsed = millis() - lockoutStart;
  unsigned long remaining = (LOCKOUT_DURATION_MS > elapsed)
                            ? (LOCKOUT_DURATION_MS - elapsed) / 1000
                            : 0;

  // Update countdown every second
  static unsigned long lastDraw = 0;
  if (millis() - lastDraw > 900) {
    lastDraw = millis();

    // Clear countdown area and redraw
    tft.fillRect(0, 55, tft.width(), 30, COL_BG);
    tft.setTextSize(2);
    tft.setTextColor(COL_TITLE, COL_BG);
    char buf[12];
    snprintf(buf, sizeof(buf), "%lu s", remaining + 1);
    int bx = (tft.width() - (int)strlen(buf) * 12) / 2;
    tft.setCursor(max(bx, 2), 60);
    tft.print(buf);

    tft.setTextSize(1);
    tft.setTextColor(COL_DIM, COL_BG);
    const char* sub = "Wait before retrying";
    int sx = (tft.width() - (int)strlen(sub) * 6) / 2;
    tft.setCursor(max(sx, 2), 85);
    tft.print(sub);
  }

  // Lockout expired → return to PIN entry
  if (elapsed >= LOCKOUT_DURATION_MS) {
    pinAttempts   = 0;
    pinDigitIndex = 0;
    memset(pinDigitValue, 0, sizeof(pinDigitValue));
    appState = STATE_PIN_ENTRY;
    enterPinEntry(false);
  }
}

// =============================================================================
// 2.5  Web-portal entry (AP setup — BLE must be off)
// =============================================================================

void enterWebPortalMode() {

  portalRunning = false;   // will be set true below
  vaultUnlocked = false;   // vault stays locked until PIN entered via web UI

  // Show "Starting AP..." screen
  tft.fillScreen(COL_BG);
  drawStatusBar();
  drawTitle("Web Portal");

  tft.setTextSize(1);
  tft.setTextColor(COL_DIM, COL_BG);
  tft.setCursor(4, 32); tft.print("Starting AP...");

  // Per-device WPA2 password derived from the chip MAC — unique per unit and
  // shown only on the physical TFT below, replacing the shared published default.
  char apPass[16];
  uint64_t mac = ESP.getEfuseMac();
  snprintf(apPass, sizeof(apPass), "FP%08X", (uint32_t)(mac & 0xFFFFFFFFu));

  // Start WiFi access point (secured with WPA2-PSK)
  WiFi.mode(WIFI_AP);
  delay(100);
  bool ok = WiFi.softAP(AP_SSID, apPass);
  delay(300);

  if (!ok) {
    drawCentredMsg("AP Failed!", "Reboot and try again", COL_RED, COL_DIM);
    while (true) delay(1000);  // halt
  }

  IPAddress apIP = WiFi.softAPIP();

  // DNS server — redirects all DNS queries to our IP (captive portal behaviour)
  dnsServer.start(AP_DNS_PORT, "*", apIP);

  // Mount LittleFS so API handlers can access vault
  // (already mounted in setup() — just verify)
  if (!LittleFS.begin()) {
    Serial.println(F("[WP]   LittleFS re-mount failed"));
  }

  // Register API routes and start server
  setupWebPortalRoutes();

  webServer.begin();
  portalRunning = true;
  Serial.println(F("[WP]   Portal running — vault locked until PIN entry"));

  // Show connection info on TFT
  tft.fillScreen(COL_BG);
  drawStatusBar();
  drawTitle("Web Portal");

  tft.setTextSize(1);
  tft.setTextColor(COL_TEXT, COL_BG);

  tft.setCursor(4, 30); tft.print("SSID:");
  tft.setTextColor(COL_TITLE, COL_BG);
  tft.setCursor(40, 30); tft.print(AP_SSID);

  tft.setTextColor(COL_TEXT, COL_BG);
  tft.setCursor(4, 44); tft.print("PASS:");
  tft.setTextColor(COL_TITLE, COL_BG);
  tft.setCursor(40, 44); tft.print(apPass);

  tft.setTextColor(COL_TEXT, COL_BG);
  tft.setCursor(4, 58); tft.print("IP:");
  tft.setTextColor(COL_HILIGHT, COL_BG);
  tft.setCursor(40, 58); tft.print(apIP);

  tft.setTextColor(COL_DIM, COL_BG);
  tft.setCursor(4, 80);  tft.print("Join this Wi-Fi, then");
  tft.setCursor(4, 92);  tft.print("open any web page and");
  tft.setCursor(4, 104); tft.print("unlock with your PIN.");
}

void drawWebPortalInfo() {
  tft.fillScreen(COL_BG);
  drawStatusBar();
  drawTitle("Web Portal");

  tft.setTextSize(1);
  tft.setTextColor(COL_TEXT, COL_BG);

  tft.setCursor(4, 32); tft.print("SSID:");
  tft.setTextColor(COL_TITLE, COL_BG);
  tft.setCursor(4, 44); tft.print(AP_SSID);

  tft.setTextColor(COL_TEXT, COL_BG);
  tft.setCursor(4, 58); tft.print("IP:");
  tft.setTextColor(COL_HILIGHT, COL_BG);
  tft.setCursor(4, 70); tft.print(WiFi.softAPIP());

  tft.setTextColor(COL_DIM, COL_BG);
  tft.setCursor(4, 90);  tft.print("Connect phone/PC to");
  tft.setCursor(4, 102); tft.print("the SSID above, then");
  tft.setCursor(4, 114); tft.print("open any web page.");
}

// =============================================================================
// PART 3 — MENUS · PASSWORD LIST · PASSWORD DETAIL · SETTINGS
// =============================================================================

// =============================================================================
// 3.1  Main Menu
// =============================================================================

static const char* MAIN_MENU_ITEMS[] = {
  "Passwords",
  "Web Portal",
  "Settings"
};
static const int MAIN_MENU_COUNT = 3;

void drawMainMenu() {
  selectedIndex    = 0;
  menuScrollOffset = 0;
  drawMenu(MAIN_MENU_ITEMS, MAIN_MENU_COUNT, selectedIndex, menuScrollOffset, "Main Menu");
}

void loopMainMenu() {
  bool redraw = false;

  if (btnUp.pressed()) {
    if (selectedIndex > 0) { selectedIndex--; redraw = true; }
  }
  if (btnDown.pressed()) {
    if (selectedIndex < MAIN_MENU_COUNT - 1) { selectedIndex++; redraw = true; }
  }
  if (redraw) {
    drawMenu(MAIN_MENU_ITEMS, MAIN_MENU_COUNT, selectedIndex, menuScrollOffset, "Main Menu");
  }

  if (btnSelect.pressed()) {
    switch (selectedIndex) {
      case 0:  // Passwords
        appState = STATE_PASS_LIST;
        selectedIndex    = 0;
        menuScrollOffset = 0;
        drawPassList();
        break;
      case 1:  // Web Portal
        Serial.println(F("[MENU] Rebooting to Web Portal Mode"));
        {
          fs::File f = LittleFS.open("/portal.flag", "w");
          if (f) { f.print("1"); f.close(); }
        }
        
        // Save session PIN to RTC memory for the web portal reboot
        strlcpy(rtcSessionPin, sessionPin, sizeof(rtcSessionPin));
        rtcPinValid = true;
        
        drawCentredMsg("Switching Modes", "Rebooting to Portal...", COL_TITLE, COL_DIM);
        delay(1000);
        ESP.restart(); // Clean reboot
        break;
      case 2:  // Settings
        appState = STATE_SETTINGS;
        selectedIndex    = 0;
        menuScrollOffset = 0;
        drawSettings();
        break;
    }
  }
  // RETURN on main menu does nothing (top level)
}

// =============================================================================
// 3.2  Password List
// =============================================================================

// Build a C-string pointer array from the live vault for drawMenu()
// We keep it as a fixed array matching MAX_CREDENTIAL_ITEMS
static const char* passListPtrs[MAX_CREDENTIAL_ITEMS];

void buildPassListPtrs() {
  for (int i = 0; i < vaultCount && i < MAX_CREDENTIAL_ITEMS; i++) {
    passListPtrs[i] = vaultIndex[i].name;
  }
}

void drawPassList() {
  buildPassListPtrs();
  if (vaultCount == 0) {
    drawCentredMsg("Vault Empty", "Add entries via portal", COL_DIM, COL_HILIGHT);
    return;
  }
  drawMenu(passListPtrs, vaultCount, selectedIndex, menuScrollOffset, "Passwords");
}

void loopPassList() {
  // Handle empty vault
  if (vaultCount == 0) {
    if (btnReturn.pressed()) {
      appState = STATE_MAIN_MENU;
      drawMainMenu();
    }
    return;
  }

  bool redraw = false;

  if (btnUp.pressed()) {
    if (selectedIndex > 0) { selectedIndex--; redraw = true; }
  }
  if (btnDown.pressed()) {
    if (selectedIndex < vaultCount - 1) { selectedIndex++; redraw = true; }
  }
  if (redraw) {
    drawMenu(passListPtrs, vaultCount, selectedIndex, menuScrollOffset, "Passwords");
  }

  if (btnSelect.pressed()) {
    selectedPassIdx  = selectedIndex;
    passRevealed     = false;
    appState         = STATE_PASS_DETAIL;
    // Reset inner selection for the detail actions menu
    selectedIndex    = 0;
    menuScrollOffset = 0;
    drawPassDetail(selectedPassIdx);
  }

  if (btnReturn.pressed()) {
    appState      = STATE_MAIN_MENU;
    selectedIndex = 0;
    menuScrollOffset = 0;
    drawMainMenu();
  }
}

// =============================================================================
// 3.3  Password Detail
// =============================================================================

// Two actions available on a selected credential
static const char* DETAIL_ACTIONS[] = {
  "View Credential",
  "Type Password (BLE)"
};
static const int DETAIL_ACTION_COUNT = 2;

// Shows name/user, then the two-action menu below
void drawPassDetail(int idx) {
  tft.fillScreen(COL_BG);
  drawStatusBar();

  // Decrypt this one entry on demand — only name and user needed for display here;
  // full struct also contains pass for reveal mode.
  Credential cred;
  bool credOk = decryptEntry(idx, sessionPin, &cred);

  // Credential header card (top area)
  tft.setTextColor(COL_TITLE, COL_BG);
  tft.setTextSize(1);
  tft.setCursor(4, USABLE_Y + 2);
  // Use lightweight index name as title (always available without decrypt)
  char nameBuf[28];
  strlcpy(nameBuf, vaultIndex[idx].name, sizeof(nameBuf));
  tft.print(nameBuf);

  tft.drawFastHLine(0, USABLE_Y + 12, tft.width(), COL_DIVIDER);

  // Username row
  tft.setTextColor(COL_DIM, COL_BG);
  tft.setCursor(4, USABLE_Y + 16);
  tft.print("User: ");
  tft.setTextColor(COL_TEXT, COL_BG);
  if (credOk) {
    char userBuf[28];
    strlcpy(userBuf, cred.user, sizeof(userBuf));
    tft.print(userBuf);
  } else {
    tft.setTextColor(COL_RED, COL_BG);
    tft.print("[err]");
  }

  // Password row — masked unless revealed
  tft.setTextColor(COL_DIM, COL_BG);
  tft.setCursor(4, USABLE_Y + 26);
  tft.print("Pass: ");
  if (passRevealed && credOk) {
    tft.setTextColor(COL_GREEN, COL_BG);
    char passBuf[28];
    strlcpy(passBuf, cred.pass, sizeof(passBuf));
    tft.print(passBuf);
  } else {
    tft.setTextColor(COL_TEXT, COL_BG);
    tft.print("* * * *");
  }

  // Wipe the decrypted credential from RAM immediately after display use
  memset(&cred, 0, sizeof(Credential));

  tft.drawFastHLine(0, USABLE_Y + 36, tft.width(), COL_DIVIDER);

  // Action menu below the card
  int actionStartY = USABLE_Y + 42;
  for (int i = 0; i < DETAIL_ACTION_COUNT; i++) {
    int y = actionStartY + i * MENU_ITEM_H;
    bool isSel = (i == selectedIndex);
    if (isSel) {
      tft.fillRect(0, y - 1, tft.width(), MENU_ITEM_H, COL_HILIGHT);
      tft.setTextColor(TFT_WHITE, COL_HILIGHT);
    } else {
      tft.setTextColor(COL_TEXT, COL_BG);
    }
    tft.setTextSize(1);
    tft.setCursor(4, y + 2);
    if (isSel) tft.print("> ");
    else        tft.print("  ");
    tft.print(DETAIL_ACTIONS[i]);
  }

  // BLE status hint at bottom
  tft.setTextSize(1);
  tft.setTextColor(COL_DIM, COL_BG);
  tft.setCursor(4, 116);
  if (bleKeyboard.bleIsConnected()) {
    tft.setTextColor(COL_GREEN, COL_BG);
    tft.print("BLE: Connected");
  } else {
    tft.print("BLE: Not paired");
  }
}

void loopPassDetail(int idx) {
  bool redraw = false;

  if (btnUp.pressed()) {
    if (selectedIndex > 0) { selectedIndex--; redraw = true; }
  }
  if (btnDown.pressed()) {
    if (selectedIndex < DETAIL_ACTION_COUNT - 1) { selectedIndex++; redraw = true; }
  }
  if (redraw) drawPassDetail(idx);

  if (btnSelect.pressed()) {
    switch (selectedIndex) {
      case 0:  // View Credential — toggle password reveal
        passRevealed = !passRevealed;
        drawPassDetail(idx);
        break;

      case 1:  // Type Password via BLE HID
        if (!bleKeyboard.bleIsConnected()) {
          // Flash error message then redraw
          drawCentredMsg("BLE Not Connected", "Pair device first", COL_RED, COL_DIM);
          delay(1500);
          drawPassDetail(idx);
        } else {
          // Show sending feedback
          tft.fillRect(0, 112, tft.width(), 16, COL_BG);
          tft.setTextSize(1);
          tft.setTextColor(COL_HILIGHT, COL_BG);
          tft.setCursor(4, 116);
          tft.print("Typing password...");

          Credential cred;
          if (decryptEntry(idx, sessionPin, &cred)) {
            bleKeyboard.bleTypeString(cred.pass);
            memset(&cred, 0, sizeof(Credential));
          }

          // Confirm sent
          tft.fillRect(0, 112, tft.width(), 16, COL_BG);
          tft.setTextColor(COL_GREEN, COL_BG);
          tft.setCursor(4, 116);
          tft.print("Sent!");
          delay(800);
          drawPassDetail(idx);
        }
        break;
    }
  }

  if (btnReturn.pressed()) {
    passRevealed  = false;
    appState      = STATE_PASS_LIST;
    selectedIndex = selectedPassIdx;   // restore list position
    menuScrollOffset = 0;
    drawPassList();
  }
}

// =============================================================================
// 3.4  Settings Menu
// =============================================================================

static const char* SETTINGS_ITEMS[] = {
  "Device Info",
  "Display Timeout"
};
static const int SETTINGS_COUNT = 2;

void drawSettings() {
  drawMenu(SETTINGS_ITEMS, SETTINGS_COUNT, selectedIndex, menuScrollOffset, "Settings");
}

void loopSettings() {
  bool redraw = false;

  if (btnUp.pressed()) {
    if (selectedIndex > 0) { selectedIndex--; redraw = true; }
  }
  if (btnDown.pressed()) {
    if (selectedIndex < SETTINGS_COUNT - 1) { selectedIndex++; redraw = true; }
  }
  if (redraw) drawSettings();

  if (btnSelect.pressed()) {
    switch (selectedIndex) {
      case 0:
        appState = STATE_SETTINGS_INFO;
        drawSettingsInfo();
        break;
      case 1:
        appState = STATE_SETTINGS_DISP;
        drawSettingsDisp();
        break;
    }
  }

  if (btnReturn.pressed()) {
    appState      = STATE_MAIN_MENU;
    selectedIndex = 0;
    menuScrollOffset = 0;
    drawMainMenu();
  }
}

// =============================================================================
// 3.5  Settings — Device Info sub-screen
// =============================================================================

void drawSettingsInfo() {
  tft.fillScreen(COL_BG);
  drawStatusBar();
  drawTitle("Device Info");

  // LittleFS storage
  size_t total = LittleFS.totalBytes();
  size_t used  = LittleFS.usedBytes();
  size_t free_ = total - used;

  auto fmtKB = [](size_t b, char* out, size_t outLen) {
    if (b >= 1024) snprintf(out, outLen, "%u KB", (unsigned)(b / 1024));
    else           snprintf(out, outLen, "%u B",  (unsigned)b);
  };

  char totalBuf[12], usedBuf[12], freeBuf[12];
  fmtKB(total,  totalBuf, sizeof(totalBuf));
  fmtKB(used,   usedBuf,  sizeof(usedBuf));
  fmtKB(free_,  freeBuf,  sizeof(freeBuf));

  int y = USABLE_Y + 16;
  const int step = 14;

  auto row = [&](const char* label, const char* val, uint16_t valCol = COL_TEXT) {
    tft.setTextSize(1);
    tft.setTextColor(COL_DIM, COL_BG);
    tft.setCursor(4, y);
    tft.print(label);
    tft.setTextColor(valCol, COL_BG);
    tft.setCursor(72, y);
    tft.print(val);
    y += step;
  };

  row("Version:",  FISKEYPASS_VERSION_STR, COL_TITLE);
  row("Entries:",  String(vaultCount).c_str());
  row("Storage:",  totalBuf);
  row("Used:",     usedBuf);
  row("Free:",     freeBuf);
  row("BLE:",      bleKeyboard.bleIsConnected() ? "Connected" : "Advertising",
                   bleKeyboard.bleIsConnected() ? COL_GREEN : COL_DIM);

  tft.setTextColor(COL_DIM, COL_BG);
  tft.setCursor(4, 116);
  tft.print("RETURN to go back");
}

void loopSettingsInfo() {
  if (btnReturn.pressed()) {
    appState = STATE_SETTINGS;
    selectedIndex = 0;
    menuScrollOffset = 0;
    drawSettings();
  }
}

// =============================================================================
// 3.6  Settings — Display Timeout sub-screen
// =============================================================================

void drawSettingsDisp() {
  tft.fillScreen(COL_BG);
  drawStatusBar();
  drawTitle("Display Timeout");

  bool on = deviceConfig.displayTimeoutEnabled;

  tft.setTextSize(1);
  tft.setTextColor(COL_DIM, COL_BG);
  tft.setCursor(4, 36);
  tft.print("Auto-blank after 30 s:");

  // Big status badge
  tft.setTextSize(2);
  uint16_t col = on ? COL_GREEN : COL_RED;
  tft.setTextColor(col, COL_BG);
  const char* status = on ? "ON" : "OFF";
  int sw = strlen(status) * 12;
  tft.setCursor((tft.width() - sw) / 2, 56);
  tft.print(status);

  tft.setTextSize(1);
  tft.setTextColor(COL_DIM, COL_BG);
  tft.setCursor(4, 84);
  tft.print("SELECT to toggle");
  tft.setCursor(4, 96);
  tft.print("RETURN to go back");
}

void loopSettingsDisp() {
  if (btnSelect.pressed()) {
    deviceConfig.displayTimeoutEnabled = !deviceConfig.displayTimeoutEnabled;
    saveConfig();
    lastActivity = millis();   // reset timer immediately
    screenOn     = true;
    drawSettingsDisp();
  }

  if (btnReturn.pressed()) {
    appState = STATE_SETTINGS;
    selectedIndex = 0;
    menuScrollOffset = 0;
    drawSettings();
  }
}

// =============================================================================
// PART 4 — WEB PORTAL · API ROUTES · loopWebPortal
// =============================================================================
//
// ESPAsyncWebServer handles requests asynchronously (no need to poll).
// DNSServer must be polled in loopWebPortal() for captive portal redirect.
//
// SECURITY RULES:
//   - Passwords are NEVER returned in GET responses
//   - All dashboard HTML served from PROGMEM (WebPortal.h)
//   - No SD card code anywhere
//   - esp_random() used for any crypto randomness
// =============================================================================

// ── Helper: check vault unlock gate ──────────────────────────────────────────
// Returns true if the vault is unlocked and the request can proceed.
// Sends a 403 JSON error and returns false otherwise.
static bool requireUnlock(AsyncWebServerRequest* req) {
  if (!vaultUnlocked) {
    req->send(403, "application/json", "{\"ok\":false,\"error\":\"Vault locked — enter PIN first\"}");
    return false;
  }
  return true;
}

// =============================================================================
// 4.1  setupWebPortalRoutes() — register all API endpoints
// =============================================================================

void setupWebPortalRoutes() {

  // ── GET / — serve the dashboard HTML from PROGMEM ──────────────────────────
  webServer.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
    AsyncWebServerResponse* r =
        req->beginResponse_P(200, "text/html", PORTAL_HTML);
    r->addHeader("Cache-Control", "no-store");
    req->send(r);
  });

  // Captive portal detection URLs — redirect all to dashboard
  auto captiveHandler = [](AsyncWebServerRequest* req) {
    req->redirect("/");
  };
  webServer.on("/generate_204",          HTTP_GET, captiveHandler);
  webServer.on("/hotspot-detect.html",   HTTP_GET, captiveHandler);
  webServer.on("/ncsi.txt",              HTTP_GET, captiveHandler);
  webServer.on("/connecttest.txt",       HTTP_GET, captiveHandler);
  webServer.onNotFound(captiveHandler);

  // Honeypot endpoints
  auto honeypotHandler = [](AsyncWebServerRequest* req) {
    securityLockout = true;
    req->send(403, "text/plain", "Forbidden");
  };
  webServer.on("/wp-admin", HTTP_ANY, honeypotHandler);
  webServer.on("/setup.php", HTTP_ANY, honeypotHandler);
  webServer.on("/phpmyadmin", HTTP_ANY, honeypotHandler);

  // ── POST /api/vault-unlock — mandatory PIN verification ────────────────────
  webServer.on(
    "/api/vault-unlock", HTTP_POST,
    [](AsyncWebServerRequest* req) {},
    nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len,
       size_t index, size_t total) {
      if (index != 0) return;

      // Brute-force lockout: after MAX_PIN_ATTEMPTS wrong PINs, refuse all
      // attempts for LOCKOUT_DURATION_MS regardless of correctness.
      if (webPinAttempts >= MAX_PIN_ATTEMPTS) {
        unsigned long elapsed = millis() - webLockoutStart;
        if (elapsed < LOCKOUT_DURATION_MS) {
          req->send(429, "application/json", "{\"ok\":false,\"error\":\"Too many attempts — locked out\"}");
          return;
        }
        webPinAttempts = 0;   // lockout window expired
      }

      JsonDocument body;
      if (deserializeJson(body, (char*)data, len)) {
        req->send(400, "application/json", "{\"ok\":false,\"error\":\"Bad JSON\"}");
        return;
      }

      const char* pin = body["pin"] | "";
      if (strlen(pin) != PIN_LENGTH) {
        req->send(400, "application/json", "{\"ok\":false,\"error\":\"PIN must be 6 characters\"}");
        return;
      }

      if (!verifyPin(pin)) {
        webPinAttempts++;
        if (webPinAttempts >= MAX_PIN_ATTEMPTS) webLockoutStart = millis();
        req->send(403, "application/json", "{\"ok\":false,\"error\":\"Wrong PIN\"}");
        return;
      }

      webPinAttempts = 0;   // success resets the counter

      // PIN verified — populate sessionPin and decrypt vault
      strlcpy(sessionPin, pin, sizeof(sessionPin));

      bool vaultOk = loadVault(sessionPin);
      if (!vaultOk && LittleFS.exists(VAULT_FILE_PATH)) {
        // Vault file exists but can't decrypt — corrupted by old blank-key bug.
        // Delete the bad file and start fresh with an empty vault.
        Serial.println(F("[WP]   Corrupt vault.dat detected — deleting and starting fresh"));
        LittleFS.remove(VAULT_FILE_PATH);
        vaultCount = 0;
      }

      vaultUnlocked = true;
      Serial.printf("[WP]   Vault unlocked via web PIN — %d entries loaded\n", vaultCount);

      String resp = String("{\"ok\":true,\"count\":") + vaultCount + "}";
      req->send(200, "application/json", resp);
    }
  );

  // ── GET /api/vault — returns names and usernames only ───────────────────────
  webServer.on("/api/vault", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!requireUnlock(req)) return;

    // Per-request iteration state. Function-local statics would corrupt
    // concurrent requests; _tempObject is auto-freed when the request is
    // destroyed (including on client abort), so there is no leak.
    struct VaultIter { int idx; int phase; };
    req->_tempObject = calloc(1, sizeof(VaultIter));
    if (!req->_tempObject) {
      req->send(500, "application/json", "{\"ok\":false,\"error\":\"Out of memory\"}");
      return;
    }

    AsyncWebServerResponse *response = req->beginChunkedResponse("application/json", [req](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
      VaultIter* it = (VaultIter*)req->_tempObject;
      if (!it) return 0;

      if (it->phase == 0) {
        it->phase = 1;
        return snprintf((char*)buffer, maxLen, "{\"entries\":[");
      }

      if (it->phase == 1) {
        if (it->idx < vaultCount) {
          Credential cred;
          if (decryptEntry(it->idx, sessionPin, &cred)) {
            JsonDocument doc;
            doc["n"] = cred.name;
            doc["u"] = cred.user;
            doc["p"] = ""; // Never send password
            String json;
            serializeJson(doc, json);
            memset(&cred, 0, sizeof(Credential)); // Wipe RAM

            size_t len = snprintf((char*)buffer, maxLen, "%s%s", (it->idx > 0) ? "," : "", json.c_str());
            it->idx++;
            return len;
          } else {
            it->idx++;
            return snprintf((char*)buffer, maxLen, " ");
          }
        } else {
          it->phase = 2;
        }
      }

      if (it->phase == 2) {
        it->phase = 3;
        return snprintf((char*)buffer, maxLen, "]}");
      }

      return 0; // done
    });
    req->send(response);
  });

  // ── POST /api/entry — add or edit entry ────────────────────────────────────
  webServer.on(
    "/api/entry", HTTP_POST,
    [](AsyncWebServerRequest* req) {},
    nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len,
       size_t index, size_t total) {
      if (!requireUnlock(req)) return;
      if (index != 0) return;

      JsonDocument body;
      DeserializationError err = deserializeJson(body, (char*)data, len);
      if (err) {
        req->send(400, "application/json", "{\"ok\":false,\"error\":\"Bad JSON\"}");
        return;
      }

      const char* n = body["n"] | "";
      const char* u = body["u"] | "";
      const char* p = body["p"] | "";

      if (strlen(n) == 0) {
        req->send(400, "application/json", "{\"ok\":false,\"error\":\"Name required\"}");
        return;
      }

      bool ok = false;
      if (body["id"].is<int>()) {
        int id = body["id"].as<int>();
        ok = updateCredential(id, n, u, (strlen(p) > 0 ? p : nullptr), sessionPin);
      } else {
        if (strlen(p) == 0) {
          req->send(400, "application/json", "{\"ok\":false,\"error\":\"Password required\"}");
          return;
        }
        ok = addCredential(n, u, p, sessionPin);
      }

      if (ok) {
        req->send(200, "application/json", "{\"ok\":true}");
      } else {
        req->send(400, "application/json", "{\"ok\":false,\"error\":\"Vault full or bad index\"}");
      }
    }
  );

  // ── DELETE /api/entry?id=N — remove one entry ───────────────────────────────
  webServer.on("/api/entry", HTTP_DELETE, [](AsyncWebServerRequest* req) {
    if (!requireUnlock(req)) return;
    if (!req->hasParam("id")) {
      req->send(400, "application/json", "{\"ok\":false,\"error\":\"Missing id param\"}");
      return;
    }
    int id = req->getParam("id")->value().toInt();
    if (deleteCredential(id, sessionPin)) {
      req->send(200, "application/json", "{\"ok\":true}");
    } else {
      req->send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid index\"}");
    }
  });

  // ── POST /api/import — multipart CSV or XML file upload ─────────────────────
  //
  // ESPAsyncWebServer multipart flow:
  //   1. Upload handler fires per-chunk (index=0 first chunk, isFinal last)
  //   2. Request handler fires AFTER all uploads complete
  //   We store the imported count via req->_tempObject so the request handler
  //   can read it and send the accurate response.
  static uint8_t* importBuf = nullptr;
  static size_t   importLen = 0;
  static bool     importOverflow = false;

  webServer.on(
    "/api/import", HTTP_POST,
    // Request completion handler (fires AFTER upload finishes)
    [](AsyncWebServerRequest* req) {
      if (!vaultUnlocked) {
        req->send(403, "application/json", "{\"ok\":false,\"error\":\"Vault locked\"}");
        return;
      }
      int imported = 0;
      if (req->_tempObject) {
        imported = *((int*)req->_tempObject);
        free(req->_tempObject);
        req->_tempObject = nullptr;
      }
      if (imported < 0) {
        req->send(400, "application/json", "{\"ok\":false,\"error\":\"Import failed or file too large (max 32 KB)\"}");
        return;
      }
      String resp = String("{\"ok\":true,\"count\":") + imported + "}";
      req->send(200, "application/json", resp);
    },
    // Upload handler (fires per-chunk as data arrives)
    [](AsyncWebServerRequest* req, const String& filename, size_t index,
       uint8_t* data, size_t len, bool isFinal) {
      if (!vaultUnlocked) return;

      if (index == 0) {
        importBuf = (uint8_t*)malloc(32768);
        importLen = 0;
        importOverflow = false;
        if (!importBuf) {
          Serial.println(F("[IMP]  Malloc failed for import buffer"));
          return;
        }
        Serial.printf("[IMP]  Upload started: %s\n", filename.c_str());
      }

      if (importBuf) {
        if (importLen + len < 32767) {
          memcpy(importBuf + importLen, data, len);
          importLen += len;
        } else {
          importOverflow = true;   // file exceeds buffer — reject, don't silently truncate
        }
      }

      if (isFinal && importBuf) {
        int imported;
        if (importOverflow) {
          Serial.println(F("[IMP]  File exceeds 32 KB buffer — import aborted"));
          imported = -1;
        } else {
          importBuf[importLen] = '\0';

          int countBefore = vaultCount;
          String fn = filename;
          fn.toLowerCase();
          if (fn.endsWith(".xml")) {
            importKeePassXML((const char*)importBuf, importLen, sessionPin);
          } else {
            importCSV((const char*)importBuf, importLen, sessionPin);
          }

          imported = vaultCount - countBefore;
          Serial.printf("[IMP]  Parsed %d new entries (total %d)\n", imported, vaultCount);
        }

        // Store count for the request completion handler
        req->_tempObject = malloc(sizeof(int));
        if (req->_tempObject) {
          *((int*)req->_tempObject) = imported;
        }

        free(importBuf);
        importBuf = nullptr;
        importLen = 0;
      }
    }
  );

  // ── GET /api/storage — LittleFS usage stats ─────────────────────────────────
  webServer.on("/api/storage", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!requireUnlock(req)) return;
    size_t total = LittleFS.totalBytes();
    size_t used  = LittleFS.usedBytes();
    String body = String("{\"total\":") + total + ",\"used\":" + used + "}";
    req->send(200, "application/json", body);
  });

  // ── POST /api/pin — change PIN ──────────────────────────────────────────────
  webServer.on(
    "/api/pin", HTTP_POST,
    [](AsyncWebServerRequest* req) {},
    nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len,
       size_t index, size_t total) {
      if (!requireUnlock(req)) return;
      if (index != 0) return;

      JsonDocument body;
      if (deserializeJson(body, (char*)data, len)) {
        req->send(400, "application/json", "{\"ok\":false,\"error\":\"Bad JSON\"}");
        return;
      }

      const char* oldPin = body["old"] | "";
      const char* newPin = body["new"] | "";

      if (strlen(oldPin) != PIN_LENGTH || strlen(newPin) != PIN_LENGTH) {
        req->send(400, "application/json", "{\"ok\":false,\"error\":\"PIN must be 6 characters\"}");
        return;
      }

      if (!verifyPin(oldPin)) {
        req->send(403, "application/json", "{\"ok\":false,\"error\":\"Wrong current PIN\"}");
        return;
      }

      // Re-encrypt vault with new PIN before changing the stored hash
      if (!saveVault(newPin)) {
        req->send(500, "application/json", "{\"ok\":false,\"error\":\"Re-encrypt failed\"}");
        return;
      }

      if (setNewPin(newPin)) {
        strlcpy(sessionPin, newPin, sizeof(sessionPin));
        req->send(200, "application/json", "{\"ok\":true}");
      } else {
        req->send(500, "application/json", "{\"ok\":false,\"error\":\"Failed to save PIN\"}");
      }
    }
  );

  // ── POST /api/reset — factory reset ─────────────────────────────────────────
  webServer.on(
    "/api/reset", HTTP_POST,
    [](AsyncWebServerRequest* req) {},
    nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len,
       size_t index, size_t total) {
      if (!requireUnlock(req)) return;
      factoryReset();
      req->send(200, "application/json", "{\"ok\":true}");
      delay(500);
      ESP.restart();
    }
  );

  Serial.println(F("[WP]   All API routes registered"));
}

// =============================================================================
// 4.2  loopWebPortal() — called from loop() while in portal mode
// =============================================================================

void loopWebPortal() {
  if (securityLockout) {
    static bool lockoutDrawn = false;
    if (!lockoutDrawn) {
      tft.fillScreen(COL_BG);
      drawStatusBar();
      drawTitle("SECURITY ALERT");
      tft.setTextSize(1);
      tft.setTextColor(COL_RED, COL_BG);
      const char* msg = "Honeypot Triggered!";
      int mx = (tft.width() - (int)strlen(msg) * 6) / 2;
      tft.setCursor(max(mx, 2), 38);
      tft.print(msg);
      tft.setTextColor(COL_DIM, COL_BG);
      tft.setCursor(4, 90);
      tft.print("Press RETURN to unlock");
      lockoutDrawn = true;
    }
    
    if (digitalRead(PIN_BTN_RETURN) == LOW) {
      securityLockout = false;
      lockoutDrawn = false;
      drawWebPortalInfo();
      delay(200); // debounce
    }
    return;
  }

  // ESPAsyncWebServer is interrupt-driven — no polling needed for HTTP.
  // We only need to service the DNS server for captive portal redirects.
  dnsServer.processNextRequest();

  // Optionally update the TFT with live client count every few seconds
  static unsigned long lastClientUpdate = 0;
  if (millis() - lastClientUpdate > 5000) {
    lastClientUpdate = millis();

    uint8_t clients = WiFi.softAPgetStationNum();
    char macStr[20] = "None";
    
    if (clients > 0) {
      wifi_sta_list_t stationList;
      esp_wifi_ap_get_sta_list(&stationList);
      if (stationList.num > 0) {
        snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                 stationList.sta[0].mac[0], stationList.sta[0].mac[1],
                 stationList.sta[0].mac[2], stationList.sta[0].mac[3],
                 stationList.sta[0].mac[4], stationList.sta[0].mac[5]);
      }
    }

    // Update client count and MAC on TFT (small area at bottom of portal screen)
    tft.fillRect(0, 108, tft.width(), 20, COL_BG);
    tft.setTextSize(1);
    tft.setTextColor(clients > 0 ? COL_GREEN : COL_DIM, COL_BG);
    tft.setCursor(4, 108);
    char buf[28];
    snprintf(buf, sizeof(buf), "Clients: %d", clients);
    tft.print(buf);
    
    tft.setCursor(4, 118);
    tft.print(clients > 0 ? macStr : "Waiting...");
  }

  // RETURN button (if held 2 s) exits portal mode and reboots
  // This prevents accidental exits while someone is browsing
  static unsigned long returnHeldSince = 0;
  if (digitalRead(PIN_BTN_RETURN) == LOW) {
    if (returnHeldSince == 0) returnHeldSince = millis();
    if (millis() - returnHeldSince > 2000) {
      // Confirm on TFT
      drawCentredMsg("Rebooting...", "Portal closing", COL_RED, COL_DIM);
      delay(800);
      ESP.restart();
    }
  } else {
    returnHeldSince = 0;
  }
}

// =============================================================================
// END OF FiskeyPass.ino  —  v4.0.1 complete
// =============================================================================
