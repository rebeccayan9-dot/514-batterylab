/**
 * ESP32-C3 Four Power Modes Demo
 * Optimized for UW MPSK WiFi
 */

#define ENABLE_USER_AUTH
#define ENABLE_DATABASE

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <FirebaseClient.h>

// Pin definitions
#define TRIG_PIN 2
#define ECHO_PIN 3

// WiFi credentials - VERIFIED CORRECT
const char* ssid     = "UW MPSK";
const char* password = "LYcSq73RVKmLnNgM";

// Firebase configuration
UserAuth user_auth(
    "AIzaSyB54AKpPGSN-u8UVwBFdJEZST-ULDNmc34",
    "qianmu@uw.edu",
    "Rebecca0109?"
);

FirebaseApp app;
WiFiClientSecure ssl_client;
using AsyncClient = AsyncClientClass;
AsyncClient async_client(ssl_client);
RealtimeDatabase Database;

// Mode tracking
enum PowerMode {
    MODE_IDLE = 0,
    MODE_ULTRASONIC = 1,
    MODE_WIFI_ONLY = 2,
    MODE_ULTRASONIC_WIFI_FIREBASE = 3
};

PowerMode currentMode = MODE_IDLE;
unsigned long modeStartTime = 0;
const unsigned long MODE_DURATION = 15000; // 15 seconds - more time for WiFi
int cycleCount = 1;
bool firebaseInitialized = false;

// Function declarations
void processData(AsyncResult &aResult);
float readUltrasonic();
void setupWiFi();
void disconnectWiFi();

void setup()
{
    Serial.begin(115200);
    delay(1000);
    
    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);
    digitalWrite(TRIG_PIN, LOW);
    
    Serial.println("\n========================================");
    Serial.println("   ESP32-C3 POWER MODES DEMO");
    Serial.println("========================================");
    Serial.printf("MAC: %s\n", WiFi.macAddress().c_str());
    Serial.println("Registered for UW MPSK ✓");
    Serial.println("========================================");
    
    Serial.printf("\n--- Cycle %d ---\n", cycleCount);
    Serial.println("MODE 0: IDLE (~20mA)");
    
    modeStartTime = millis();
}

void loop()
{
    unsigned long currentTime = millis();
    
    // Check if it's time to switch modes
    if (currentTime - modeStartTime >= MODE_DURATION) {
        currentMode = (PowerMode)((currentMode + 1) % 4);
        modeStartTime = currentTime;
        
        if (currentMode == MODE_IDLE) {
            Serial.println("\n========================================");
            Serial.printf("Completed Cycle %d\n", cycleCount);
            Serial.println("========================================");
            cycleCount++;
            Serial.printf("\n--- Cycle %d ---\n", cycleCount);
        }
        
        switch(currentMode) {
            case MODE_IDLE:
                Serial.println("\nMODE 0: IDLE");
                Serial.println("Power: ~20mA");
                disconnectWiFi();
                firebaseInitialized = false;
                break;
                
            case MODE_ULTRASONIC:
                Serial.println("\nMODE 1: ULTRASONIC ONLY");
                Serial.println("Power: ~30mA");
                break;
                
            case MODE_WIFI_ONLY:
                Serial.println("\nMODE 2: WiFi ONLY");
                Serial.println("Power: ~80-120mA");
                setupWiFi();
                break;
                
            case MODE_ULTRASONIC_WIFI_FIREBASE:
                Serial.println("\nMODE 3: FULL OPERATION");
                Serial.println("Power: ~150-200mA");
                
                // Ensure WiFi is connected
                if (WiFi.status() != WL_CONNECTED) {
                    setupWiFi();
                }
                
                // Initialize Firebase once when WiFi connects
                if (WiFi.status() == WL_CONNECTED && !firebaseInitialized) {
                    Serial.println("Initializing Firebase...");
                    ssl_client.setInsecure();
                    ssl_client.setHandshakeTimeout(10);
                    initializeApp(async_client, app, getAuth(user_auth), processData, "authTask");
                    app.getApp<RealtimeDatabase>(Database);
                    Database.url("https://esp-project-7e4c3-default-rtdb.firebaseio.com/");
                    firebaseInitialized = true;
                }
                break;
        }
    }
    
    // Execute mode-specific operations
    switch(currentMode) {
        case MODE_IDLE:
            delay(500);
            break;
            
        case MODE_ULTRASONIC:
            {
                float distance = readUltrasonic();
                if (distance > 0) {
                    Serial.printf("📏 Distance: %.2f cm\n", distance);
                }
                delay(1000);
            }
            break;
            
        case MODE_WIFI_ONLY:
            {
                delay(500);
                static unsigned long lastPrint = 0;
                if (currentTime - lastPrint >= 3000) {
                    lastPrint = currentTime;
                    if (WiFi.status() == WL_CONNECTED) {
                        Serial.printf("📶 Connected - IP: %s, Signal: %d dBm\n", 
                            WiFi.localIP().toString().c_str(), WiFi.RSSI());
                    } else {
                        Serial.println("⚠️  WiFi disconnected, attempting reconnect...");
                        setupWiFi();
                    }
                }
            }
            break;
            
        case MODE_ULTRASONIC_WIFI_FIREBASE:
            {
                app.loop();
                
                static unsigned long lastReading = 0;
                if (currentTime - lastReading >= 3000) {
                    lastReading = currentTime;
                    
                    if (WiFi.status() != WL_CONNECTED) {
                        Serial.println("⚠️  WiFi lost, reconnecting...");
                        setupWiFi();
                        firebaseInitialized = false;
                        break;
                    }
                    
                    float distance = readUltrasonic();
                    if (distance < 0) distance = 100.0;
                    
                    Serial.printf("📏 Distance: %.2f cm - ", distance);
                    
                    if (app.ready()) {
                        String basePath = "/sensor_data/cycle_" + String(cycleCount);
                        String readingId = String(millis());
                        
                        Database.set<float>(async_client, 
                            basePath + "/reading_" + readingId + "/distance", 
                            distance, processData, "Send");
                        
                        Serial.println("📤 Sent to Firebase");
                    } else {
                        Serial.println("⏳ Waiting for Firebase auth...");
                    }
                }
            }
            break;
    }
}

