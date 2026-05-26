#include "SecureLayerManager.h"

SecureLayerManager &SecureLayerManager::getInstance() {
  static SecureLayerManager instance;
  return instance;
}

SecureLayerManager::SecureLayerManager() : initialized(false) {}

SecureLayerManager::~SecureLayerManager() { end(); }

bool SecureLayerManager::begin() {
  if (initialized)
    return true;

  mbedtls_entropy_init(&entropy);
  mbedtls_ctr_drbg_init(&ctr_drbg);
  mbedtls_ecdh_init(&ecdh_context);

  const char *pers = "fiskeypass_secure_layer_v3";
  int ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                  (const unsigned char *)pers, strlen(pers));
  if (ret != 0)
    return false;

  ret = mbedtls_ecdh_setup(&ecdh_context, MBEDTLS_ECP_DP_SECP256R1);
  if (ret != 0)
    return false;

  ret = mbedtls_ecdh_make_public(&ecdh_context, &serverPubKeyLen, serverPubKey,
                                 sizeof(serverPubKey), mbedtls_ctr_drbg_random,
                                 &ctr_drbg);
  if (ret != 0)
    return false;

  initialized = true;
  return true;
}

void SecureLayerManager::end() {
  if (!initialized)
    return;
  for (auto &pair : sessions) {
    memset(pair.second.sessionKey, 0, SECURE_AES_KEY_SIZE);
    memset(pair.second.clientNonce, 0, 16);
  }
  sessions.clear();
  mbedtls_ecdh_free(&ecdh_context);
  mbedtls_ctr_drbg_free(&ctr_drbg);
  mbedtls_entropy_free(&entropy);
  initialized = false;
}

String SecureLayerManager::getServerPublicKey() {
  if (!initialized || serverPubKeyLen == 0)
    return "";
  return bytesToHex(serverPubKey, serverPubKeyLen);
}

bool SecureLayerManager::processKeyExchange(const String &clientId,
                                            const String &clientPubKeyHex,
                                            String &response) {
  if (!initialized) {
    response = "{\"type\":\"keyexchange\",\"status\":\"error\",\"message\":"
               "\"Server not initialized\"}";
    return false;
  }

  SecureSession *session = findSession(clientId);
  if (!session) {
    session = createSession(clientId);
    if (!session) {
      response = "{\"type\":\"keyexchange\",\"status\":\"error\",\"message\":"
                 "\"Session limit exceeded\"}";
      return false;
    }
  }

  uint8_t clientPubKey[65];
  if (!hexToBytes(clientPubKeyHex, clientPubKey, sizeof(clientPubKey))) {
    response = "{\"type\":\"keyexchange\",\"status\":\"error\",\"message\":"
               "\"Invalid public key format\"}";
    return false;
  }

  uint8_t sharedSecret[32];
  if (!performECDH(clientPubKey, 65, sharedSecret)) {
    response = "{\"type\":\"keyexchange\",\"status\":\"error\",\"message\":"
               "\"ECDH failed\"}";
    memset(sharedSecret, 0, sizeof(sharedSecret));
    return false;
  }

  if (!deriveSessionKey(sharedSecret, session->clientNonce,
                        session->sessionKey)) {
    response = "{\"type\":\"keyexchange\",\"status\":\"error\",\"message\":"
               "\"Key derivation failed\"}";
    memset(sharedSecret, 0, sizeof(sharedSecret));
    return false;
  }

  // Explicitly zero out the shared secret after session key derivation
  memset(sharedSecret, 0, sizeof(sharedSecret));

  session->keyExchanged = true;
  session->lastActivity = millis();
  session->rxCounter = 0;
  session->txCounter = 0;

  String serverPubKey = getServerPublicKey();
  String saltHex = bytesToHex(session->clientNonce, 16);
  response = "{\"type\":\"keyexchange\",\"status\":\"success\",\"pubkey\":\"" +
             serverPubKey + "\",\"salt\":\"" + saltHex + "\"}";

  memset(sharedSecret, 0, sizeof(sharedSecret));
  return true;
}

IRAM_ATTR bool SecureLayerManager::encryptResponse(const String &clientId,
                                                   const String &plaintext,
                                                   String &encryptedJson) {
  if (!initialized)
    return false;
  unsigned long randomDelay = 50 + (esp_random() % 150);
  delay(randomDelay);

  SecureSession *session = findSession(clientId);
  if (!session || !session->keyExchanged) {
    delay(100 + (esp_random() % 100));
    return false;
  }
  session->lastActivity = millis();

  size_t plaintextLen = plaintext.length();
  uint8_t *plaintextBytes = (uint8_t *)plaintext.c_str();
  uint8_t *ciphertext = new uint8_t[plaintextLen];
  uint8_t iv[SECURE_GCM_IV_SIZE];
  uint8_t tag[SECURE_GCM_TAG_SIZE];
  size_t ciphertextLen;

  generateNonce(iv, SECURE_GCM_IV_SIZE);
  bool success = encryptData(session->sessionKey, plaintextBytes, plaintextLen,
                             ciphertext, &ciphertextLen, iv, tag);

  if (success) {
    JsonDocument doc;
    doc["type"] = "secure";
    doc["counter"] = session->txCounter++;
    doc["data"] = bytesToHex(ciphertext, ciphertextLen);
    doc["iv"] = bytesToHex(iv, SECURE_GCM_IV_SIZE);
    doc["tag"] = bytesToHex(tag, SECURE_GCM_TAG_SIZE);
    serializeJson(doc, encryptedJson);
  }
  delete[] ciphertext;
  return success;
}

