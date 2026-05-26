# FiskeyPass v3.0.1

FiskeyPass is an open-source, encrypted hardware password vault built on the ESP32. It uses AES-256-GCM encryption, a built-in NimBLE HID keyboard for password injection, and a completely offline, air-gapped web portal for importing and managing credentials — all without any cloud connectivity.

## Core Features

- **AES-256-GCM Encryption**: All credentials are encrypted at rest on the ESP32's internal flash (`LittleFS`). The encryption key is derived using PBKDF2-SHA256 from your 4-digit PIN and the ESP32's unique hardware MAC address as salt.
- **Bluetooth LE HID Keyboard**: Select a password on the device's screen and it will instantly "type" it into any paired smartphone, tablet, or PC.
- **Offline Web Dashboard**: The device broadcasts its own WPA2-secured WiFi network (`FiskeyPass-Setup`). Connecting presents a dark-themed dashboard to manage credentials, change your PIN, and import KeePass XML or CSV files. Access requires your physical 4-digit device PIN — no separate portal credentials.
- **PIN-Locked Web Portal**: The web dashboard enforces a mandatory PIN unlock modal on every session. The vault is never decrypted in memory until the correct PIN is entered via the browser.
- **Zero External Dependencies**: Uses the ESP32 internal flash only. No SD cards, no cloud sync, no external APIs.

## Hardware Requirements

- **ESP32 Development Board** (Standard DevKit V1, 30 or 38 pin)
- **1.8" ST7735 TFT Display** (128×160 resolution)
- **4× Tactile Push Buttons**

## Pinout / Wiring

All buttons are wired directly from the GPIO pin to `GND` (using internal pull-ups — no external resistors needed).

| Component | ESP32 Pin | Function |
| :--- | :--- | :--- |
| **TFT SCK** | GPIO 18 | SPI Clock |
| **TFT SDA** | GPIO 23 | SPI MOSI |
| **TFT A0 (DC)** | GPIO 2 | Data/Command |
| **TFT RESET** | GPIO 4 | Hardware Reset |
| **TFT CS** | GPIO 15 | Chip Select |
| **Button UP** | GPIO 13 | Navigate up / cycle digit |
| **Button DOWN** | GPIO 33 | Navigate down / cycle digit |
| **Button SELECT** | GPIO 14 | Confirm / Reveal password |
| **Button RETURN** | GPIO 27 | Go back / Exit Web Portal (hold 2s) |

> See `WIRING.md` for the full connection table, Mermaid diagram, and wiring notes.

## Installation & Compilation

FiskeyPass requires **ESP32 Core v3.x** and specific library versions to compile successfully.

### Required Libraries (Install via Arduino Library Manager)

1. `TFT_eSPI` by Bodmer
2. `NimBLE-Arduino` by h2zero (v2.x or later)
3. `ArduinoJson` by Benoit Blanchon (v7.x or later)
4. `ESPAsyncWebServer` — **Must use the [Mathieu Carbou fork](https://github.com/mathieucarbou/ESPAsyncWebServer)**
5. `AsyncTCP` — **Must use the [Mathieu Carbou fork](https://github.com/mathieucarbou/AsyncTCP)**

### Compilation Setup

1. Open `FiskeyPass.ino` in the Arduino IDE.
2. Select your ESP32 board (e.g., "DOIT ESP32 DEVKIT V1").
3. Set Partition Scheme to **Huge APP (3MB No OTA/1MB SPIFFS)** or similar to fit the binary.
4. Hit **Upload**.

## Usage

### First Boot
The device prompts you to create a new 4-digit PIN. Use **UP/DOWN** to cycle digits and **SELECT** to confirm each one.

### Normal Operation
After PIN entry the vault decrypts and you land on the Main Menu. Navigate with **UP/DOWN**, confirm with **SELECT**, go back with **RETURN**.

- **Passwords**: Browse stored entries; hold **SELECT** to reveal the password; choose "Type Password" to inject it via BLE.
- **Web Portal**: Select from the Main Menu. The device reboots into AP mode (BLE shuts down automatically to avoid radio conflicts).
- **Settings**: Change display timeout, change PIN.

### Web Portal
1. Select **Web Portal** from the Main Menu. The device reboots and broadcasts `FiskeyPass-Setup` (password: `FiskeyAdmin123`).
2. Connect your phone or PC to that Wi-Fi network — your browser will be redirected automatically.
3. Enter your **device 4-digit PIN** in the unlock modal. The vault decrypts in memory.
4. Manage entries, import CSV/KeePass XML, change PIN, or factory reset.
5. Hold **RETURN** for 2 seconds to exit and reboot back to normal mode.

### Typing Passwords via BLE
1. On the device, navigate to a credential and select **Type Password**.
2. On your phone/PC, pair with the Bluetooth device named `FiskeyPass`. Enter passkey `123456` when prompted.
3. The ESP32 types the password directly into the focused text field.

## Import Format

### CSV
```
Name,Username,Password
Gmail,user@gmail.com,MyP@ssw0rd
GitHub,octocat,hunter2
```
Header row is required and skipped automatically. Maximum 24 entries total.

### KeePass XML
Standard KeePass v2 XML export. Group structure is flattened; only `Title`, `UserName`, and `Password` fields are imported.

## Security Architecture

| Layer | Mechanism |
| :--- | :--- |
| **Network** | WPA2-PSK (`FiskeyAdmin123`) — blocks unauthenticated Wi-Fi connections |
| **Portal access** | Mandatory 4-digit PIN unlock modal on every session |
| **Vault at rest** | AES-256-GCM, key derived via PBKDF2-SHA256(PIN + MAC salt, 10 000 iterations) |
| **Radio isolation** | BLE and Wi-Fi never run simultaneously — reboot flag pattern prevents coexistence crashes |
| **PIN lockout** | 5 wrong attempts triggers a 60-second lockout |
| **Flash binding** | Encryption salt includes the ESP32's unique MAC — moving the flash chip to another board does not compromise the vault even if the PIN is known |

## Security Notes

- Passwords are **never** sent over the API. `GET /api/vault` returns only names and usernames.
- The vault is decrypted into RAM only after a successful PIN verification, and only for the duration of the portal session.
- A corrupted vault file (e.g., caused by a power loss during a write) is automatically detected and removed on the next unlock. The vault starts empty rather than blocking access permanently.
- Entering the wrong PIN 5 times triggers a 60-second hardware lockout on the device screen.

## File Inventory

| File | Purpose |
| :--- | :--- |
| `FiskeyPass.ino` | Main firmware, state machine, API routes |
| `Project_Config.h` | Hardware pins, security constants, version |
| `Crypto.h` | AES-256-GCM engine (mbedtls 3.x) |
| `VaultManager.h` | LittleFS vault read/write, CSV/XML parsers |
| `BleKeyboard.h` | NimBLE 2.x HID keyboard wrapper |
| `WebPortal.h` | Captive portal HTML/CSS/JS (PROGMEM) |
| `SplashImage.h` | RGB565 splash screen bitmap |
| `tinyxml2.h/.cpp` | KeePass XML parser |
| `WIRING.md` | Full hardware connection guide |
| `CONTEXT.md` | Developer context and architecture notes |

## License

This project is open-source. See the source files for individual library licenses.
