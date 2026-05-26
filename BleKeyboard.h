// =============================================================================
// BleKeyboard.h — FiskeyPass v2.5.1 NimBLE HID Keyboard Wrapper
// =============================================================================
//
// Provides BLE HID keyboard functionality using NimBLE-Arduino by h2zero.
// Device name: "FiskeyPass"
//
// IMPORTANT: BLE and WiFi cannot run simultaneously on ESP32.
//   - Normal mode: BLE enabled, WiFi off
//   - Web portal mode: WiFi enabled, BLE off
// =============================================================================

#ifndef BLE_KEYBOARD_H
#define BLE_KEYBOARD_H

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include "Project_Config.h"

static char connectedBleMac[20] = "";

// HID Report Descriptor — Standard 104-key keyboard
// Boot-compatible keyboard with 8-byte input reports
static const uint8_t HID_REPORT_MAP[] = {
  0x05, 0x01,  // Usage Page (Generic Desktop)
  0x09, 0x06,  // Usage (Keyboard)
  0xA1, 0x01,  // Collection (Application)
  0x85, 0x01,  //   Report ID (1)
  // Modifier keys (8 bits)
  0x05, 0x07,  //   Usage Page (Key Codes)
  0x19, 0xE0,  //   Usage Minimum (224 - Left Control)
  0x29, 0xE7,  //   Usage Maximum (231 - Right GUI)
  0x15, 0x00,  //   Logical Minimum (0)
  0x25, 0x01,  //   Logical Maximum (1)
  0x75, 0x01,  //   Report Size (1)
  0x95, 0x08,  //   Report Count (8)
  0x81, 0x02,  //   Input (Data, Variable, Absolute)
  // Reserved byte
  0x95, 0x01,  //   Report Count (1)
  0x75, 0x08,  //   Report Size (8)
  0x81, 0x01,  //   Input (Constant)
  // LED output report (5 bits + 3 padding)
  0x95, 0x05,  //   Report Count (5)
  0x75, 0x01,  //   Report Size (1)
  0x05, 0x08,  //   Usage Page (LEDs)
  0x19, 0x01,  //   Usage Minimum (1 - Num Lock)
  0x29, 0x05,  //   Usage Maximum (5 - Kana)
  0x91, 0x02,  //   Output (Data, Variable, Absolute)
  0x95, 0x01,  //   Report Count (1)
  0x75, 0x03,  //   Report Size (3)
  0x91, 0x01,  //   Output (Constant)
  // Key array (6 keys)
  0x95, 0x06,  //   Report Count (6)
  0x75, 0x08,  //   Report Size (8)
  0x15, 0x00,  //   Logical Minimum (0)
  0x25, 0x65,  //   Logical Maximum (101)
  0x05, 0x07,  //   Usage Page (Key Codes)
  0x19, 0x00,  //   Usage Minimum (0)
  0x29, 0x65,  //   Usage Maximum (101)
  0x81, 0x00,  //   Input (Data, Array)
  0xC0         // End Collection
};

// ─────────────────────────────────────────────────────────────────────────────
// HID Keycode Lookup
// Maps ASCII characters to HID usage codes + modifier
// ─────────────────────────────────────────────────────────────────────────────

struct KeyMapping {
  uint8_t keycode;
  uint8_t modifier;  // 0x00 = none, 0x02 = Left Shift
};

