<img width="456" height="547" alt="FiskeyPass no bg" src="https://github.com/user-attachments/assets/a5576797-5d08-46b8-bb1c-bc89eb204bcb" />

# 🔐 FiskeyPass v4.0.0

<p align="center">
  <img src="https://img.shields.io/badge/Hardware-ESP32-blue?style=for-the-badge&logo=espressif" />
  <img src="https://img.shields.io/badge/Storage-LittleFS-green?style=for-the-badge" />
  <img src="https://img.shields.io/badge/Encryption-AES--256--GCM-red?style=for-the-badge" />
  <img src="https://img.shields.io/badge/Protocol-BLE_HID-yellow?style=for-the-badge&logo=bluetooth" />
</p>

**FiskeyPass** is an open-source, encrypted hardware password vault built on the **ESP32**. It uses military-grade AES-256-GCM block encryption, a built-in NimBLE HID keyboard for seamless password injection, and a completely offline, air-gapped web portal for importing and managing credentials — all without a single byte leaving the device.

---

## ✨ Core Features

*   **🛡️ AES-256-GCM Encryption**: All credentials are encrypted at rest on the ESP32's internal flash (`LittleFS`) via block streaming. The encryption key is derived using PBKDF2-SHA256 from your 6-character alphanumeric PIN and the ESP32's unique hardware MAC address as salt.
*   **⌨️ Bluetooth LE HID Keyboard**: Select a password on the device's screen and it instantly "types" it into any paired smartphone, tablet, or PC like a standard Bluetooth keyboard.
*   **🌐 Air-Gapped Web Dashboard**: The device broadcasts its own WPA2-secured Wi-Fi network (`FiskeyPass-Setup`). Connecting presents a dark-themed dashboard to manage credentials, change your PIN, and import KeePass XML or CSV files. Access requires your physical 6-character device PIN — no separate portal credentials needed.
*   **🔒 RAM Security Model**: The web dashboard enforces a mandatory PIN unlock modal on every session. The vault is never fully decrypted in memory at once; blocks are decrypted on-demand and wiped instantly.
*   **📦 Zero External Dependencies**: Uses the ESP32 internal flash exclusively. No SD cards, no cloud sync, no external APIs.

---

## 🛠️ Hardware Requirements

*   **ESP32 Development Board** (Standard DevKit V1, 30 or 38 pin)
*   **1.8" ST7735 TFT Display** (128×160 resolution)
*   **4× Tactile Push Buttons**
*   Breadboard & Jumper wires

---

## 🔌 Pinout / Wiring

All buttons are wired directly from the GPIO pin to `GND` (using internal pull-ups — no external resistors needed).

| Component | ESP32 Pin | Function |
| :--- | :--- | :--- |
| **TFT SCK** | `GPIO 18` | SPI Clock |
| **TFT SDA** | `GPIO 23` | SPI MOSI |
| **TFT A0 (DC)** | `GPIO 2` | Data/Command |
| **TFT RESET** | `GPIO 4` | Hardware Reset |
| **TFT CS** | `GPIO 15` | Chip Select |
| **Button UP** | `GPIO 13` | Navigate up / cycle digit |
| **Button DOWN** | `GPIO 33` | Navigate down / cycle digit |
| **Button SELECT**| `GPIO 14` | Confirm / Reveal password |
| **Button RETURN**| `GPIO 27` | Go back / Exit Web Portal (hold 2s) |

> 📚 **See `WIRING.md`** for the full connection table, Mermaid diagram, and detailed wiring notes.

---

## 🚀 Installation & Compilation

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

---

## 🎮 Usage Guide

### 🟢 First Boot
The device prompts you to create a new **6-character PIN** (letters and digits). Use `UP`/`DOWN` to cycle each character (hold to cycle quickly) and `SELECT` to confirm each one.

