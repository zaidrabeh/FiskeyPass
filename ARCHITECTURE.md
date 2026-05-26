# 🧠 ARCHITECTURE.md — FiskeyPass v4 Technical Architecture

> **🎯 Purpose**: This document serves as the comprehensive technical specification and architectural blueprint for the FiskeyPass project. It outlines the state machine, storage mechanisms, encryption standards, and hardware integration details.

---

## 1️⃣ Quick Specs

| Category | Specification |
| :--- | :--- |
| **Main file** | `FiskeyPass.ino` (must match folder name for Arduino IDE) |
| **Hardware** | ESP32 DevKit V1 + 1.8" ST7735 TFT (128×160) |
| **Purpose** | Encrypted hardware password vault with BLE HID keyboard |
| **Version** | v4.0.0 |
| **Purpose** | Encrypted hardware password vault with BLE HID keyboard |
| **Version** | v4 |

---

## 2. Project History (Chronological)

### Phase 1 — Hardware Test (v0.1)
- Created `Project_Config.h` with `USER_SETUP_LOADED` override for TFT_eSPI
- Created `FiskeyPass.ino` with SPI bus init → TFT init → SD card mount → splash screen
- Created `WIRING.md` with full connection tables and Mermaid diagrams
- SD card used SdFat library on GPIO 5 (shared SPI bus with TFT)

### Phase 2 — Menu-Driven Prototype (v1.0)
- Added 4-button navigation: UP(13), DOWN(33), SELECT(14), RETURN(27)
- Implemented state machine: SPLASH → MAIN_MENU → PASSWORDS → PASS_DETAIL → SETTINGS
- Added display timeout (30s), password masking (SELECT to reveal)
- UI: dark background, gold titles, teal highlight bars

### Phase 3 — v2.0 (Migration to LittleFS & BLE)
- Complete rewrite to encrypted vault with LittleFS, BLE HID, and web portal
- Implemented AES-256-GCM encryption with PBKDF2-SHA256 key derivation
- Replaced SD card storage with internal flash via LittleFS
- Added NimBLE HID keyboard support for password typing

### Phase 4 — v2.1 (ESP32 Core 3.x & Stability Fixes)
- **Core 3.x Migration**: Updated `mbedtls` calls to 3.x API, resolved `LittleFS` namespace collisions
- **Library Patches**: Patched `ESPAsyncWebServer` and `AsyncTCP` for LwIP/mbedtls compatibility
- **Reboot Flag Pattern**: Replaced "hold button at boot" AP trigger with a Main Menu option that writes `/portal.flag` and restarts. Avoids radio coexistence crashes (BLE and WiFi never run in the same session)
- **Security**: WPA2-PSK enforced on AP, BLE pairing passkey `123456`

### Phase 5 — v2.2 (Security & Logic Fixes)
- **AZERTY Layout**: Rewrote `asciiToHID` to support French AZERTY layouts
- **Web Portal Lock-down**: Added portal credential setup page + HTTP Basic Auth (later replaced in v4)
- **BLE Connection Logic**: Removed continuous advertising post-connection

### Phase 6 — v3.0.0 (Security Layer Upgrade Attempt)
- Added `SecureLayerManager` (ECDH key exchange) and `TrafficObfuscationManager`
- These introduced mbedtls ECDH dependencies and complex JS `secureFetch()` wrappers
- `secureFetch()` was never defined in the JS — all authenticated API calls silently failed