IRAM_ATTR bool SecureLayerManager::decryptRequest(const String &clientId,
                                                  const String &encryptedJson,
                                                  String &plaintext) {
  if (!initialized)
    return false;
  SecureSession *session = findSession(clientId);
  if (!session || !session->keyExchanged)
    return false;

  DynamicJsonDocument doc(2048);
  DeserializationError error = deserializeJson(doc, encryptedJson);
  if (error)
    return false;

  String dataHex = doc["data"];
  String ivHex = doc["iv"];
  String tagHex = doc["tag"];
  uint64_t counter = doc["counter"];

  if (dataHex.isEmpty() || ivHex.isEmpty() || tagHex.isEmpty())
    return false;
  if (counter <= session->rxCounter && session->rxCounter != 0)
    return false;
  session->rxCounter = counter;

  size_t dataLen = dataHex.length() / 2;
  uint8_t *ciphertext = new uint8_t[dataLen];
  uint8_t iv[SECURE_GCM_IV_SIZE];
  uint8_t tag[SECURE_GCM_TAG_SIZE];

  if (!hexToBytes(dataHex, ciphertext, dataLen) ||
      !hexToBytes(ivHex, iv, SECURE_GCM_IV_SIZE) ||
      !hexToBytes(tagHex, tag, SECURE_GCM_TAG_SIZE)) {
    delete[] ciphertext;
    return false;
  }

  uint8_t *decryptedBytes = new uint8_t[dataLen + 1];
  size_t decryptedLen = 0;

  bool success = decryptData(session->sessionKey, ciphertext, dataLen, iv, tag,
                             decryptedBytes, &decryptedLen);
  if (success) {
    decryptedBytes[decryptedLen] = '\0';
  } else {
    decryptedBytes[0] = '\0';
  }

  plaintext = String((char *)decryptedBytes);
  session->lastActivity = millis();
  delete[] ciphertext;
  delete[] decryptedBytes;
  return success;
}

bool SecureLayerManager::isSecureSessionValid(const String &clientId) {
  SecureSession *session = findSession(clientId);
  return session && session->keyExchanged;
}

bool SecureLayerManager::isSessionAuthenticated(const String &clientId) {
  SecureSession *session = findSession(clientId);
  return session && session->keyExchanged && session->isAuthenticated;
}

void SecureLayerManager::setSessionAuthenticated(const String &clientId,
                                                 bool auth) {
  SecureSession *session = findSession(clientId);
  if (session)
    session->isAuthenticated = auth;
}

void SecureLayerManager::invalidateSecureSession(const String &clientId) {
  removeSession(clientId);
}

String SecureLayerManager::wrapSecureResponse(const String &clientId,
                                              const String &originalResponse) {
  if (!isSecureSessionValid(clientId))
    return originalResponse;
  String encryptedResponse;
  if (encryptResponse(clientId, originalResponse, encryptedResponse)) {
    return encryptedResponse;
  }
  return originalResponse;
}

bool SecureLayerManager::unwrapSecureRequest(const String &clientId,
                                             const String &requestBody,
                                             String &unwrappedBody) {
  if (!isSecureSessionValid(clientId)) {
    unwrappedBody = requestBody;
    return true;
  }
  return decryptRequest(clientId, requestBody, unwrappedBody);
}

SecureLayerManager::SecureSession *
SecureLayerManager::findSession(const String &clientId) {
  auto it = sessions.find(clientId);
  return (it != sessions.end()) ? &it->second : nullptr;
}

SecureLayerManager::SecureSession *
SecureLayerManager::createSession(const String &clientId) {
  if (sessions.size() >= SECURE_MAX_SESSIONS) {
    auto oldest = sessions.begin();
    for (auto it = sessions.begin(); it != sessions.end(); ++it) {
      if (it->second.lastActivity < oldest->second.lastActivity) {
        oldest = it;
      }
    }
    sessions.erase(oldest);
  }

  SecureSession session;
  session.clientId = clientId;
  session.rxCounter = 0;
  session.txCounter = 0;
  session.keyExchanged = false;
  session.isAuthenticated = false;
  session.lastActivity = millis();
  generateNonce(session.clientNonce, 16);
  sessions[clientId] = session;
  return &sessions[clientId];
}

void SecureLayerManager::removeSession(const String &clientId) {
  auto it = sessions.find(clientId);
  if (it != sessions.end()) {
    memset(it->second.sessionKey, 0, SECURE_AES_KEY_SIZE);
    memset(it->second.clientNonce, 0, 16);
    sessions.erase(it);
  }
}

