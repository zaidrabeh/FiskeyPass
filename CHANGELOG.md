# 📜 CHANGELOG.md — FiskeyPass Version History

## v4.0.1 (Security)
- **Alphanumeric PIN**: The unlock/create PIN now accepts alphanumeric characters per position instead of digits only. The default charset is digits + lowercase (36 chars → 36⁶ ≈ 2.2×10⁹, ~2000× the old 10⁶ space; extend to 62 for A-Z if desired). On-device entry cycles characters with UP/DOWN (**hold to fast-cycle**) and shows a "Char X of 6" position indicator; the web modal accepts letters. BLE pairing stays numeric (BLE passkeys are 6-digit numbers).
- **BLE pairing hardened**: Replaced the insecure "Just Works" pairing (`BLE_HS_IO_NO_INPUT_OUTPUT`, any device could bond unattended) with **Passkey Entry** (`BLE_HS_IO_KEYBOARD_ONLY`). The host now displays a fresh random 6-digit passkey that must be typed on the FiskeyPass to authorise each bond, via the async `onPassKeyEntry` → `injectPassKey` flow and a new `STATE_BLE_PAIR` entry screen.
- **Salted PIN verifier**: `config.json` now stores the PIN as salted PBKDF2-SHA256 (`PBKDF2_ITERATIONS` rounds) instead of an unsalted single-round SHA-256, which could be cracked instantly from a flash dump of the 6-digit PIN space.
- **Web unlock lockout**: `/api/vault-unlock` now enforces the same `MAX_PIN_ATTEMPTS` / `LOCKOUT_DURATION_MS` lockout as on-device entry, closing the unthrottled remote PIN brute-force path.
- **Per-device AP password**: The Wi-Fi portal no longer uses the shared, published `FiskeyAdmin123`. It derives a unique WPA2 password (`FP########`) from the chip MAC and displays it on the TFT.
- **GCM block-position binding**: Each vault block now authenticates its file offset as AAD, so encrypted blocks can no longer be reordered/swapped on flash undetected.
- **Session key cache**: The PBKDF2-derived vault key is memoised for the active PIN, eliminating the per-block key-derivation storm (and scrubbed on factory reset).
- **Import overflow handling**: CSV/XML uploads larger than the 32 KB buffer are rejected with an explicit error instead of silent truncation.
- **Robustness**: heap-failure guards around BLE init, per-request state for `GET /api/vault` (no more corruption on concurrent requests), removed the debug `Test BLE` credential, and migrated to ArduinoJson v7 (`JsonDocument`).

> ⚠️ **Breaking**: the PIN hash and vault encryption (AAD) formats changed. Devices upgraded from v4.0.0 must be **factory-reset (erase flash) once**, then re-create the PIN and re-import credentials.
- **Web unlock brute-force lockout**: `/api/vault-unlock` now enforces the same `MAX_PIN_ATTEMPTS` / `LOCKOUT_DURATION_MS` lockout as the on-device PIN entry, closing the unthrottled remote PIN brute-force path over the AP.
- **Import overflow handling**: CSV/XML uploads larger than the 32 KB buffer are now rejected with an explicit error instead of being silently truncated and reported as success.

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