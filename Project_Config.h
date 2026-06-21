// =============================================================================
// Project_Config.h — FiskeyPass v4.0.1 Central Hardware & Application Configuration
// =============================================================================
//
// PURPOSE:
//   Single source of truth for all hardware pins, driver selections, security
//   constants, LittleFS paths, and application parameters.
//
//   Defining USER_SETUP_LOADED before TFT_eSPI.h is included tells the library
//   to skip its own User_Setup.h. This file MUST be #included as the VERY FIRST
//   line of FiskeyPass.ino.
//
// STORAGE:
//   The firmware uses ESP32 internal 4MB flash via LittleFS. No external SD card.
// =============================================================================

#ifndef PROJECT_CONFIG_H
#define PROJECT_CONFIG_H

// =============================================================================
// SECTION 1 — TFT_eSPI DRIVER OVERRIDE
// =============================================================================

#define USER_SETUP_LOADED  // ← Tells TFT_eSPI: "I own my config, skip yours"

// --- Driver Selection --------------------------------------------------------
#define ST7735_DRIVER        // 1.8" 128×160 TFT (ST7735S chip variant)
#define ST7735_BLACKTAB      // Tab colour for your specific module (black tab)

// --- Display Geometry --------------------------------------------------------
#define TFT_WIDTH  128
#define TFT_HEIGHT 160

// =============================================================================
// SECTION 2 — PIN ASSIGNMENTS
// =============================================================================

// TFT SPI & Control Pins (ST7735)
#define TFT_CS   15   // Chip Select   — TFT
#define TFT_DC    2   // Data/Command  — TFT (also labelled RS or A0)
#define TFT_RST   4   // Hardware Reset — TFT (pull LOW to reset)
#define TFT_MOSI 23   // SPI MOSI
#define TFT_SCLK 18   // SPI Clock
#define TFT_MISO 19   // SPI MISO

// Backlight (if your module exposes BLK / LED pin)
// Connect BLK to 3.3 V for always-on, or tie to a GPIO for software control.
// Uncomment the line below only if you want PWM backlight control:
// #define TFT_BL   -1

// =============================================================================
// SECTION 3 — SPI FREQUENCY SETTINGS
// =============================================================================

#define SPI_FREQUENCY       27000000  // 27 MHz — safe for ST7735 on breadboard
#define SPI_READ_FREQUENCY   5000000  //  5 MHz — conservative
#define SPI_TOUCH_FREQUENCY  2500000  //  2.5 MHz — reserved for future use

// =============================================================================
// SECTION 4 — FONT & RENDERING OPTIONS
// =============================================================================

#define LOAD_GLCD    // Built-in font 1 (6×8 px)
#define LOAD_FONT2   // Built-in font 2 (16 px)
#define LOAD_FONT4   // Built-in font 4 (26 px)
#define LOAD_FONT6   // Built-in font 6 (48 px, digits only)
#define LOAD_FONT7   // Built-in font 7 (7-seg style, 48 px, digits only)
#define LOAD_FONT8   // Built-in font 8 (75 px, digits only)
#define LOAD_GFXFF   // Enable FreeFont support (Adafruit GFX-compatible fonts)
#define SMOOTH_FONT  // Enable anti-aliased font rendering

// =============================================================================
// SECTION 5 — PROJECT VERSION
// =============================================================================

#define FISKEYPASS_VERSION_MAJOR 4
#define FISKEYPASS_VERSION_MINOR 0
#define FISKEYPASS_VERSION_PATCH 1
#define FISKEYPASS_VERSION_STR   "v4.0.1"

// =============================================================================
// SECTION 6 — NAVIGATION BUTTON PINS
//   Four tactile buttons wired between GPIO and GND.
//   All use INPUT_PULLUP — no external pull-up resistors required.
// =============================================================================

static constexpr int8_t PIN_BTN_UP     = 13;  // Navigate up / cycle digits
static constexpr int8_t PIN_BTN_DOWN   = 33;  // Navigate down / cycle digits
static constexpr int8_t PIN_BTN_SELECT = 14;  // Confirm / enter / reveal
static constexpr int8_t PIN_BTN_RETURN = 27;  // Go back / hold at boot for AP

// =============================================================================
// SECTION 7 — TIMING CONSTANTS
// =============================================================================

static constexpr unsigned long DEBOUNCE_MS        = 180;     // Button debounce
static constexpr unsigned long SPLASH_DISPLAY_MS  = 2000;    // Splash duration
static constexpr unsigned long DISPLAY_TIMEOUT_MS = 30000;   // Screen blanks 30s

// =============================================================================
// SECTION 8 — PASSWORD VAULT LIMITS
// =============================================================================

static constexpr int MAX_CREDENTIAL_ITEMS  = 500;
static constexpr int CREDENTIAL_NAME_LEN   = 32;
static constexpr int CREDENTIAL_USER_LEN   = 64;
static constexpr int CREDENTIAL_PASS_LEN   = 64;

// =============================================================================
// SECTION 9 — SECURITY CONSTANTS
// =============================================================================

static constexpr int  PIN_LENGTH         = 6;      // 6-character alphanumeric PIN
static constexpr int  PBKDF2_ITERATIONS  = 10000;  // Key derivation rounds
static constexpr int  MAX_PIN_ATTEMPTS   = 5;      // Before lockout
static constexpr unsigned long LOCKOUT_DURATION_MS = 60000;  // 60 second lockout
static constexpr int  AES_KEY_SIZE       = 32;     // AES-256 (bytes)
static constexpr int  GCM_IV_SIZE        = 12;     // 96-bit IV
static constexpr int  GCM_TAG_SIZE       = 16;     // 128-bit auth tag

// =============================================================================
// SECTION 10 — LittleFS FILE PATHS
// =============================================================================

#define VAULT_FILE_PATH   "/vault.dat"
#define CONFIG_FILE_PATH  "/config.json"

// =============================================================================
// SECTION 11 — BLE & NETWORK CONFIGURATION
// =============================================================================

#define BLE_DEVICE_NAME    "FiskeyPass"
#define AP_SSID            "FiskeyPass-Setup"
// AP_PASSWORD is no longer used: the portal now derives a per-device WPA2
// password from the chip MAC at runtime and shows it on the TFT. Kept only
// for reference / older builds.
#define AP_PASSWORD        "FiskeyAdmin123"
#define AP_DNS_PORT        53

// =============================================================================
// SECTION 12 — SPI BUS PINS (convenience aliases)
// =============================================================================

static constexpr int8_t PIN_SPI_SCK  = TFT_SCLK;  // GPIO 18
static constexpr int8_t PIN_SPI_MISO = TFT_MISO;   // GPIO 19
static constexpr int8_t PIN_SPI_MOSI = TFT_MOSI;   // GPIO 23

#endif // PROJECT_CONFIG_H