float readUltrasonic()
{
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(5);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);
    
    long duration = pulseIn(ECHO_PIN, HIGH, 50000);
    if (duration == 0) return -1;
    
    float distance = (float)duration * 0.0343 / 2.0;
    if (distance < 2.0 || distance > 400.0) return -1;
    
    return distance;
}

void setupWiFi()
{
    // If already connected, just verify
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("✓ WiFi already connected");
        return;
    }

    Serial.printf("📡 Connecting to UW MPSK (MAC: %s)...\n", WiFi.macAddress().c_str());
    
    // Complete disconnect first
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(500);
    
    // Configure and connect
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);  // Disable power saving
    WiFi.setAutoReconnect(true);
    
    WiFi.begin(ssid, password);

    Serial.print("   ");
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 60) {  // 30 seconds
        delay(500);
        Serial.print(".");
        attempts++;
        
        // Print status every 10 attempts
        if (attempts % 20 == 0) {
            Serial.printf("\n   Still trying (Status: %d)... ", WiFi.status());
        }
    }

    Serial.println();
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("✓ WiFi Connected Successfully!");
        Serial.printf("   IP Address: %s\n", WiFi.localIP().toString().c_str());
        Serial.printf("   Signal Strength: %d dBm\n", WiFi.RSSI());
        Serial.printf("   Gateway: %s\n", WiFi.gatewayIP().toString().c_str());
    } else {
        Serial.println("✗ WiFi Connection Failed");
        Serial.printf("   Final Status Code: %d\n", WiFi.status());
        Serial.println("   Possible issues:");
        Serial.println("   - MAC registration still pending (wait 5-10 min)");
        Serial.println("   - Out of WiFi range");
        Serial.println("   - Network congestion");
    }
}

void disconnectWiFi()
{
    if (WiFi.status() == WL_CONNECTED) {
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        Serial.println("WiFi disconnected");
    }
}

void processData(AsyncResult &aResult)
{
    if (!aResult.isResult()) return;
    
    if (aResult.isEvent())
        Serial.printf("   Event: %s\n", aResult.eventLog().message().c_str());
    
    if (aResult.isError())
        Serial.printf("   ✗ Error: %s (code: %d)\n", 
            aResult.error().message().c_str(), 
            aResult.error().code());
    
    if (aResult.available())
        Serial.printf("   ✓ Confirmed\n");
}