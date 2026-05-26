// =============================================================================
// SplashImage.h — FiskeyPass v2.5.1 Splash Screen Image
// =============================================================================
// Uses whoami.h (already converted to byte array).
// The firmware checks FISKEYPASS_HAS_SPLASH: bitmap if present, text fallback otherwise.
// =============================================================================

#ifndef SPLASH_IMAGE_H
#define SPLASH_IMAGE_H

#include <Arduino.h>

// Uncomment to enable the bitmap splash (requires whoami.h in project folder)
// #define FISKEYPASS_HAS_SPLASH

#ifdef FISKEYPASS_HAS_SPLASH
#include "whoami.h"
// Note: whoami.h provides:
//   WHOAMI_WIDTH, WHOAMI_HEIGHT, whoami[] (PROGMEM byte array)
// The image is 1058x1280 — too large for the 160x128 TFT.
// You must resize whoami.jpg to 160x128 and re-export as RGB565 uint16_t array
// using https://javl.github.io/image2cpp/ for direct pushImage() usage.
// Until then, the text-based splash is used.
#endif

#endif // SPLASH_IMAGE_H
