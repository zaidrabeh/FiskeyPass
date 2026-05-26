#ifndef SECURE_LAYER_MANAGER_H
#define SECURE_LAYER_MANAGER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <map>

// mbedTLS headers for cryptographic operations
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/ecdh.h"
#include "mbedtls/entropy.h"
#include "mbedtls/gcm.h"
#include "mbedtls/md.h"

#define SECURE_AES_KEY_SIZE 32 // AES-256
#define SECURE_GCM_IV_SIZE 12  // 96 bit for GCM
#define SECURE_GCM_TAG_SIZE 16 // 128 bit auth tag
#define SECURE_MAX_SESSIONS 5  // Max secure sessions

class SecureLayerManager {
public:
  static SecureLayerManager &getInstance();

  bool begin();
  void end();

  String getServerPublicKey();
  bool processKeyExchange(const String &clientId, const String &clientPubKeyHex,
                          String &response);

  IRAM_ATTR bool encryptResponse(const String &clientId,
                                 const String &plaintext,
                                 String &encryptedJson);
  IRAM_ATTR bool decryptRequest(const String &clientId,
                                const String &encryptedJson, String &plaintext);

  bool isSecureSessionValid(const String &clientId);
  bool isSessionAuthenticated(const String &clientId);
  void setSessionAuthenticated(const String &clientId, bool auth);
  void invalidateSecureSession(const String &clientId);

  String wrapSecureResponse(const String &clientId,
                            const String &originalResponse);
  bool unwrapSecureRequest(const String &clientId, const String &requestBody,
                           String &unwrappedBody);

private:
  struct SecureSession {
    String clientId;
    uint8_t sessionKey[SECURE_AES_KEY_SIZE];
    uint64_t rxCounter;
    uint64_t txCounter;
    bool keyExchanged;
    bool isAuthenticated;
    unsigned long lastActivity;
    uint8_t clientNonce[16];
  };

  SecureLayerManager();
  ~SecureLayerManager();
  SecureLayerManager(const SecureLayerManager &) = delete;
  SecureLayerManager &operator=(const SecureLayerManager &) = delete;

  SecureSession *findSession(const String &clientId);
  SecureSession *createSession(const String &clientId);
  void removeSession(const String &clientId);

  bool performECDH(const uint8_t *clientPubKey, size_t keyLen,
                   uint8_t *sharedSecret);
  bool deriveSessionKey(const uint8_t *sharedSecret, const uint8_t *salt,
                        uint8_t *sessionKey);
  bool encryptData(const uint8_t *key, const uint8_t *plaintext,
                   size_t plaintextLen, uint8_t *ciphertext,
                   size_t *ciphertextLen, uint8_t *iv, uint8_t *tag);
  bool decryptData(const uint8_t *key, const uint8_t *ciphertext,
                   size_t ciphertextLen, const uint8_t *iv, const uint8_t *tag,
                   uint8_t *plaintext, size_t *plaintextLen);

  String bytesToHex(const uint8_t *bytes, size_t length);
  bool hexToBytes(const String &hex, uint8_t *bytes, size_t maxLength);
  bool generateNonce(uint8_t *nonce, size_t length);

  mbedtls_entropy_context entropy;
  mbedtls_ctr_drbg_context ctr_drbg;
  mbedtls_ecdh_context ecdh_context;

  uint8_t serverPubKey[65];
  size_t serverPubKeyLen;

  std::map<String, SecureSession> sessions;
  bool initialized;
};

#endif // SECURE_LAYER_MANAGER_H
