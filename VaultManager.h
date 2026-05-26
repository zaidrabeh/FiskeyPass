// =============================================================================
// VaultManager.h — FiskeyPass v2.5.1 Encrypted Vault & Config Management
// =============================================================================
//
// Responsibilities:
//   - LittleFS vault load/save with AES-256-GCM encryption via Crypto.h
//   - Config file read/write (/config.json — unencrypted, stores PIN hash)
//   - CSV import parser (Name, Username, Password columns)
//   - KeePass XML import via tinyxml2 (already in project folder)
//   - ArduinoJson for vault JSON serialization
//   - First boot detection and PIN creation flow
// =============================================================================

#ifndef VAULT_MANAGER_H
#define VAULT_MANAGER_H

#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "Project_Config.h"
#include "Crypto.h"
#include "tinyxml2.h"

// ─────────────────────────────────────────────────────────────────────────────
// Credential Structure
// ─────────────────────────────────────────────────────────────────────────────

struct Credential {
  char name[CREDENTIAL_NAME_LEN];
  char user[CREDENTIAL_USER_LEN];
  char pass[CREDENTIAL_PASS_LEN];
};

// ─────────────────────────────────────────────────────────────────────────────
// Global Vault State
// ─────────────────────────────────────────────────────────────────────────────

static Credential vault[MAX_CREDENTIAL_ITEMS];
static int vaultCount = 0;

// ─────────────────────────────────────────────────────────────────────────────
// Config Structure (stored as /config.json, unencrypted)
// ─────────────────────────────────────────────────────────────────────────────

struct DeviceConfig {
  char pinHash[65];           // SHA-256 hex digest of the PIN
  bool displayTimeoutEnabled; // Screen timeout on/off
  bool firstBoot;             // True if no config exists yet
  char portalUser[32];        // Web portal username
  char portalPassHash[65];    // SHA-256 hex digest of web portal password
};

static DeviceConfig deviceConfig;

// ─────────────────────────────────────────────────────────────────────────────
// Filesystem Initialization
// ─────────────────────────────────────────────────────────────────────────────

