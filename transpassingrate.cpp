/**
 * ESP32-C3 Firebase Transmission Rate Test
 * Tests 5 different data rates: 2Hz, 1Hz, 0.5Hz, 0.333Hz, 0.25Hz
 */

#define ENABLE_USER_AUTH
#define ENABLE_DATABASE

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <FirebaseClient.h>

// Pin definitions
#define TRIG_PIN 2
#define ECHO_PIN 3

// WiFi credentials
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

// Transmission rate modes
enum TransmissionMode {
    MODE_2HZ = 0,      // 2 times per second
    MODE_1HZ = 1,      // 1 time per second
    MODE_0_5HZ = 2,    // Once every 2 seconds
    MODE_0_33HZ = 3,   // Once every 3 seconds
    MODE_0_25HZ = 4    // Once every 4 seconds
};

TransmissionMode currentMode = MODE_2HZ;
unsigned long modeStartTime = 0;
const unsigned long MODE_DURATION = 60000; // 60 seconds per mode for accurate measurement
int modeCount = 0;
int readingCount = 0;

// Transmission intervals (milliseconds)
const unsigned long INTERVALS[] = {
    500,   // 2 Hz = every 500ms
    1000,  // 1 Hz = every 1000ms
    2000,  // 0.5 Hz = every 2000ms
    3000,  // 0.333 Hz = every 3000ms
    4000   // 0.25 Hz = every 4000ms
};

const char* MODE_NAMES[] = {
    "2 Hz (2x per second)",
    "1 Hz (1x per second)",
    "0.5 Hz (every 2 sec)",
    "0.333 Hz (every 3 sec)",
    "0.25 Hz (every 4 sec)"
};

// Function declarations
void processData(AsyncResult &aResult);
float readUltrasonic();
void setupWiFi();

void setup()
{
    Serial.begin(115200);
    delay(1000);
    
    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);
    digitalWrite(TRIG_PIN, LOW);
    
    Serial.println("\n========================================");
    Serial.println("  FIREBASE TRANSMISSION RATE TEST");
    Serial.println("========================================");
    
    // Connect WiFi
    setupWiFi();
    
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("ERROR: WiFi not connected!");
        Serial.println("Cannot proceed with test.");
        while(1) delay(1000);
    }
    
    // Initialize Firebase
    Serial.println("\nInitializing Firebase...");
    ssl_client.setInsecure();
    ssl_client.setHandshakeTimeout(10);
    initializeApp(async_client, app, getAuth(user_auth), processData, "authTask");
    app.getApp<RealtimeDatabase>(Database);
    Database.url("https://esp-project-7e4c3-default-rtdb.firebaseio.com/");
    
    // Wait for Firebase ready
    Serial.print("Waiting for Firebase auth");
    int attempts = 0;
    while (!app.ready() && attempts < 30) {
        app.loop();
        Serial.print(".");
        delay(1000);
        attempts++;
    }
    Serial.println();
    
    if (!app.ready()) {
        Serial.println("ERROR: Firebase not ready!");
        Serial.println("Cannot proceed with test.");
        while(1) delay(1000);
    }
    
    Serial.println("✓ Firebase Ready!");
    Serial.println("\n========================================");
    Serial.println("Starting Test Sequence");
    Serial.println("Each mode runs for 60 seconds");
    Serial.println("========================================\n");
    
    delay(2000);
    
    // Start first mode
    modeCount = 1;
    readingCount = 0;
    currentMode = MODE_2HZ;
    modeStartTime = millis();
    
    Serial.println("┌────────────────────────────────────────┐");
    Serial.printf("│ MODE %d: %s           │\n", modeCount, MODE_NAMES[currentMode]);
    Serial.println("│ Duration: 60 seconds                   │");
    Serial.printf("│ Interval: %lu ms                      │\n", INTERVALS[currentMode]);
    Serial.println("└────────────────────────────────────────┘");
    Serial.println("\n⏱️  Recording power consumption...\n");
}