### Phase 7 — v4 (Architecture Hardening — CURRENT)
- **Blank Key Bug Fixed**: `sessionPin` was never populated in the portal path, so `saveVault("")` encrypted with a blank-derived key, making the vault unreadable on reboot
- **Purged ECDH complexity**: Removed `SecureLayerManager`, `TrafficObfuscationManager`, `secureFetch()`, `authenticatedClients` set, and all `X-Client-ID` auth
- **PIN unlock modal**: Replaced the old login-credential system with a mandatory 4-digit device PIN entry in the browser (`POST /api/vault-unlock`)
- **Import timing fixed**: ESPAsyncWebServer multipart fires the request handler before the upload handler; fixed via `req->_tempObject` to pass count from upload handler to completion handler
- **AP Boot Gatekeeper fixed**: Portal now boots whenever `/portal.flag` exists, regardless of RTC PIN state (hard reset path is safe because web UI handles auth)
- **Corrupt vault recovery**: If correct PIN passes `verifyPin()` but `loadVault()` fails (old blank-key corruption), the bad `vault.enc` is deleted and the vault starts fresh rather than blocking permanently

---

## 3. Current File Inventory

```
FiskeyPass/
├── FiskeyPass.ino           — Main app, state machine, API routes (~60 KB)
├── Project_Config.h         — Hardware pins, security constants, version
├── Crypto.h                 — AES-256-GCM engine (mbedtls 3.x)
├── VaultManager.h           — LittleFS vault read/write, CSV/XML parsers
├── BleKeyboard.h            — NimBLE 2.x HID keyboard wrapper
├── WebPortal.h              — Captive portal HTML/CSS/JS (PROGMEM, ~22 KB)
├── SplashImage.h            — RGB565 splash bitmap

├── tinyxml2.h/.cpp          — KeePass XML parser
├── WIRING.md                — Hardware connection guide
├── ARCHITECTURE.md          — Technical specifications and state machine
├── CHANGELOG.md             — Version history and updates
└── README.md                — User-facing documentation
```

---

## 4. Hardware Wiring (Current)

### TFT Display (ST7735, Left-Side Header)
| Pin | GPIO | Function |
|-----|------|----------|
| LED | 3V3 | Backlight (always-on) |
| SCK | 18 | SPI Clock |
| SDA | 23 | SPI MOSI |
| AO  | 2  | Data/Command (DC) |
| RESET | 4 | Hardware reset |
| CS  | 15 | TFT Chip Select |
| GND | GND | Ground |
| VCC | 3V3 | Power |

### Navigation Buttons (INPUT_PULLUP, wired to GND)
| Button | GPIO | Function |
|--------|------|----------|
| UP     | 13   | Navigate up / cycle digits |
| DOWN   | 33   | Navigate down / cycle digits |
| SELECT | 14   | Confirm / enter / reveal |
| RETURN | 27   | Go back / Exit Web Portal (hold 2s) |

> **GPIO 33 note**: GPIO 33 is input-only on the ESP32. If it always reads LOW, add an external 10 kΩ pull-up between GPIO 33 and 3V3.

---

## 5. v4 Architecture

### 5.1 State Machine
```
BOOT
  ├─ /portal.flag exists → Delete flag → Clear sessionPin & rtcSessionPin
  │                      → STATE_WEB_PORTAL (web UI prompts for PIN)
  └─ No flag → STATE_SPLASH → PIN_ENTRY → MAIN_MENU
                                              ├→ PASSWORDS → PASS_DETAIL
                                              │                ├→ View (masked/plain)
                                              │                └→ Type Password (BLE HID)
                                              ├→ WEB PORTAL (write flag → reboot)
                                              └→ SETTINGS
```

### 5.2 Security Architecture

**PIN Authentication:**
- 6-digit PIN stored as SHA-256 hash in `/config.json`
- 5 wrong attempts → 60-second lockout
- First boot → force user to set a new PIN

**Encryption (mbedtls 3.x):**
- **Algorithm**: AES-256-GCM
- **Key Derivation**: PBKDF2-SHA256(PIN + ESP32 MAC salt, 10,000 iterations)
- **Storage**: `/vault.dat` on LittleFS (Block streaming)