static bool vaultFsInit() {
  if (!LittleFS.begin(true)) {  // true = format on first use
    Serial.println(F("[FS]   LittleFS mount FAILED"));
    return false;
  }
  Serial.println(F("[FS]   LittleFS mounted"));
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

  const char* ph = doc["pinHash"] | "";
  strlcpy(deviceConfig.pinHash, ph, sizeof(deviceConfig.pinHash));
  deviceConfig.displayTimeoutEnabled = doc["displayTimeout"] | true;
  deviceConfig.firstBoot = false;

  const char* pu = doc["portalUser"] | "";
  strlcpy(deviceConfig.portalUser, pu, sizeof(deviceConfig.portalUser));
  const char* pph = doc["portalPassHash"] | "";
  strlcpy(deviceConfig.portalPassHash, pph, sizeof(deviceConfig.portalPassHash));

  // If password hash is blank but user exists, clear user so setup triggers again
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
// Vault JSON Serialization (plaintext before encryption)
// Format: {"entries":[{"n":"Gmail","u":"user@gmail.com","p":"secret"}]}
// ─────────────────────────────────────────────────────────────────────────────

static String serializeVault() {
  DynamicJsonDocument doc(4096);
  JsonArray arr = doc.createNestedArray("entries");

  for (int i = 0; i < vaultCount; i++) {
    JsonObject entry = arr.createNestedObject();
    entry["n"] = vault[i].name;
    entry["u"] = vault[i].user;
    entry["p"] = vault[i].pass;
  }

  String json;
  serializeJson(doc, json);
  return json;
}

static bool deserializeVault(const char* json) {
  DynamicJsonDocument doc(4096);
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    Serial.print(F("[VLT]  JSON parse error: "));
    Serial.println(err.c_str());
    return false;
  }

  JsonArray arr = doc["entries"];
  vaultCount = 0;

  for (JsonObject entry : arr) {
    if (vaultCount >= MAX_CREDENTIAL_ITEMS) break;

    strlcpy(vault[vaultCount].name, entry["n"] | "", CREDENTIAL_NAME_LEN);
    strlcpy(vault[vaultCount].user, entry["u"] | "", CREDENTIAL_USER_LEN);
    strlcpy(vault[vaultCount].pass, entry["p"] | "", CREDENTIAL_PASS_LEN);
    vaultCount++;
  }

  Serial.printf("[VLT]  Deserialized %d entries\n", vaultCount);
  return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Encrypted Vault File Operations
// ─────────────────────────────────────────────────────────────────────────────

static bool saveVault(const char* pin) {
  if (!pin || strlen(pin) == 0) {
    Serial.println(F("[VLT]  ABORT: Empty PIN provided for save"));
    return false;
  }
  // Derive encryption key from PIN
  uint8_t key[AES_KEY_SIZE];
  if (!deriveKey(pin, key, AES_KEY_SIZE)) {
    Serial.println(F("[VLT]  Key derivation failed"));
    return false;
  }

  // Serialize vault to JSON
  String json = serializeVault();

  // Encrypt
  size_t encLen = 0;
  uint8_t* encrypted = encryptData(key, (const uint8_t*)json.c_str(),
                                    json.length(), &encLen);

  // Clear key from memory
  memset(key, 0, sizeof(key));

  if (!encrypted) {
    Serial.println(F("[VLT]  Encryption failed"));
    return false;
  }

  // Write to LittleFS
  fs::File f = LittleFS.open(VAULT_FILE_PATH, "w");
  if (!f) {
    free(encrypted);
    Serial.println(F("[VLT]  Failed to open vault file for writing"));
    return false;
  }

  size_t written = f.write(encrypted, encLen);
  f.close();
  free(encrypted);

  if (written != encLen) {
    Serial.println(F("[VLT]  Write incomplete"));
    return false;
  }

  Serial.printf("[VLT]  Vault saved (%d bytes encrypted)\n", encLen);
  return true;
}

static bool loadVault(const char* pin) {
  if (!LittleFS.exists(VAULT_FILE_PATH)) {
    Serial.println(F("[VLT]  No vault file — starting empty"));
    vaultCount = 0;
    return true;  // Not an error, just empty
  }

  fs::File f = LittleFS.open(VAULT_FILE_PATH, "r");
  if (!f) {
    Serial.println(F("[VLT]  Failed to open vault file"));
    return false;
  }

  size_t fileSize = f.size();
  uint8_t* encrypted = (uint8_t*)malloc(fileSize + 1);  // +1 for portal raw-JSON null terminator
  if (!encrypted) {
    f.close();
    Serial.println(F("[VLT]  Malloc failed for vault read"));
    return false;
  }

  f.read(encrypted, fileSize);
  f.close();


  // Derive key
  uint8_t key[AES_KEY_SIZE];
  if (!deriveKey(pin, key, AES_KEY_SIZE)) {
    free(encrypted);
    Serial.println(F("[VLT]  Key derivation failed"));
    return false;
  }

  // Decrypt
  size_t ptLen = 0;
  uint8_t* plaintext = decryptData(key, encrypted, fileSize, &ptLen);

  // Clear sensitive data
  memset(key, 0, sizeof(key));
  free(encrypted);

  if (!plaintext) {
    Serial.println(F("[VLT]  Decryption failed (wrong PIN or corrupted)"));
    return false;
  }

  // Parse JSON
  bool ok = deserializeVault((const char*)plaintext);
  free(plaintext);
  return ok;
}

// ─────────────────────────────────────────────────────────────────────────────
// CSV Import Parser
// Columns: Name, Username, Password  (extra columns beyond 3 are ignored)
// Handles: quoted fields, Windows \r\n line endings, extra columns (url/notes)
// ─────────────────────────────────────────────────────────────────────────────

// Parse a single CSV field starting at *src.
// Advances *src past the field and the following comma (if any).
// Returns pointer into buf (null-terminated), strips surrounding quotes.
static char* csvNextField(const char** src, const char* lineEnd,
                          char* buf, size_t bufLen) {
  const char* p = *src;
  size_t out = 0;

  if (p >= lineEnd) { buf[0] = '\0'; return buf; }

  if (*p == '"') {
    // Quoted field
    p++;  // skip opening quote
    while (p < lineEnd && out < bufLen - 1) {
      if (*p == '"') {
        p++;
        if (p < lineEnd && *p == '"') { buf[out++] = '"'; p++; } // escaped ""
        else break;  // closing quote
      } else {
        buf[out++] = *p++;
      }
    }
    // Advance to next comma or end
    while (p < lineEnd && *p != ',') p++;
    if (p < lineEnd && *p == ',') p++;
  } else {
    // Unquoted field — stop at next comma
    while (p < lineEnd && *p != ',' && out < bufLen - 1) {
      buf[out++] = *p++;
    }
    if (p < lineEnd && *p == ',') p++;
  }

  buf[out] = '\0';
  // Strip trailing \r
  while (out > 0 && buf[out - 1] == '\r') { buf[--out] = '\0'; }

  *src = p;
  return buf;
}

static int importCSV(const char* csvData, size_t dataLen) {
  int imported = 0;
  const char* ptr = csvData;
  const char* end = csvData + dataLen;
  bool firstLine = true;

  char fName[CREDENTIAL_NAME_LEN];
  char fUser[CREDENTIAL_USER_LEN];
  char fPass[CREDENTIAL_PASS_LEN];

  while (ptr < end && vaultCount < MAX_CREDENTIAL_ITEMS) {
    // Find end of line
    const char* lineEnd = ptr;
    while (lineEnd < end && *lineEnd != '\n' && *lineEnd != '\r') lineEnd++;

    size_t lineLen = lineEnd - ptr;

    if (lineLen > 0 && !firstLine) {
      const char* p = ptr;

      // Col 0 — Name
      csvNextField(&p, lineEnd, fName, sizeof(fName));
      // Col 1 — Username
      csvNextField(&p, lineEnd, fUser, sizeof(fUser));
      // Col 2 — Password (stop here; extra columns are discarded)
      csvNextField(&p, lineEnd, fPass, sizeof(fPass));
      // Any remaining columns (url, notes, etc.) are intentionally ignored

      if (strlen(fName) > 0 && strlen(fPass) > 0) {
        strlcpy(vault[vaultCount].name, fName, CREDENTIAL_NAME_LEN);
        strlcpy(vault[vaultCount].user, fUser, CREDENTIAL_USER_LEN);
        strlcpy(vault[vaultCount].pass, fPass, CREDENTIAL_PASS_LEN);
        vaultCount++;
        imported++;
      }
    }

    firstLine = false;

    // Advance past line ending(s)
    ptr = lineEnd;
    while (ptr < end && (*ptr == '\n' || *ptr == '\r')) ptr++;
  }

  Serial.printf("[IMP]  CSV: imported %d entries\n", imported);
  return imported;
}

// ─────────────────────────────────────────────────────────────────────────────
// KeePass XML Import
// Parses KeePass 2.x XML export format using tinyxml2
// ─────────────────────────────────────────────────────────────────────────────

static int importKeePassXML(const char* xmlData, size_t dataLen) {
  int imported = 0;

  tinyxml2::XMLDocument doc;
  tinyxml2::XMLError err = doc.Parse(xmlData, dataLen);
  if (err != tinyxml2::XML_SUCCESS) {
    Serial.printf("[IMP]  XML parse error: %d\n", err);
    return 0;
  }

  // KeePass XML structure:
  // <KeePassFile><Root><Group><Entry>
  //   <String><Key>Title</Key><Value>...</Value></String>
  //   <String><Key>UserName</Key><Value>...</Value></String>
  //   <String><Key>Password</Key><Value>...</Value></String>
  // </Entry></Group></Root></KeePassFile>

  tinyxml2::XMLElement* root = doc.FirstChildElement("KeePassFile");
  if (!root) root = doc.FirstChildElement();  // Fallback
  if (!root) return 0;

  // Recursively search for Entry elements
  // We use a simple iterative approach with a stack
  struct NodeStack {
    tinyxml2::XMLElement* elem;
  };

  // Find all <Entry> elements by traversing the tree
  // Simple recursive lambda isn't available, so we use iteration
  tinyxml2::XMLElement* searchRoot = root;

  // Try KeePass standard path first
  tinyxml2::XMLElement* kpRoot = root->FirstChildElement("Root");
  if (kpRoot) searchRoot = kpRoot;

  // Iterate through all Group elements and their Entry children
  // This handles nested groups
  for (tinyxml2::XMLElement* group = searchRoot->FirstChildElement();
       group != nullptr && vaultCount < MAX_CREDENTIAL_ITEMS;
       group = group->NextSiblingElement()) {

    // Process entries at this level
    tinyxml2::XMLElement* entry = nullptr;

    // Check if this element IS an Entry
    if (strcmp(group->Name(), "Entry") == 0) {
      entry = group;
    } else {
      // Look for Entry children within this Group
      entry = group->FirstChildElement("Entry");
    }

    while (entry != nullptr && vaultCount < MAX_CREDENTIAL_ITEMS) {
      char title[CREDENTIAL_NAME_LEN] = {0};
      char username[CREDENTIAL_USER_LEN] = {0};
      char password[CREDENTIAL_PASS_LEN] = {0};

      // Parse <String> elements within this Entry
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

      // Import if we have at least a title
      if (strlen(title) > 0) {
        strlcpy(vault[vaultCount].name, title, CREDENTIAL_NAME_LEN);
        strlcpy(vault[vaultCount].user, username, CREDENTIAL_USER_LEN);
        strlcpy(vault[vaultCount].pass, password, CREDENTIAL_PASS_LEN);
        vaultCount++;
        imported++;
      }

      entry = entry->NextSiblingElement("Entry");
    }

    // Also check for nested Group elements (one level deep)
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
          strlcpy(vault[vaultCount].name, title, CREDENTIAL_NAME_LEN);
          strlcpy(vault[vaultCount].user, username, CREDENTIAL_USER_LEN);
          strlcpy(vault[vaultCount].pass, password, CREDENTIAL_PASS_LEN);
          vaultCount++;
          imported++;
        }
      }
    }
  }

  Serial.printf("[IMP]  KeePass XML: imported %d entries\n", imported);
  return imported;
}

