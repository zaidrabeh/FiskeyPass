// =============================================================================
// VaultManager.h — FiskeyPass v4.0.0 Encrypted Vault & Config Management
// =============================================================================
//
// Responsibilities:
//   - LittleFS streamed/indexed vault with AES-256-GCM block encryption
//   - Config file read/write (/config.json — unencrypted, stores PIN hash)
//   - CSV & KeePass XML imports streamed directly to vault blocks
//
// Storage architecture:
//   /vault.dat  — fixed 188-byte encrypted blocks on LittleFS
//                 [12B IV][160B ciphertext][16B GCM tag]  per entry
//   RAM index   — CredentialIndex[] holds only name + file offset (36B each)
//   Passwords   — NEVER held in RAM; decrypted on demand into a temp buffer
// =============================================================================

#ifndef VAULT_MANAGER_H
#define VAULT_MANAGER_H

#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <esp_task_wdt.h>    // esp_task_wdt_reset() — prevents WDT on heavy I/O loops
#include "Project_Config.h"
#include "Crypto.h"
#include "tinyxml2.h"

// ─────────────────────────────────────────────────────────────────────────────
// Credential Structure (160 bytes — packed into one encrypted block)
// ─────────────────────────────────────────────────────────────────────────────

struct Credential {
  char name[CREDENTIAL_NAME_LEN];   // 32
  char user[CREDENTIAL_USER_LEN];   // 64
  char pass[CREDENTIAL_PASS_LEN];   // 64
};

// ─────────────────────────────────────────────────────────────────────────────
// Lightweight RAM Index — only names + seek offsets (36 bytes per entry)
// ─────────────────────────────────────────────────────────────────────────────

struct CredentialIndex {
  char name[CREDENTIAL_NAME_LEN];   // 32
  uint32_t fileOffset;              //  4
};

// ─────────────────────────────────────────────────────────────────────────────
// Global Vault State
// ─────────────────────────────────────────────────────────────────────────────

static CredentialIndex vaultIndex[MAX_CREDENTIAL_ITEMS];
static int vaultCount = 0;

extern char sessionPin[PIN_LENGTH + 1]; // defined in FiskeyPass.ino

// ─────────────────────────────────────────────────────────────────────────────
// Config Structure (stored as /config.json on LittleFS, unencrypted)
// ─────────────────────────────────────────────────────────────────────────────

struct DeviceConfig {
  char pinHash[65];
  bool displayTimeoutEnabled;
  bool firstBoot;
  char portalUser[32];
  char portalPassHash[65];
};

static DeviceConfig deviceConfig;

// ─────────────────────────────────────────────────────────────────────────────
// Block size constant: IV(12) + Credential(160) + Tag(16) = 188 bytes
// ─────────────────────────────────────────────────────────────────────────────

static const size_t BLOCK_SIZE = GCM_IV_SIZE + sizeof(Credential) + GCM_TAG_SIZE;

// ─────────────────────────────────────────────────────────────────────────────
// Robust LittleFS Initialization (handles raw/unformatted partitions)
// ─────────────────────────────────────────────────────────────────────────────

