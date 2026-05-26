# FiskeyPass v2.5.1 — Continuation Prompt

> **Copy everything below the line and paste it as your first message in the new conversation.**

---

Role: You are a Senior Embedded Systems Engineer and Cybersecurity Expert.

## Context

Read `@CONTEXT.md` first — it contains the COMPLETE project history, architecture, design decisions, hardware wiring, and file inventory. Everything you need is there.

## Task

Implement FiskeyPass v2.5.1 — an ESP32-based encrypted hardware password vault. The architecture is fully designed and approved. Your job is to write ALL the code.

## Files to Create/Modify

Create these files in `C:\Users\u\Documents\Projects\FiskeyPass\`:

### 1. `Project_Config.h` — REWRITE
- Keep `USER_SETUP_LOADED` override for TFT_eSPI (this MUST work — see CONTEXT.md Section 5.1)
- Keep TFT pin assignments (CS=15, DC=2, RST=4, MOSI=23, SCLK=18, MISO=19)
- Keep button pins (UP=13, DOWN=12, SELECT=14, RETURN=27)
- Remove ALL SD card references (no SD_CS_PIN, no SdFat audit docs)
- Add: version macros (`FISKEYPASS_VERSION_STR "v2.5.1.0"`), security constants (PIN_LENGTH=4, PBKDF2_ITERATIONS=10000), LittleFS paths (`/vault.enc`, `/config.json`), BLE device name, AP SSID

### 2. `Crypto.h` — NEW
- AES-256-GCM encryption/decryption using ESP32 built-in `mbedtls`
- PBKDF2-SHA256 key derivation from PIN + `ESP.getEfuseMac()` salt
- 96-bit IV via `esp_random()`, 16-byte GCM auth tag
- File format: `[12B IV][ciphertext][16B tag]`
- Functions: `deriveKey()`, `encryptData()`, `decryptData()`

### 3. `VaultManager.h` — NEW
- LittleFS vault load/save with encryption via Crypto.h
- Config file read/write (`/config.json` — unencrypted, stores PIN hash + settings)
- CSV import parser (Name, Username, Password columns)
- KeePass XML import using `tinyxml2` (already in project folder)
- ArduinoJson for vault JSON serialization
- Credential struct: name[32], user[64], pass[64], max 24 entries
- First boot: create default config, prompt PIN creation

### 4. `BleKeyboard.h` — NEW
- NimBLE-Arduino HID keyboard wrapper
- Device name: "FiskeyPass"
- Functions: `bleInit()`, `bleIsConnected()`, `bleTypeString(text)`
- Standard 104-key HID report map

### 5. `WebPortal.h` — NEW
- Complete dark-themed web dashboard as PROGMEM string
- No CDN (captive portal, no internet) — all CSS inline
- Dark palette: #0d1117 bg, #58a6ff accent, #f0f6fc text
- 4 tabs: Dashboard | Vault | Import | Settings
- Import: drag-and-drop file upload for .csv/.xml
- Settings: storage progress bar (`LittleFS.totalBytes()/usedBytes()`), PIN change, factory reset
- Security: passwords NEVER sent in GET responses

### 6. `SplashImage.h` — NEW
- Placeholder with instructions for converting `whoami.jpg` to 160×128 RGB565 array
- `#ifdef FISKEYPASS_HAS_SPLASH` guard — firmware falls back to text splash if absent

### 7. `FiskeyPass.ino` — REWRITE
- `#include "Project_Config.h"` MUST be the very first line (TFT_eSPI override)
- State machine: BOOT → SPLASH → PIN_ENTRY → MAIN_MENU → PASSWORDS/SETTINGS → sub-screens
- Boot: LittleFS.begin() → load config → check RETURN held → (AP mode) or (splash → PIN → decrypt vault → menu)
- PIN entry: 4 digit boxes, UP/DOWN cycle 0-9, SELECT confirms, masked with `*`, 5 attempts then 60s lockout
- Status bar: top 12px, BLE icon left, "FiskeyPass" right, divider line
- Password detail: "View" (show on screen) and "Type Password" (send via BLE HID)
- Display timeout: 30s default, toggleable
- Web portal mode: ESPAsyncWebServer captive portal with DNS redirect, all API routes from CONTEXT.md Section 5.4

### 8. `WIRING.md` — UPDATE
- Remove all SD card wiring sections
- Add note: "Storage: ESP32 internal 4MB flash via LittleFS — no external SD card"
- Keep TFT and button wiring unchanged
- Add LittleFS Data Upload tool instructions

## Critical Constraints

1. `#include "Project_Config.h"` is ALWAYS the first line in FiskeyPass.ino
2. `USER_SETUP_LOADED` must be defined before TFT_eSPI.h is included
3. No SD card code anywhere — LittleFS only
4. `tinyxml2.h` and `tinyxml2.cpp` already exist in the project folder — do NOT recreate them
5. BLE and WiFi cannot run simultaneously on ESP32 — disable BLE in portal mode, disable WiFi in normal mode
6. Passwords must NEVER appear in HTTP GET responses
7. All web HTML/CSS/JS must be in PROGMEM (not heap)
8. Use `esp_random()` for all cryptographic randomness (not `random()`)
9. Display orientation: landscape, `tft.setRotation(1)`, 160×128

## Required Libraries (user will install)
- TFT_eSPI (installed)
- NimBLE-Arduino by h2zero
- ESPAsyncWebServer + AsyncTCP
- ArduinoJson by Benoit Blanchon
- mbedtls + LittleFS (built into ESP32 core)

## Reference
- `Dr_Passwords_V1.txt` in the project folder contains a working reference implementation. Borrow menu rendering patterns, scroll logic, and web portal HTML structure from it. Do NOT copy TOTP, WiFi time sync, or USB HID code.

Please implement all files completely. Start with Project_Config.h and Crypto.h, then VaultManager.h, BleKeyboard.h, WebPortal.h, SplashImage.h, and finally FiskeyPass.ino and WIRING.md.