### 5.3 Web Portal (v4)
- **Network Security**: WPA2-PSK (`FiskeyPass-Setup` / `FiskeyAdmin123`)
- **Vault Security**: Mandatory 6-digit PIN unlock modal on every session (`POST /api/vault-unlock`)
- **Auth gate**: `vaultUnlocked` boolean (static global in `FiskeyPass.ino`) — all API routes call `requireUnlock()` before any operation
- **Passwords**: Never returned by the API — `GET /api/vault` returns names and usernames only
- **Import**: CSV and KeePass XML; `req->_tempObject` bridges upload handler count to completion handler
- **Corrupt vault recovery**: Bad `vault.enc` detected → deleted → fresh empty vault unlocked
- **Radio isolation**: Portal session never initializes BLE; normal session never initializes WiFi

### 5.4 API Routes
| Method | Path | Auth | Description |
|--------|------|------|-------------|
| GET | `/` | None | Serve dashboard HTML |
| POST | `/api/vault-unlock` | None | Verify PIN, decrypt vault, set `vaultUnlocked` |
| GET | `/api/vault` | `vaultUnlocked` | Entry list (name + username, no passwords) |
| POST | `/api/entry` | `vaultUnlocked` | Add or edit entry |
| DELETE | `/api/entry?id=N` | `vaultUnlocked` | Remove entry |
| POST | `/api/import` | `vaultUnlocked` | Upload CSV or KeePass XML |
| GET | `/api/storage` | `vaultUnlocked` | LittleFS usage stats |
| POST | `/api/pin` | `vaultUnlocked` | Change PIN (re-encrypts vault) |
| POST | `/api/reset` | `vaultUnlocked` | Factory reset + reboot |

### 5.5 BLE HID Keyboard
- **Library**: NimBLE-Arduino 2.x
- **Security**: Passkey `123456` (IO Cap: Display Only)
- **Initialization**: Deferred until after successful PIN entry on TFT
- **Radio**: Disabled entirely during portal sessions

---

## 6. Required Libraries

| Library | Author | Note |
|---------|--------|------|
| TFT_eSPI | Bodmer | |
| NimBLE-Arduino | h2zero | v2.x required |
| AsyncTCP | mathieucarbou | **Use Mathieu Carbou fork** |
| ESPAsyncWebServer | mathieucarbou | **Use Mathieu Carbou fork** |
| ArduinoJson | Benoit Blanchon | v7.x required |

---

## 7. Design Decisions (Confirmed)

- **Storage**: LittleFS (internal flash) ONLY — no SD card
- **AP Access**: WPA2-PSK secured
- **Portal Auth**: Device 6-digit PIN via web modal, not separate credentials
- **Reboot Flag**: Used to avoid simultaneous BLE/WiFi radio conflicts
- **No ECDH / HTTPS**: Plain HTTP/JSON — air-gapped LAN use only
- **mbedtls**: Used for AES-256-GCM and PBKDF2 only (no ECDH)

---

## 8. Known Issues & Gotchas

1. **TCPIP Core Locking**: ESP32 Core 3.x requires TCPIP core locking. Use `mathieucarbou` forks of AsyncTCP and ESPAsyncWebServer.
2. **mbedtls 3.x Headers**: `mbedtls_pkcs5_pbkdf2_hmac` is now `mbedtls_pkcs5_pbkdf2_hmac_ext`.
3. **GPIO 33**: Input-only on ESP32 — may need external pull-up if always reads LOW.
4. **Namespace**: `LittleFS` file operations require `fs::` prefix (e.g., `fs::File`).
5. **NimBLE deinit**: `NimBLEDevice::deinit(true)` panics in ESP-IDF v5 — use Reboot Flag pattern instead.
6. **ArduinoJson**: Use sufficient document size for vault parsing (~4 KB for 24 entries).
7. **PROGMEM for web HTML**: Dashboard HTML must be in PROGMEM to avoid consuming heap RAM.
8. **ESPAsyncWebServer multipart order**: Request completion handler fires *before* upload handler in some versions — use `req->_tempObject` to pass state from upload to completion handler.