bool SecureLayerManager::performECDH(const uint8_t *clientPubKey, size_t keyLen,
                                     uint8_t *sharedSecret) {
  if (!initialized || keyLen != 65)
    return false;

  int ret = mbedtls_ecdh_read_public(&ecdh_context, clientPubKey, keyLen);
  if (ret != 0)
    return false;

  size_t olen = 0;
  ret = mbedtls_ecdh_calc_secret(&ecdh_context, &olen, sharedSecret, 32,
                                 mbedtls_ctr_drbg_random, &ctr_drbg);

  return ret == 0;
}

bool SecureLayerManager::deriveSessionKey(const uint8_t *sharedSecret,
                                          const uint8_t *salt,
                                          uint8_t *sessionKey) {
  mbedtls_md_context_t ctx;
  const mbedtls_md_info_t *md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (!md)
    return false;
  mbedtls_md_init(&ctx);

  uint8_t prk[32];
  int ret = mbedtls_md_setup(&ctx, md, 1);
  if (ret != 0) {
    mbedtls_md_free(&ctx);
    return false;
  }

  ret = mbedtls_md_hmac_starts(&ctx, salt, 16);
  if (ret == 0)
    ret = mbedtls_md_hmac_update(&ctx, sharedSecret, 32);
  if (ret == 0)
    ret = mbedtls_md_hmac_finish(&ctx, prk);
  if (ret != 0) {
    mbedtls_md_free(&ctx);
    return false;
  }

  const char *info = "SecureLayerV1";
  size_t info_len = 13;
  ret = mbedtls_md_hmac_starts(&ctx, prk, 32);
  if (ret == 0)
    ret = mbedtls_md_hmac_update(&ctx, (const uint8_t *)info, info_len);
  if (ret == 0) {
    uint8_t counter = 1;
    ret = mbedtls_md_hmac_update(&ctx, &counter, 1);
  }
  if (ret == 0)
    ret = mbedtls_md_hmac_finish(&ctx, sessionKey);
  mbedtls_md_free(&ctx);
  memset(prk, 0, sizeof(prk));
  return ret == 0;
}

bool SecureLayerManager::encryptData(const uint8_t *key,
                                     const uint8_t *plaintext,
                                     size_t plaintextLen, uint8_t *ciphertext,
                                     size_t *ciphertextLen, uint8_t *iv,
                                     uint8_t *tag) {
  mbedtls_gcm_context gcm;
  mbedtls_gcm_init(&gcm);
  int ret = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key,
                               SECURE_AES_KEY_SIZE * 8);
  if (ret != 0) {
    mbedtls_gcm_free(&gcm);
    return false;
  }

  generateNonce(iv, SECURE_GCM_IV_SIZE);
  ret = mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, plaintextLen, iv,
                                  SECURE_GCM_IV_SIZE, nullptr, 0, plaintext,
                                  ciphertext, SECURE_GCM_TAG_SIZE, tag);
  *ciphertextLen = plaintextLen;
  mbedtls_gcm_free(&gcm);
  return ret == 0;
}

bool SecureLayerManager::decryptData(const uint8_t *key,
                                     const uint8_t *ciphertext,
                                     size_t ciphertextLen, const uint8_t *iv,
                                     const uint8_t *tag, uint8_t *plaintext,
                                     size_t *plaintextLen) {
  mbedtls_gcm_context gcm;
  mbedtls_gcm_init(&gcm);
  int ret = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key,
                               SECURE_AES_KEY_SIZE * 8);
  if (ret != 0) {
    mbedtls_gcm_free(&gcm);
    return false;
  }

  ret = mbedtls_gcm_auth_decrypt(&gcm, ciphertextLen, iv, SECURE_GCM_IV_SIZE,
                                 nullptr, 0, tag, SECURE_GCM_TAG_SIZE,
                                 ciphertext, plaintext);
  *plaintextLen = ciphertextLen;
  mbedtls_gcm_free(&gcm);
  return ret == 0;
}

String SecureLayerManager::bytesToHex(const uint8_t *bytes, size_t length) {
  String hex = "";
  for (size_t i = 0; i < length; i++) {
    char hexByte[3];
    sprintf(hexByte, "%02x", bytes[i]);
    hex += hexByte;
  }
  return hex;
}

bool SecureLayerManager::hexToBytes(const String &hex, uint8_t *bytes,
                                    size_t maxLength) {
  if (hex.length() % 2 != 0 || hex.length() / 2 > maxLength)
    return false;
  size_t length = hex.length() / 2;
  for (size_t i = 0; i < length; i++) {
    String hexByte = hex.substring(i * 2, i * 2 + 2);
    bytes[i] = (uint8_t)strtol(hexByte.c_str(), nullptr, 16);
  }
  return true;
}

bool SecureLayerManager::generateNonce(uint8_t *nonce, size_t length) {
  return mbedtls_ctr_drbg_random(&ctr_drbg, nonce, length) == 0;
}
