#ifndef TRAFFIC_OBFUSCATION_MANAGER_H
#define TRAFFIC_OBFUSCATION_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <vector>

class TrafficObfuscationManager {
public:
    static TrafficObfuscationManager& getInstance();
    
    bool begin();
    void end();
    void update();
    
    void generateDecoyTraffic();
    bool generateRandomHttpRequests();

private:
    TrafficObfuscationManager();
    ~TrafficObfuscationManager();
    TrafficObfuscationManager(const TrafficObfuscationManager&) = delete;
    TrafficObfuscationManager& operator=(const TrafficObfuscationManager&) = delete;
    
    bool sendDecoyHttpRequest(const String& endpoint, const String& method = "GET");
    
    bool initialized;
    unsigned long lastDecoyTraffic;
    unsigned long decoyInterval;
    
    static const char* decoyEndpoints[];
    static const char* decoyUserAgents[];
};

#endif // TRAFFIC_OBFUSCATION_MANAGER_H