void loop()
{
    unsigned long currentTime = millis();
    app.loop();
    
    // Check if mode duration completed
    if (currentTime - modeStartTime >= MODE_DURATION) {
        Serial.println("\n========================================");
        Serial.printf("MODE %d COMPLETE: %s\n", modeCount, MODE_NAMES[currentMode]);
        Serial.printf("Total readings sent: %d\n", readingCount);
        Serial.println("========================================\n");
        
        // Check Power Profiler now
        Serial.println("📊 CHECK POWER PROFILER NOW!");
        Serial.println("   - Note the AVERAGE current (mA)");
        Serial.println("   - Zoom to the last 60 seconds");
        Serial.println("   - Write down the value\n");
        
        // Wait before next mode
        Serial.println("⏸️  Pausing 10 seconds before next mode...\n");
        delay(10000);
        
        // Move to next mode
        currentMode = (TransmissionMode)((currentMode + 1) % 5);
        
        // Check if completed all modes
        if (currentMode == MODE_2HZ && modeCount >= 5) {
            Serial.println("\n========================================");
            Serial.println("  ALL TESTS COMPLETED!");
            Serial.println("========================================\n");
            Serial.println("Summary of data to record:");
            Serial.println("1. MODE 1 (2 Hz): ___ mA");
            Serial.println("2. MODE 2 (1 Hz): ___ mA");
            Serial.println("3. MODE 3 (0.5 Hz): ___ mA");
            Serial.println("4. MODE 4 (0.333 Hz): ___ mA");
            Serial.println("5. MODE 5 (0.25 Hz): ___ mA");
            Serial.println("\nTest will restart in 30 seconds...\n");
            delay(30000);
            modeCount = 0;
        }
        
        // Start next mode
        modeCount++;
        readingCount = 0;
        modeStartTime = currentTime;
        
        Serial.println("┌────────────────────────────────────────┐");
        Serial.printf("│ MODE %d: %s           │\n", modeCount, MODE_NAMES[currentMode]);
        Serial.println("│ Duration: 60 seconds                   │");
        Serial.printf("│ Interval: %lu ms                      │\n", INTERVALS[currentMode]);
        Serial.println("└────────────────────────────────────────┘");
        Serial.println("\n⏱️  Recording power consumption...\n");
    }
    
    // Send data at appropriate interval
    static unsigned long lastSend = 0;
    if (currentTime - lastSend >= INTERVALS[currentMode]) {
        lastSend = currentTime;
        readingCount++;
        
        // Read sensor
        float distance = readUltrasonic();
        if (distance < 0) distance = 100.0;
        
        // Send to Firebase
        String path = "/power_test/mode_" + String(modeCount) + 
                     "/reading_" + String(readingCount);
        
        Database.set<float>(async_client, path + "/distance", distance, processData, "Send");
        Database.set<unsigned long>(async_client, path + "/timestamp", millis(), processData, "Send");
        
        // Print progress
        Serial.printf("[%02lu:%02lu] Reading #%d: %.2f cm → Firebase\n", 
                     (currentTime - modeStartTime) / 60000,
                     ((currentTime - modeStartTime) / 1000) % 60,
                     readingCount, distance);
    }
    
    delay(10); // Small delay to prevent watchdog issues
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
    if (WiFi.status() == WL_CONNECTED) return;

    Serial.printf("📡 Connecting to %s...\n", ssid);
    
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(500);
    
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.begin(ssid, password);

    int attempts = 0;
    Serial.print("   ");
    while (WiFi.status() != WL_CONNECTED && attempts < 60) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    Serial.println();
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("✓ WiFi Connected!");
        Serial.printf("   IP: %s\n", WiFi.localIP().toString().c_str());
        Serial.printf("   Signal: %d dBm\n", WiFi.RSSI());
    } else {
        Serial.println("✗ WiFi Failed!");
    }
}

void processData(AsyncResult &aResult)
{
    if (!aResult.isResult()) return;
    
    if (aResult.isError()) {
        Serial.printf("   ✗ Firebase Error: %s\n", aResult.error().message().c_str());
    }
}
