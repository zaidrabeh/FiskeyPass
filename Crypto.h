// =============================================================================
// Crypto.h — FiskeyPass v2.5.1 AES-256-GCM Encryption Engine
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
// Hash a PIN string to a hex digest for storage in config.json
// ─────────────────────────────────────────────────────────────────────────────
static bool hashPinSHA256(const char* pin, char* outHex, size_t outHexLen) {
  if (outHexLen < 65) return false;  // Need 64 hex chars + null

  uint8_t hash[32];
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0);  // 0 = SHA-256 (not SHA-224)
  mbedtls_sha256_update(&ctx, (const uint8_t*)pin, strlen(pin));
  mbedtls_sha256_finish(&ctx, hash);
  mbedtls_sha256_free(&ctx);

  for (int i = 0; i < 32; i++) {
    sprintf(&outHex[i * 2], "%02x", hash[i]);
  }
  outHex[64] = '\0';
  return true;
}

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
// Derive a 256-bit AES key from PIN + device-unique salt via PBKDF2-SHA256
// Updated for mbedtls 3.x (ESP32 Core v3.0+)
// ─────────────────────────────────────────────────────────────────────────────
static bool deriveKey(const char* pin, uint8_t* outKey, size_t keyLen) {
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

  return (ret == 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// AES-256-GCM Encrypt
//
// Input:  plaintext buffer + length
// Output: allocated buffer = [12B IV][ciphertext][16B tag]
// Caller must free() the returned buffer.
// Returns nullptr on failure.
// ─────────────────────────────────────────────────────────────────────────────
static uint8_t* encryptData(const uint8_t* key, const uint8_t* plaintext,
                             size_t ptLen, size_t* outLen) {
  *outLen = 0;

  // Total output: IV + ciphertext + tag
  size_t totalLen = GCM_IV_SIZE + ptLen + GCM_TAG_SIZE;
  uint8_t* output = (uint8_t*)malloc(totalLen);
  if (!output) return nullptr;

  // Generate random IV
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
    NULL, 0,            // No additional authenticated data
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
                             size_t encLen, size_t* outLen) {
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
    NULL, 0,            // No additional authenticated data
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
