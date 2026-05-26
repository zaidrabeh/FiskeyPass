# 📜 CHANGELOG.md — FiskeyPass Version History

## v4.0.0 (Current)
- **LittleFS Block Streaming**: Overhauled `VaultManager.h` to use fixed 188-byte blocks. Passwords are no longer held in global RAM; they are decrypted on-the-fly and wiped instantly.
- **6-Digit PIN Upgrade**: Transitioned from the legacy 4-digit PIN to a 6-digit PIN standard.
- **Web Portal Security**: PIN unlock modal updated to enforce 6-digit length.
- **WDT Protection**: Implemented `esp_task_wdt_reset()` during bulk imports (CSV/XML) to prevent hardware watchdog resets on large files.
- **Documentation**: Major formatting overhaul of `README.md`, `WIRING.md`, and technical documentation.

## v3.0.1
- **Blank Key Bug Fixed**: Resolved an issue where saving the vault without a session PIN would encrypt the payload with a blank key, causing data loss.
- **Architecture Simplification**: Removed experimental ECDH secure layer and complex JavaScript `secureFetch` wrappers in favor of air-gapped network isolation.
- **PIN Unlock Modal**: Replaced standard HTTP Basic Auth with a mandatory PIN-entry modal in the web portal.
- **Import Timing Fix**: Patched ESPAsyncWebServer multipart upload race conditions using `req->_tempObject`.
- **Corrupt Vault Recovery**: The vault now detects corrupted decryption (e.g., from power loss during write) and fails gracefully to an empty state instead of bricking the device.

## v2.2
- **AZERTY Layout**: Rewrote `asciiToHID` to properly support French AZERTY keyboards during BLE password injection.
- **BLE Connection Logic**: Removed continuous Bluetooth advertising post-connection to improve security and battery life.

## v2.1
- **ESP32 Core 3.x Migration**: Updated `mbedtls` calls to the 3.x API.
- **Library Patches**: Switched to the `mathieucarbou` forks of `ESPAsyncWebServer` and `AsyncTCP` for LwIP compatibility.
- **Reboot Flag Pattern**: Replaced the physical "hold button at boot" AP trigger with a software-driven Main Menu option that writes `/portal.flag` and reboots to avoid BLE/WiFi radio coexistence crashes.

## v2.0
- **LittleFS & BLE**: Complete rewrite. SD card storage replaced with internal flash memory via LittleFS.
- **Encryption Engine**: Implemented AES-256-GCM encryption with PBKDF2-SHA256 key derivation.
- **NimBLE HID**: Added Bluetooth keyboard emulation for direct password typing.

## v1.0
- **Prototype**: Initial menu-driven prototype.
- **Hardware Integration**: 4-button navigation mapped to GPIO 13, 33, 14, and 27.
- **State Machine**: Implemented core UI flow (SPLASH → MAIN_MENU → PASSWORDS → SETTINGS).

## v0.1
- **Hardware Test**: Initial SPI bus and TFT display initialization.