// Returns HID keycode + modifier for an ASCII character
static KeyMapping asciiToHID(char c) {
  KeyMapping k = {0, 0};
  switch (c) {
    // Lowercase letters
    case 'a': k.keycode = 0x14; break;
    case 'b': k.keycode = 0x05; break;
    case 'c': k.keycode = 0x06; break;
    case 'd': k.keycode = 0x07; break;
    case 'e': k.keycode = 0x08; break;
    case 'f': k.keycode = 0x09; break;
    case 'g': k.keycode = 0x0A; break;
    case 'h': k.keycode = 0x0B; break;
    case 'i': k.keycode = 0x0C; break;
    case 'j': k.keycode = 0x0D; break;
    case 'k': k.keycode = 0x0E; break;
    case 'l': k.keycode = 0x0F; break;
    case 'm': k.keycode = 0x33; break;
    case 'n': k.keycode = 0x11; break;
    case 'o': k.keycode = 0x12; break;
    case 'p': k.keycode = 0x13; break;
    case 'q': k.keycode = 0x04; break;
    case 'r': k.keycode = 0x15; break;
    case 's': k.keycode = 0x16; break;
    case 't': k.keycode = 0x17; break;
    case 'u': k.keycode = 0x18; break;
    case 'v': k.keycode = 0x19; break;
    case 'w': k.keycode = 0x1D; break;
    case 'x': k.keycode = 0x1B; break;
    case 'y': k.keycode = 0x1C; break;
    case 'z': k.keycode = 0x1A; break;

    // Uppercase letters
    case 'A': k.keycode = 0x14; k.modifier = 0x02; break;
    case 'B': k.keycode = 0x05; k.modifier = 0x02; break;
    case 'C': k.keycode = 0x06; k.modifier = 0x02; break;
    case 'D': k.keycode = 0x07; k.modifier = 0x02; break;
    case 'E': k.keycode = 0x08; k.modifier = 0x02; break;
    case 'F': k.keycode = 0x09; k.modifier = 0x02; break;
    case 'G': k.keycode = 0x0A; k.modifier = 0x02; break;
    case 'H': k.keycode = 0x0B; k.modifier = 0x02; break;
    case 'I': k.keycode = 0x0C; k.modifier = 0x02; break;
    case 'J': k.keycode = 0x0D; k.modifier = 0x02; break;
    case 'K': k.keycode = 0x0E; k.modifier = 0x02; break;
    case 'L': k.keycode = 0x0F; k.modifier = 0x02; break;
    case 'M': k.keycode = 0x33; k.modifier = 0x02; break;
    case 'N': k.keycode = 0x11; k.modifier = 0x02; break;
    case 'O': k.keycode = 0x12; k.modifier = 0x02; break;
    case 'P': k.keycode = 0x13; k.modifier = 0x02; break;
    case 'Q': k.keycode = 0x04; k.modifier = 0x02; break;
    case 'R': k.keycode = 0x15; k.modifier = 0x02; break;
    case 'S': k.keycode = 0x16; k.modifier = 0x02; break;
    case 'T': k.keycode = 0x17; k.modifier = 0x02; break;
    case 'U': k.keycode = 0x18; k.modifier = 0x02; break;
    case 'V': k.keycode = 0x19; k.modifier = 0x02; break;
    case 'W': k.keycode = 0x1D; k.modifier = 0x02; break;
    case 'X': k.keycode = 0x1B; k.modifier = 0x02; break;
    case 'Y': k.keycode = 0x1C; k.modifier = 0x02; break;
    case 'Z': k.keycode = 0x1A; k.modifier = 0x02; break;

    // Numbers (Shifted on AZERTY)
    case '1': k.keycode = 0x1E; k.modifier = 0x02; break;
    case '2': k.keycode = 0x1F; k.modifier = 0x02; break;
    case '3': k.keycode = 0x20; k.modifier = 0x02; break;
    case '4': k.keycode = 0x21; k.modifier = 0x02; break;
    case '5': k.keycode = 0x22; k.modifier = 0x02; break;
    case '6': k.keycode = 0x23; k.modifier = 0x02; break;
    case '7': k.keycode = 0x24; k.modifier = 0x02; break;
    case '8': k.keycode = 0x25; k.modifier = 0x02; break;
    case '9': k.keycode = 0x26; k.modifier = 0x02; break;
    case '0': k.keycode = 0x27; k.modifier = 0x02; break;

    // Symbols (unshifted)
    case '&': k.keycode = 0x1E; break;
    case '"': k.keycode = 0x20; break;
    case '\'': k.keycode = 0x21; break;
    case '(': k.keycode = 0x22; break;
    case '-': k.keycode = 0x23; break;
    case '_': k.keycode = 0x25; break;
    case ')': k.keycode = 0x2D; break;
    case '=': k.keycode = 0x2E; break;
    case ',': k.keycode = 0x10; break;
    case ';': k.keycode = 0x36; break;
    case ':': k.keycode = 0x37; break;
    case '!': k.keycode = 0x38; break;
    case '*': k.keycode = 0x31; break;
    case '$': k.keycode = 0x30; break;
    case '<': k.keycode = 0x64; break;

    // Symbols (shifted)
    case '+': k.keycode = 0x2E; k.modifier = 0x02; break;
    case '?': k.keycode = 0x10; k.modifier = 0x02; break;
    case '.': k.keycode = 0x36; k.modifier = 0x02; break;
    case '/': k.keycode = 0x37; k.modifier = 0x02; break;
    case '%': k.keycode = 0x34; k.modifier = 0x02; break;
    case '>': k.keycode = 0x64; k.modifier = 0x02; break;

    // Symbols (AltGr)
    case '@': k.keycode = 0x27; k.modifier = 0x40; break;
    case '#': k.keycode = 0x20; k.modifier = 0x40; break;
    case '~': k.keycode = 0x1F; k.modifier = 0x40; break;
    case '{': k.keycode = 0x21; k.modifier = 0x40; break;
    case '}': k.keycode = 0x2E; k.modifier = 0x40; break;
    case '[': k.keycode = 0x22; k.modifier = 0x40; break;
    case ']': k.keycode = 0x2D; k.modifier = 0x40; break;
    case '|': k.keycode = 0x23; k.modifier = 0x40; break;
    case '\\': k.keycode = 0x25; k.modifier = 0x40; break;
    case '^': k.keycode = 0x2F; break; // Note: dead key on standard layout

    // Whitespace
    case ' ': k.keycode = 0x2C; break;
    case '\n': case '\r': k.keycode = 0x28; break;
    case '\t': k.keycode = 0x2B; break;
    
    default: break;
  }
  return k;
}