### 🟡 Normal Operation
After PIN entry, the vault decrypts its index and you land on the Main Menu. Navigate with `UP`/`DOWN`, confirm with `SELECT`, go back with `RETURN`.
*   **Passwords**: Browse stored entries; hold `SELECT` to reveal the password; choose "Type Password" to inject it via BLE.
*   **Web Portal**: Select from the Main Menu. The device reboots into AP mode (BLE shuts down automatically to avoid radio conflicts).
*   **Settings**: Change display timeout, change PIN.

### 🔵 Web Portal
1. Select **Web Portal** from the Main Menu. The device reboots and broadcasts `FiskeyPass-Setup`. The **per-device WPA2 password is shown on the TFT** (a unique `FP########` derived from the chip MAC — not a shared default).
2. Connect your phone or PC to that Wi-Fi network — your browser will be redirected automatically to the local IP.
3. Enter your **device 6-character PIN** in the unlock modal. The vault index decrypts into memory.
4. Manage entries, import CSV/KeePass XML, change PIN, or factory reset.
5. Hold `RETURN` for 2 seconds on the device to exit and reboot back to normal mode.

### 🟣 Typing Passwords via BLE
1. On the device, navigate to a credential and select **Type Password**.
2. On your phone/PC, pair with the Bluetooth device named `FiskeyPass`. The host shows a **random 6-digit passkey**; type that code on the FiskeyPass (UP/DOWN to cycle digits, SELECT to confirm, RETURN to cancel) to authorise the bond.
3. The ESP32 types the password directly into the focused text field.

---

## 📥 Import Format

### CSV
```csv
Name,Username,Password
Gmail,user@gmail.com,MyP@ssw0rd
GitHub,octocat,hunter2
```
*Header row is required and skipped automatically.*

### KeePass XML
Standard KeePass v2 XML export. Group structure is flattened; only `Title`, `UserName`, and `Password` fields are imported into the hardware vault.

---

## 🔐 Security Architecture

| Layer | Mechanism |
| :--- | :--- |
| **Network** | WPA2-PSK with a per-device password (unique per unit, shown on the TFT) — blocks unauthenticated Wi-Fi connections |
| **Portal Access** | Mandatory 6-character PIN unlock modal on every session |
| **Vault at Rest** | Block-streamed AES-256-GCM, key derived via PBKDF2-SHA256 (PIN + MAC salt, 10,000 iterations); each block authenticates its position (GCM AAD) against reordering |
| **Radio Isolation** | BLE and Wi-Fi never run simultaneously — reboot flag pattern prevents coexistence crashes and dual-attack vectors |
| **PIN Lockout** | 5 wrong attempts triggers a 60-second lockout (on-device and web portal) |
| **Flash Binding** | Encryption salt includes the ESP32's unique MAC. Desoldering the flash chip to attack it offline is useless unless the exact ESP32 MAC is known. |

### Security Notes
*   Passwords are **never** sent over the API. `GET /api/vault` returns only names and usernames.
*   The vault is decrypted via block streaming (`/vault.dat`). Passwords never sit in global RAM.
*   A corrupted vault file (e.g., caused by a power loss during a write) is automatically detected and removed on the next unlock to prevent bricking.

---

## 📂 File Inventory

| File | Purpose |
| :--- | :--- |
| `FiskeyPass.ino` | Main firmware, state machine, API routes |
| `Project_Config.h` | Hardware pins, security constants, version limits |
| `Crypto.h` | AES-256-GCM engine (mbedtls 3.x) |
| `VaultManager.h` | LittleFS block streaming, CSV/XML parsers |
| `BleKeyboard.h` | NimBLE 2.x HID keyboard wrapper |
| `WebPortal.h` | Captive portal HTML/CSS/JS (PROGMEM) |
| `SplashImage.h` | RGB565 splash screen bitmap |
| `tinyxml2.h/.cpp` | KeePass XML parser |
| `WIRING.md` | Full hardware connection guide & diagrams |
| `ARCHITECTURE.md` | Technical specifications and historical architecture notes |
| `CHANGELOG.md` | Version history and update logs |

---
*FiskeyPass. Secure your payload. Own your keys.*