// ─────────────────────────────────────────────────────────────────────────────
// Vault Utility Functions
// ─────────────────────────────────────────────────────────────────────────────

static bool addCredential(const char* name, const char* user, const char* pass) {
  if (vaultCount >= MAX_CREDENTIAL_ITEMS) return false;

  strlcpy(vault[vaultCount].name, name, CREDENTIAL_NAME_LEN);
  strlcpy(vault[vaultCount].user, user, CREDENTIAL_USER_LEN);
  strlcpy(vault[vaultCount].pass, pass, CREDENTIAL_PASS_LEN);
  vaultCount++;
  return true;
}

static bool updateCredential(int index, const char* name, const char* user,
                              const char* pass) {
  if (index < 0 || index >= vaultCount) return false;

  if (name && strlen(name) > 0)
    strlcpy(vault[index].name, name, CREDENTIAL_NAME_LEN);
  if (user)
    strlcpy(vault[index].user, user, CREDENTIAL_USER_LEN);
  if (pass && strlen(pass) > 0)
    strlcpy(vault[index].pass, pass, CREDENTIAL_PASS_LEN);

  return true;
}

static bool deleteCredential(int index) {
  if (index < 0 || index >= vaultCount) return false;

  // Shift entries down
  for (int i = index; i < vaultCount - 1; i++) {
    memcpy(&vault[i], &vault[i + 1], sizeof(Credential));
  }
  vaultCount--;

  // Clear the now-unused slot
  memset(&vault[vaultCount], 0, sizeof(Credential));
  return true;
}

static void factoryReset() {
  LittleFS.remove(VAULT_FILE_PATH);
  LittleFS.remove(CONFIG_FILE_PATH);
  vaultCount = 0;
  memset(vault, 0, sizeof(vault));
  memset(&deviceConfig, 0, sizeof(deviceConfig));
  deviceConfig.firstBoot = true;
  deviceConfig.displayTimeoutEnabled = true;
  Serial.println(F("[RST]  Factory reset complete"));
}

#endif // VAULT_MANAGER_H