// ─────────────────────────────────────────────────────────────────────────────
// BLE Keyboard Class
// ─────────────────────────────────────────────────────────────────────────────

class FiskeyBLE {
private:
  NimBLEServer*         _server       = nullptr;
  NimBLEHIDDevice*      _hid          = nullptr;
  NimBLECharacteristic* _inputReport  = nullptr;
  bool                  _connected    = false;
  bool                  _initialized  = false;

  // Connection callbacks
  class ServerCallbacks : public NimBLEServerCallbacks {
  public:
    FiskeyBLE* parent;
    ServerCallbacks(FiskeyBLE* p) : parent(p) {}

    void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
      parent->_connected = true;
      strlcpy(connectedBleMac, connInfo.getAddress().toString().c_str(), sizeof(connectedBleMac));
      Serial.printf("[BLE]  Client connected: %s\n", connectedBleMac);
      // Removed startAdvertising() to ensure single connection
    }

    void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
      parent->_connected = false;
      connectedBleMac[0] = '\0';
      Serial.println(F("[BLE]  Client disconnected"));
      NimBLEDevice::startAdvertising();
    }
  };

public:
  // Initialize NimBLE stack and HID service
  bool bleInit() {
    if (_initialized) return true;

    NimBLEDevice::init(BLE_DEVICE_NAME);
    NimBLEDevice::setSecurityAuth(true, true, true);  // bonding, MITM, SC
    // Removed setSecurityPasskey to allow "Just Works" pairing without phone forcing a typed PIN
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);

    _server = NimBLEDevice::createServer();
    _server->setCallbacks(new ServerCallbacks(this));

    _hid = new NimBLEHIDDevice(_server);
    _inputReport = _hid->getInputReport(1);           // NimBLE 2.x API

    _hid->setManufacturer("FiskeyPass");              // NimBLE 2.x API
    _hid->setPnp(0x02, 0xE502, 0xA111, 0x0210);       // NimBLE 2.x API
    _hid->setHidInfo(0x00, 0x01);                     // NimBLE 2.x API
    _hid->setReportMap((uint8_t*)HID_REPORT_MAP, sizeof(HID_REPORT_MAP)); // NimBLE 2.x API

    _hid->startServices();

    // Advertising
    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->setAppearance(HID_KEYBOARD);
    adv->addServiceUUID(_hid->getHidService()->getUUID()); // NimBLE 2.x API
    adv->setName(BLE_DEVICE_NAME);
    adv->start();

    _initialized = true;
    Serial.println(F("[BLE]  HID Keyboard initialized"));
    return true;
  }

  // Deinitialize BLE (for WiFi portal mode)
  void bleDeinit() {
    if (!_initialized) return;
    // Don't free BT memory (true) as it crashes ESP-IDF v5 when WiFi is starting
    NimBLEDevice::deinit(false);
    _initialized = false;
    _connected = false;
    _server = nullptr;
    _hid = nullptr;
    _inputReport = nullptr;
    Serial.println(F("[BLE]  Deinitialized"));
  }

  bool bleIsConnected() {
    return _connected;
  }

  bool bleIsInitialized() {
    return _initialized;
  }

  // Send a single keystroke
  void bleSendKey(uint8_t modifier, uint8_t keycode) {
    if (!_connected || !_inputReport) return;

    uint8_t report[8] = {0};
    report[0] = modifier;
    report[2] = keycode;
    _inputReport->setValue(report, sizeof(report));
    _inputReport->notify();

    // Randomized duration: 15ms base + 0..15ms random
    delay(15 + (esp_random() % 15));

    // Release all keys
    memset(report, 0, sizeof(report));
    _inputReport->setValue(report, sizeof(report));
    _inputReport->notify();

    // Randomized inter-key delay: 15ms base + 0..30ms random
    delay(15 + (esp_random() % 30));
  }

  // Type a string via BLE HID keystrokes
  void bleTypeString(const char* text) {
    if (!_connected || !_inputReport || !text) return;

    Serial.printf("[BLE]  Typing %d chars\n", strlen(text));

    for (size_t i = 0; text[i] != '\0'; i++) {
      KeyMapping km = asciiToHID(text[i]);
      if (km.keycode != 0) {
        bleSendKey(km.modifier, km.keycode);
      }
    }
  }
};

// Global BLE keyboard instance
static FiskeyBLE bleKeyboard;

#endif // BLE_KEYBOARD_H