static bool initLittleFS() {
  // Step 1: try a normal mount (no auto-format)
  if (LittleFS.begin(false)) {
    Serial.println(F("[FS]   LittleFS mounted OK"));
    return true;
  }

  // Step 2: mount failed — partition is likely raw (e.g. after switching
  // to "Huge APP" partition scheme).  Explicitly format it.
  Serial.println(F("[FS]   LittleFS mount failed — partition may be raw"));
  Serial.println(F("[FS]   Formatting LittleFS..."));

  if (!LittleFS.format()) {
    Serial.println(F("[FS]   LittleFS format FAILED"));
    return false;
  }
  Serial.println(F("[FS]   LittleFS format OK"));

  // Step 3: mount the freshly formatted partition
  if (!LittleFS.begin(false)) {
    Serial.println(F("[FS]   LittleFS mount after format FAILED"));
    return false;
  }

  Serial.println(F("[FS]   LittleFS mounted (fresh format)"));
  return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Config File Operations
// ─────────────────────────────────────────────────────────────────────────────

static bool loadConfig() {
  deviceConfig.displayTimeoutEnabled = true;
  deviceConfig.firstBoot = true;
  memset(deviceConfig.pinHash, 0, sizeof(deviceConfig.pinHash));
  memset(deviceConfig.portalUser, 0, sizeof(deviceConfig.portalUser));
  memset(deviceConfig.portalPassHash, 0, sizeof(deviceConfig.portalPassHash));

  if (!LittleFS.exists(CONFIG_FILE_PATH)) {
    Serial.println(F("[CFG]  No config.json — first boot"));
    return false;
  }

  fs::File f = LittleFS.open(CONFIG_FILE_PATH, "r");
  if (!f) {
    Serial.println(F("[CFG]  Failed to open config.json"));
    return false;
  }

  DynamicJsonDocument doc(512);
  DeserializationError err = deserializeJson(doc, f);
  f.close();

  if (err) {
    Serial.print(F("[CFG]  JSON parse error: "));
    Serial.println(err.c_str());
    return false;
  }

  strlcpy(deviceConfig.pinHash, doc["pinHash"] | "", sizeof(deviceConfig.pinHash));
  deviceConfig.displayTimeoutEnabled = doc["displayTimeout"] | true;
  deviceConfig.firstBoot = false;
  strlcpy(deviceConfig.portalUser, doc["portalUser"] | "", sizeof(deviceConfig.portalUser));
  strlcpy(deviceConfig.portalPassHash, doc["portalPassHash"] | "", sizeof(deviceConfig.portalPassHash));

  if (strlen(deviceConfig.portalUser) > 0 && strlen(deviceConfig.portalPassHash) == 0) {
    memset(deviceConfig.portalUser, 0, sizeof(deviceConfig.portalUser));
  }

  Serial.println(F("[CFG]  Config loaded"));
  return true;
}

static bool saveConfig() {
  DynamicJsonDocument doc(512);
  doc["pinHash"] = deviceConfig.pinHash;
  doc["displayTimeout"] = deviceConfig.displayTimeoutEnabled;
  doc["portalUser"] = deviceConfig.portalUser;
  doc["portalPassHash"] = deviceConfig.portalPassHash;

  fs::File f = LittleFS.open(CONFIG_FILE_PATH, "w");
  if (!f) {
    Serial.println(F("[CFG]  Failed to write config.json"));
    return false;
  }
  serializeJson(doc, f);
  f.close();
  Serial.println(F("[CFG]  Config saved"));
  return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// PIN Verification
// ─────────────────────────────────────────────────────────────────────────────

static bool verifyPin(const char* pin) {
  char hash[65];
  if (!hashPinSHA256(pin, hash, sizeof(hash))) return false;
  return (strcmp(hash, deviceConfig.pinHash) == 0);
}

static bool setNewPin(const char* pin) {
  if (!hashPinSHA256(pin, deviceConfig.pinHash, sizeof(deviceConfig.pinHash))) {
    return false;
  }
  deviceConfig.firstBoot = false;
  return saveConfig();
}

// ─────────────────────────────────────────────────────────────────────────────
// On-Demand Decryption — read ONE block from /vault.dat, decrypt, return
// ─────────────────────────────────────────────────────────────────────────────

static bool decryptEntry(int index, const char* pin, Credential* outCred) {
  if (index < 0 || index >= vaultCount) return false;

  fs::File f = LittleFS.open(VAULT_FILE_PATH, "r");
  if (!f) return false;

  f.seek(vaultIndex[index].fileOffset);
  uint8_t buf[BLOCK_SIZE];
  if (f.read(buf, BLOCK_SIZE) != BLOCK_SIZE) {
    f.close();
    return false;
  }
  f.close();

  uint8_t key[AES_KEY_SIZE];
  if (!deriveKey(pin, key, AES_KEY_SIZE)) return false;

  size_t ptLen = 0;
  uint8_t* plaintext = decryptData(key, buf, BLOCK_SIZE, &ptLen);
  memset(key, 0, sizeof(key));

  if (plaintext && ptLen == sizeof(Credential)) {
    memcpy(outCred, plaintext, sizeof(Credential));
    free(plaintext);
    return true;
  }
  if (plaintext) free(plaintext);
  return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Append a single encrypted block to /vault.dat
// ─────────────────────────────────────────────────────────────────────────────

static bool appendCredentialBlock(const Credential* cred, const char* pin) {
  if (vaultCount >= MAX_CREDENTIAL_ITEMS) return false;

  uint8_t key[AES_KEY_SIZE];
  if (!deriveKey(pin, key, AES_KEY_SIZE)) return false;

  size_t encLen = 0;
  uint8_t* encrypted = encryptData(key, (const uint8_t*)cred, sizeof(Credential), &encLen);
  memset(key, 0, sizeof(key));

  if (!encrypted) return false;

  fs::File f = LittleFS.open(VAULT_FILE_PATH, "a");   // append mode
  if (!f) {
    free(encrypted);
    return false;
  }

  uint32_t offset = f.size();
  size_t written = f.write(encrypted, encLen);
  f.close();
  free(encrypted);

  if (written == encLen) {
    strlcpy(vaultIndex[vaultCount].name, cred->name, CREDENTIAL_NAME_LEN);
    vaultIndex[vaultCount].fileOffset = offset;
    vaultCount++;
    return true;
  }
  return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Load vault — stream through /vault.dat, extract names into RAM index
// ─────────────────────────────────────────────────────────────────────────────

static bool loadVault(const char* pin) {
  vaultCount = 0;

  if (!LittleFS.exists(VAULT_FILE_PATH)) {
    Serial.println(F("[VLT]  No vault file — starting empty"));
    return true;  // not an error, just empty
  }

  fs::File f = LittleFS.open(VAULT_FILE_PATH, "r");
  if (!f) {
    Serial.println(F("[VLT]  Failed to open vault file"));
    return false;
  }

  uint8_t key[AES_KEY_SIZE];
  if (!deriveKey(pin, key, AES_KEY_SIZE)) {
    f.close();
    Serial.println(F("[VLT]  Key derivation failed"));
    return false;
  }

  uint8_t buf[BLOCK_SIZE];
  while (f.available() && vaultCount < MAX_CREDENTIAL_ITEMS) {
    uint32_t offset = f.position();
    size_t readLen = f.read(buf, BLOCK_SIZE);
    if (readLen != BLOCK_SIZE) break;   // partial block = EOF or corruption

    size_t ptLen = 0;
    uint8_t* plaintext = decryptData(key, buf, BLOCK_SIZE, &ptLen);
    if (plaintext && ptLen == sizeof(Credential)) {
      Credential* cred = (Credential*)plaintext;
      strlcpy(vaultIndex[vaultCount].name, cred->name, CREDENTIAL_NAME_LEN);
      vaultIndex[vaultCount].fileOffset = offset;
      vaultCount++;
    }
    if (plaintext) free(plaintext);
  }

  memset(key, 0, sizeof(key));
  f.close();
  Serial.printf("[VLT]  Loaded %d entries from LittleFS\n", vaultCount);
  return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Save vault (re-encrypt all blocks) — used when PIN changes
// Writes to /vault.tmp then renames over /vault.dat for crash safety
// ─────────────────────────────────────────────────────────────────────────────

static bool saveVault(const char* newPin) {
  fs::File newF = LittleFS.open("/vault.tmp", "w");
  if (!newF) return false;

  uint8_t newKey[AES_KEY_SIZE];
  if (!deriveKey(newPin, newKey, AES_KEY_SIZE)) {
    newF.close();
    return false;
  }

  Credential tempCred;
  bool success = true;

  for (int i = 0; i < vaultCount; i++) {
    if (!decryptEntry(i, sessionPin, &tempCred)) { success = false; break; }

    size_t encLen = 0;
    uint8_t* encrypted = encryptData(newKey, (const uint8_t*)&tempCred, sizeof(Credential), &encLen);
    if (encrypted) {
      uint32_t newOffset = newF.size();
      newF.write(encrypted, encLen);
      vaultIndex[i].fileOffset = newOffset;
      free(encrypted);
    } else {
      success = false;
      break;
    }
    memset(&tempCred, 0, sizeof(Credential));
  }

  memset(newKey, 0, sizeof(newKey));
  newF.close();

  if (success) {
    LittleFS.remove(VAULT_FILE_PATH);
    LittleFS.rename("/vault.tmp", VAULT_FILE_PATH);
    return true;
  }
  LittleFS.remove("/vault.tmp");
  return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// CRUD helpers
// ─────────────────────────────────────────────────────────────────────────────

static bool addCredential(const char* name, const char* user, const char* pass, const char* pin) {
  Credential cred;
  memset(&cred, 0, sizeof(Credential));
  strlcpy(cred.name, name, CREDENTIAL_NAME_LEN);
  strlcpy(cred.user, user, CREDENTIAL_USER_LEN);
  strlcpy(cred.pass, pass, CREDENTIAL_PASS_LEN);
  return appendCredentialBlock(&cred, pin);
}

static bool updateCredential(int index, const char* name, const char* user, const char* pass, const char* pin) {
  if (index < 0 || index >= vaultCount) return false;

  fs::File newF = LittleFS.open("/vault.tmp", "w");
  if (!newF) return false;

  uint8_t key[AES_KEY_SIZE];
  if (!deriveKey(pin, key, AES_KEY_SIZE)) {
    newF.close();
    return false;
  }

  Credential tempCred;
  bool success = true;

  for (int i = 0; i < vaultCount; i++) {
    if (!decryptEntry(i, pin, &tempCred)) { success = false; break; }

    if (i == index) {
      if (name && strlen(name) > 0) strlcpy(tempCred.name, name, CREDENTIAL_NAME_LEN);
      if (user) strlcpy(tempCred.user, user, CREDENTIAL_USER_LEN);
      if (pass && strlen(pass) > 0) strlcpy(tempCred.pass, pass, CREDENTIAL_PASS_LEN);
      strlcpy(vaultIndex[i].name, tempCred.name, CREDENTIAL_NAME_LEN);
    }

    size_t encLen = 0;
    uint8_t* encrypted = encryptData(key, (const uint8_t*)&tempCred, sizeof(Credential), &encLen);
    if (encrypted) {
      uint32_t newOffset = newF.size();
      newF.write(encrypted, encLen);
      vaultIndex[i].fileOffset = newOffset;
      free(encrypted);
    } else {
      success = false;
      break;
    }
    memset(&tempCred, 0, sizeof(Credential));
  }

  memset(key, 0, sizeof(key));
  newF.close();

  if (success) {
    LittleFS.remove(VAULT_FILE_PATH);
    LittleFS.rename("/vault.tmp", VAULT_FILE_PATH);
    return true;
  }
  LittleFS.remove("/vault.tmp");
  return false;
}

static bool deleteCredential(int index, const char* pin) {
  if (index < 0 || index >= vaultCount) return false;

  fs::File newF = LittleFS.open("/vault.tmp", "w");
  if (!newF) return false;

  uint8_t key[AES_KEY_SIZE];
  if (!deriveKey(pin, key, AES_KEY_SIZE)) {
    newF.close();
    return false;
  }

  Credential tempCred;
  bool success = true;
  int newCount = 0;

  for (int i = 0; i < vaultCount; i++) {
    if (i == index) continue;   // skip deleted entry

    if (!decryptEntry(i, pin, &tempCred)) { success = false; break; }

    size_t encLen = 0;
    uint8_t* encrypted = encryptData(key, (const uint8_t*)&tempCred, sizeof(Credential), &encLen);
    if (encrypted) {
      uint32_t newOffset = newF.size();
      newF.write(encrypted, encLen);
      strlcpy(vaultIndex[newCount].name, tempCred.name, CREDENTIAL_NAME_LEN);
      vaultIndex[newCount].fileOffset = newOffset;
      newCount++;
      free(encrypted);
    } else {
      success = false;
      break;
    }
    memset(&tempCred, 0, sizeof(Credential));
  }

  memset(key, 0, sizeof(key));
  newF.close();

  if (success) {
    LittleFS.remove(VAULT_FILE_PATH);
    LittleFS.rename("/vault.tmp", VAULT_FILE_PATH);
    vaultCount = newCount;
    return true;
  }
  LittleFS.remove("/vault.tmp");
  return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// CSV Import Parser
// ─────────────────────────────────────────────────────────────────────────────

static char* csvNextField(const char** src, const char* lineEnd,
                          char* buf, size_t bufLen) {
  const char* p = *src;
  size_t out = 0;

  if (p >= lineEnd) { buf[0] = '\0'; return buf; }

  if (*p == '"') {
    p++;
    while (p < lineEnd && out < bufLen - 1) {
      if (*p == '"') {
        p++;
        if (p < lineEnd && *p == '"') { buf[out++] = '"'; p++; }
        else break;
      } else {
        buf[out++] = *p++;
      }
    }
    while (p < lineEnd && *p != ',') p++;
    if (p < lineEnd && *p == ',') p++;
  } else {
    while (p < lineEnd && *p != ',' && out < bufLen - 1) {
      buf[out++] = *p++;
    }
    if (p < lineEnd && *p == ',') p++;
  }

  buf[out] = '\0';
  while (out > 0 && buf[out - 1] == '\r') { buf[--out] = '\0'; }

  *src = p;
  return buf;
}

static int importCSV(const char* csvData, size_t dataLen, const char* pin) {
  int imported = 0;
  const char* ptr = csvData;
  const char* end = csvData + dataLen;
  bool firstLine = true;

  char fName[CREDENTIAL_NAME_LEN];
  char fUser[CREDENTIAL_USER_LEN];
  char fPass[CREDENTIAL_PASS_LEN];

  while (ptr < end && vaultCount < MAX_CREDENTIAL_ITEMS) {
    const char* lineEnd = ptr;
    while (lineEnd < end && *lineEnd != '\n' && *lineEnd != '\r') lineEnd++;
    size_t lineLen = lineEnd - ptr;

    if (lineLen > 0 && !firstLine) {
      const char* p = ptr;
      csvNextField(&p, lineEnd, fName, sizeof(fName));
      csvNextField(&p, lineEnd, fUser, sizeof(fUser));
      csvNextField(&p, lineEnd, fPass, sizeof(fPass));

      if (strlen(fName) > 0 && strlen(fPass) > 0) {
        if (addCredential(fName, fUser, fPass, pin)) {
          imported++;
        }
      }
      // Yield to FreeRTOS scheduler and reset TWDT to prevent WDT on bulk imports
      yield();
      esp_task_wdt_reset();
    }

    firstLine = false;
    ptr = lineEnd;
    while (ptr < end && (*ptr == '\n' || *ptr == '\r')) ptr++;
  }

  Serial.printf("[IMP]  CSV: imported %d entries\n", imported);
  return imported;
}

// ─────────────────────────────────────────────────────────────────────────────
// KeePass XML Import
// ─────────────────────────────────────────────────────────────────────────────

static int importKeePassXML(const char* xmlData, size_t dataLen, const char* pin) {
  int imported = 0;

  tinyxml2::XMLDocument doc;
  tinyxml2::XMLError err = doc.Parse(xmlData, dataLen);
  if (err != tinyxml2::XML_SUCCESS) {
    Serial.printf("[IMP]  XML parse error: %d\n", err);
    return 0;
  }

  tinyxml2::XMLElement* root = doc.FirstChildElement("KeePassFile");
  if (!root) root = doc.FirstChildElement();
  if (!root) return 0;

  tinyxml2::XMLElement* searchRoot = root;
  tinyxml2::XMLElement* kpRoot = root->FirstChildElement("Root");
  if (kpRoot) searchRoot = kpRoot;

  for (tinyxml2::XMLElement* group = searchRoot->FirstChildElement();
       group != nullptr && vaultCount < MAX_CREDENTIAL_ITEMS;
       group = group->NextSiblingElement()) {

    tinyxml2::XMLElement* entry = nullptr;
    if (strcmp(group->Name(), "Entry") == 0) {
      entry = group;
    } else {
      entry = group->FirstChildElement("Entry");
    }

    while (entry != nullptr && vaultCount < MAX_CREDENTIAL_ITEMS) {
      char title[CREDENTIAL_NAME_LEN] = {0};
      char username[CREDENTIAL_USER_LEN] = {0};
      char password[CREDENTIAL_PASS_LEN] = {0};

      for (tinyxml2::XMLElement* str = entry->FirstChildElement("String");
           str != nullptr;
           str = str->NextSiblingElement("String")) {

        tinyxml2::XMLElement* keyElem = str->FirstChildElement("Key");
        tinyxml2::XMLElement* valElem = str->FirstChildElement("Value");

        if (keyElem && valElem && keyElem->GetText()) {
          const char* key = keyElem->GetText();
          const char* val = valElem->GetText();
          if (!val) val = "";

          if (strcmp(key, "Title") == 0) {
            strlcpy(title, val, CREDENTIAL_NAME_LEN);
          } else if (strcmp(key, "UserName") == 0) {
            strlcpy(username, val, CREDENTIAL_USER_LEN);
          } else if (strcmp(key, "Password") == 0) {
            strlcpy(password, val, CREDENTIAL_PASS_LEN);
          }
        }
      }

      if (strlen(title) > 0) {
        if (addCredential(title, username, password, pin)) {
          imported++;
        }
      }
      // Yield to FreeRTOS scheduler and reset TWDT
      yield();
      esp_task_wdt_reset();

      entry = entry->NextSiblingElement("Entry");
    }

    // Check nested groups (one level deep)
    for (tinyxml2::XMLElement* subGroup = group->FirstChildElement("Group");
         subGroup != nullptr && vaultCount < MAX_CREDENTIAL_ITEMS;
         subGroup = subGroup->NextSiblingElement("Group")) {

      for (tinyxml2::XMLElement* subEntry = subGroup->FirstChildElement("Entry");
           subEntry != nullptr && vaultCount < MAX_CREDENTIAL_ITEMS;
           subEntry = subEntry->NextSiblingElement("Entry")) {

        char title[CREDENTIAL_NAME_LEN] = {0};
        char username[CREDENTIAL_USER_LEN] = {0};
        char password[CREDENTIAL_PASS_LEN] = {0};

        for (tinyxml2::XMLElement* str = subEntry->FirstChildElement("String");
             str != nullptr;
             str = str->NextSiblingElement("String")) {

          tinyxml2::XMLElement* keyElem = str->FirstChildElement("Key");
          tinyxml2::XMLElement* valElem = str->FirstChildElement("Value");

          if (keyElem && valElem && keyElem->GetText()) {
            const char* key = keyElem->GetText();
            const char* val = valElem->GetText();
            if (!val) val = "";

            if (strcmp(key, "Title") == 0) {
              strlcpy(title, val, CREDENTIAL_NAME_LEN);
            } else if (strcmp(key, "UserName") == 0) {
              strlcpy(username, val, CREDENTIAL_USER_LEN);
            } else if (strcmp(key, "Password") == 0) {
              strlcpy(password, val, CREDENTIAL_PASS_LEN);
            }
          }
        }

        if (strlen(title) > 0) {
          if (addCredential(title, username, password, pin)) {
            imported++;
          }
        }
        // Yield to FreeRTOS scheduler and reset TWDT
        yield();
        esp_task_wdt_reset();
      }
    }
  }

  Serial.printf("[IMP]  KeePass XML: imported %d entries\n", imported);
  return imported;
}

// ─────────────────────────────────────────────────────────────────────────────
// Factory Reset
// ─────────────────────────────────────────────────────────────────────────────

static void factoryReset() {
  LittleFS.remove(VAULT_FILE_PATH);
  LittleFS.remove(CONFIG_FILE_PATH);
  vaultCount = 0;
  memset(vaultIndex, 0, sizeof(vaultIndex));
  memset(&deviceConfig, 0, sizeof(deviceConfig));
  deviceConfig.firstBoot = true;
  deviceConfig.displayTimeoutEnabled = true;
  Serial.println(F("[RST]  Factory reset complete"));
}

#endif // VAULT_MANAGER_H
