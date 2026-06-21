// =============================================================================
// Crypto.h — FiskeyPass v4.0.1 AES-256-GCM Encryption Engine
// =============================================================================
//
// Uses ESP32 built-in mbedtls for:
//   - PBKDF2-SHA256 key derivation from PIN + device eFuse MAC salt
//   - AES-256-GCM authenticated encryption/decryption
//   - Cryptographic randomness via esp_random()
//
// File format: [12B IV][N-byte ciphertext][16B GCM auth tag]
//
// PIN hashing for config.json uses SHA-256 directly (for verification only).
// =============================================================================

#ifndef CRYPTO_H
#define CRYPTO_H

#include <Arduino.h>
#include <mbedtls/gcm.h>
#include <mbedtls/pkcs5.h>
#include <mbedtls/sha256.h>
#include <mbedtls/md.h>
#include <esp_system.h>

// ─────────────────────────────────────────────────────────────────────────────
// Build an 8-byte salt from the ESP32 eFuse MAC (chip-unique)
// ─────────────────────────────────────────────────────────────────────────────
static bool getMacSalt(uint8_t* salt, size_t* saltLen) {
  uint64_t mac = ESP.getEfuseMac();
  memcpy(salt, &mac, 8);
  *saltLen = 8;
  return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Hash a PIN string to a hex digest for storage in config.json.
//
// Uses salted PBKDF2-SHA256 (PBKDF2_ITERATIONS rounds) instead of a single
// unsalted SHA-256. The old scheme let anyone who read config.json recover the
// 6-digit PIN instantly via a precomputed table; salting + iteration removes
// rainbow-table attacks and forces a per-guess work factor. The 'V' suffix
// byte domain-separates this verifier from the AES key derivation so the two
// cannot be derived from one another.
//
// NOTE: this changes the stored hash format. Devices upgraded from a build
// using the old SHA-256 hash must be factory-reset (erase flash) once; the
// next boot re-creates the PIN under the new scheme.
// ─────────────────────────────────────────────────────────────────────────────
static bool hashPinVerifier(const char* pin, char* outHex, size_t outHexLen) {
  if (outHexLen < 65) return false;  // Need 64 hex chars + null

  uint8_t salt[9];
  size_t saltLen = 0;
  if (!getMacSalt(salt, &saltLen)) return false;
  salt[saltLen++] = 'V';  // domain-separate verifier from AES key derivation

  uint8_t hash[32];
  int ret = mbedtls_pkcs5_pbkdf2_hmac_ext(
    MBEDTLS_MD_SHA256,
    (const unsigned char*)pin, strlen(pin),
    salt, saltLen,
    (unsigned int)PBKDF2_ITERATIONS,
    (uint32_t)sizeof(hash), hash
  );
  if (ret != 0) return false;

  for (int i = 0; i < 32; i++) {
    sprintf(&outHex[i * 2], "%02x", hash[i]);
  }
  outHex[64] = '\0';
  return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Derive a 256-bit AES key from PIN + device-unique salt via PBKDF2-SHA256
// Updated for mbedtls 3.x (ESP32 Core v3.0+)
//
// The result is memoised: deriving the vault key costs ~100 ms, and the vault
// code calls this on every block read/write. Caching the key for the active
// PIN turns an O(n) PBKDF2 storm (one per entry) into a single derivation per
// session. Call wipeKeyCache() on lock/reset to scrub it from RAM.
// ─────────────────────────────────────────────────────────────────────────────
static char    _keyCachePin[16] = {0};
static uint8_t _keyCache[32];
static bool    _keyCacheValid = false;

static void wipeKeyCache() {
  memset(_keyCache, 0, sizeof(_keyCache));
  memset(_keyCachePin, 0, sizeof(_keyCachePin));
  _keyCacheValid = false;
}

static bool deriveKey(const char* pin, uint8_t* outKey, size_t keyLen) {
  if (_keyCacheValid && keyLen == 32 && strcmp(pin, _keyCachePin) == 0) {
    memcpy(outKey, _keyCache, 32);
    return true;
  }

  uint8_t salt[8];
  size_t saltLen = 0;
  if (!getMacSalt(salt, &saltLen)) return false;

  // mbedtls_pkcs5_pbkdf2_hmac_ext() replaces the manual md_context dance
  // that mbedtls 2.x required — works with ESP32 Core v3.0+
  int ret = mbedtls_pkcs5_pbkdf2_hmac_ext(
    MBEDTLS_MD_SHA256,
    (const unsigned char*)pin, strlen(pin),
    salt, saltLen,
    (unsigned int)PBKDF2_ITERATIONS,
    (uint32_t)keyLen, outKey
  );
  if (ret != 0) return false;

  if (keyLen == 32 && strlen(pin) < sizeof(_keyCachePin)) {
    memcpy(_keyCache, outKey, 32);
    strlcpy(_keyCachePin, pin, sizeof(_keyCachePin));
    _keyCacheValid = true;
  }
  return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// AES-256-GCM Encrypt
//
// Input:  plaintext buffer + length
// Output: allocated buffer = [12B IV][ciphertext][16B tag]
// Caller must free() the returned buffer.
// Optional AAD (e.g. the block's file offset) is authenticated but not stored,
// binding each block to its position so blocks cannot be reordered/swapped.
// Returns nullptr on failure.
// ─────────────────────────────────────────────────────────────────────────────
static uint8_t* encryptData(const uint8_t* key, const uint8_t* plaintext,
                             size_t ptLen, size_t* outLen,
                             const uint8_t* aad = nullptr, size_t aadLen = 0) {
  *outLen = 0;

  // Total output: IV + ciphertext + tag
  size_t totalLen = GCM_IV_SIZE + ptLen + GCM_TAG_SIZE;
  uint8_t* output = (uint8_t*)malloc(totalLen);
  if (!output) return nullptr;

  // Generate random IV. esp_random() is a true HW RNG only while the RF
  // subsystem (BLE or WiFi) is active — which it always is here: BLE runs in
  // normal mode and WiFi in portal mode, so every encrypt path has live entropy.
  uint8_t* iv = output;  // First 12 bytes
  uint32_t r0 = esp_random();
  uint32_t r1 = esp_random();
  uint32_t r2 = esp_random();
  memcpy(iv,     &r0, 4);
  memcpy(iv + 4, &r1, 4);
  memcpy(iv + 8, &r2, 4);

  uint8_t* ciphertext = output + GCM_IV_SIZE;
  uint8_t* tag = output + GCM_IV_SIZE + ptLen;

  // AES-256-GCM encrypt
  mbedtls_gcm_context gcm;
  mbedtls_gcm_init(&gcm);

  int ret = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 256);
  if (ret != 0) {
    mbedtls_gcm_free(&gcm);
    free(output);
    return nullptr;
  }

  ret = mbedtls_gcm_crypt_and_tag(
    &gcm, MBEDTLS_GCM_ENCRYPT,
    ptLen,
    iv, GCM_IV_SIZE,
    aad, aadLen,        // Additional authenticated data (block position)
    plaintext,
    ciphertext,
    GCM_TAG_SIZE, tag
  );

  mbedtls_gcm_free(&gcm);

  if (ret != 0) {
    free(output);
    return nullptr;
  }

  *outLen = totalLen;
  return output;
}

// ─────────────────────────────────────────────────────────────────────────────
// AES-256-GCM Decrypt
//
// Input:  encrypted buffer = [12B IV][ciphertext][16B tag]
// Output: allocated plaintext buffer
// Caller must free() the returned buffer.
// Returns nullptr on failure (wrong key or tampered data).
// ─────────────────────────────────────────────────────────────────────────────
static uint8_t* decryptData(const uint8_t* key, const uint8_t* encrypted,
                             size_t encLen, size_t* outLen,
                             const uint8_t* aad = nullptr, size_t aadLen = 0) {
  *outLen = 0;

  // Minimum size: IV + tag (empty plaintext)
  if (encLen < (size_t)(GCM_IV_SIZE + GCM_TAG_SIZE)) return nullptr;

  size_t ptLen = encLen - GCM_IV_SIZE - GCM_TAG_SIZE;

  const uint8_t* iv         = encrypted;
  const uint8_t* ciphertext = encrypted + GCM_IV_SIZE;
  const uint8_t* tag        = encrypted + GCM_IV_SIZE + ptLen;

  uint8_t* plaintext = (uint8_t*)malloc(ptLen + 1);  // +1 for null terminator
  if (!plaintext) return nullptr;

  mbedtls_gcm_context gcm;
  mbedtls_gcm_init(&gcm);

  int ret = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 256);
  if (ret != 0) {
    mbedtls_gcm_free(&gcm);
    free(plaintext);
    return nullptr;
  }

  ret = mbedtls_gcm_auth_decrypt(
    &gcm,
    ptLen,
    iv, GCM_IV_SIZE,
    aad, aadLen,        // Additional authenticated data (block position)
    tag, GCM_TAG_SIZE,
    ciphertext,
    plaintext
  );

  mbedtls_gcm_free(&gcm);

  if (ret != 0) {
    // Authentication failed — wrong PIN or tampered data
    free(plaintext);
    return nullptr;
  }

  plaintext[ptLen] = '\0';  // Null-terminate for JSON parsing convenience
  *outLen = ptLen;
  return plaintext;
}

#endif // CRYPTO_H
