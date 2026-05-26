#include "TrafficObfuscationManager.h"

const char* TrafficObfuscationManager::decoyEndpoints[] = {
    "/api/status", "/api/health", "/api/version", "/api/metrics",
    "/api/ping", "/api/config/general", "/api/system/info",
    "/favicon.ico", "/robots.txt", nullptr
};

const char* TrafficObfuscationManager::decoyUserAgents[] = {
    "Mozilla/5.0 (compatible; ESP32-Monitor)",
    "ESP32-Status-Checker/1.0", 
    "IoT-Device-Health/2.1",
    "ESP32-Metrics-Collector",
    "System-Monitor-Bot",
    nullptr
};

TrafficObfuscationManager& TrafficObfuscationManager::getInstance() {
    static TrafficObfuscationManager instance;
    return instance;
}

TrafficObfuscationManager::TrafficObfuscationManager() 
    : initialized(false), lastDecoyTraffic(0), decoyInterval(30000) {
}

TrafficObfuscationManager::~TrafficObfuscationManager() {
    end();
}

bool TrafficObfuscationManager::begin() {
    if (initialized) return true;
    decoyInterval = 20000 + (esp_random() % 40000);
    lastDecoyTraffic = millis();
    initialized = true;
    return true;
}

void TrafficObfuscationManager::end() {
    initialized = false;
}

void TrafficObfuscationManager::update() {
    if (!initialized) return;
    unsigned long now = millis();
    if (now - lastDecoyTraffic >= decoyInterval) {
        generateDecoyTraffic();
        lastDecoyTraffic = now;
        decoyInterval = 15000 + (esp_random() % 45000); // 15-60 seconds
    }
}

void TrafficObfuscationManager::generateDecoyTraffic() {
    if (!WiFi.isConnected() || WiFi.status() != WL_CONNECTED) return;
    int requestCount = 2 + (esp_random() % 3);
    for (int i = 0; i < requestCount; i++) {
        generateRandomHttpRequests();
        delay(200 + (esp_random() % 800));
    }
}

bool TrafficObfuscationManager::generateRandomHttpRequests() {
    int endpointIndex = esp_random() % 9;
    String endpoint = String(decoyEndpoints[endpointIndex]);
    String method = ((esp_random() % 100) < 20) ? "POST" : "GET";
    return sendDecoyHttpRequest(endpoint, method);
}

bool TrafficObfuscationManager::sendDecoyHttpRequest(const String& endpoint, const String& method) {
    if (!WiFi.isConnected() || WiFi.status() != WL_CONNECTED) return false;
    
    static char packet[256];
    WiFiUDP udp;
    IPAddress fakeIP;
    uint32_t choice = esp_random() % 100;
    
    if (choice < 30) {
        const IPAddress publicDNS[] = { IPAddress(8,8,8,8), IPAddress(8,8,4,4), IPAddress(1,1,1,1), IPAddress(1,0,0,1), IPAddress(9,9,9,9) };
        fakeIP = publicDNS[esp_random() % 5];
    } else if (choice < 60) {
        fakeIP = IPAddress(192, 168, 0, 1 + (esp_random() % 254));
    } else {
        fakeIP = IPAddress(192, 168, 1 + (esp_random() % 255), 1 + (esp_random() % 254));
    }
    
    int agentIndex = esp_random() % 5;
    int len = snprintf(packet, sizeof(packet),
        "%s %s HTTP/1.1\r\n"
        "Host: %d.%d.%d.%d\r\n"
        "User-Agent: %s\r\n"
        "Accept: application/json\r\n"
        "Connection: close\r\n\r\n",
        method.c_str(), endpoint.c_str(),
        fakeIP[0], fakeIP[1], fakeIP[2], fakeIP[3],
        decoyUserAgents[agentIndex]
    );
    
    if (len < 0 || len >= (int)sizeof(packet)) return false;
    
    if (udp.beginPacket(fakeIP, 80)) {
        udp.write((const uint8_t*)packet, len);
        return udp.endPacket();
    }
    return false;
}
