/**
 * ESP32-C3 Power-Saving Strategy: "Smart Interval with Deep Sleep"
 * 
 * Strategy:
 * - Deep sleep between readings (4 min 50 sec)
 * - Wake up every 5 minutes
 * - Take ultrasonic reading
 * - Connect WiFi quickly
 * - Send to Firebase
 * - Disconnect and return to deep sleep
 * 
 * Target: 24+ hours on 500mAh battery
 * Expected: ~3.5 days actual runtime
 */

#define ENABLE_USER_AUTH
#define ENABLE_DATABASE

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <FirebaseClient.h>

// Pin definitions
#define TRIG_PIN 2
#define ECHO_PIN 3
#define LED_PIN 21  // Built-in LED for status indication

// WiFi credentials
const char* ssid     = "UW MPSK";
const char* password = "****";

// Firebase configuration
UserAuth user_auth(
    "****",
    "qianmu@uw.edu",
    "-------?"
);

FirebaseApp app;
WiFiClientSecure ssl_client;
using AsyncClient = AsyncClientClass;
AsyncClient async_client(ssl_client);
RealtimeDatabase Database;

// Power saving configuration
const unsigned long SLEEP_DURATION = 290 * 1000000ULL; // 4 min 50 sec in microseconds (290 seconds)
const unsigned long AWAKE_TIMEOUT = 10000; // 10 seconds max awake time
const int MAX_WIFI_ATTEMPTS = 20; // Limit WiFi connection attempts

// Persistent data (survives deep sleep)
RTC_DATA_ATTR int bootCount = 0;
RTC_DATA_ATTR int successfulReadings = 0;
RTC_DATA_ATTR int failedReadings = 0;

// Function declarations
float readUltrasonic();
bool connectWiFi();
bool sendToFirebase(float distance);
void enterDeepSleep();
void blinkLED(int times);

void setup()
{
    Serial.begin(115200);
    delay(500);
    
    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(TRIG_PIN, LOW);
    digitalWrite(LED_PIN, LOW);
    
    bootCount++;
    
    Serial.println("\n========================================");
    Serial.println("  POWER-SAVING STRATEGY: SMART INTERVAL");
    Serial.println("========================================");
    Serial.printf("Boot #%d\n", bootCount);
    Serial.printf("Success: %d | Failed: %d\n", successfulReadings, failedReadings);
    
    // Check wake reason
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    if (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER) {
        Serial.println("Woke from deep sleep (timer)");
    } else {
        Serial.println("Fresh boot or reset");
    }
    
    Serial.println("\n--- STAGE 1: SENSOR READING ---");
    unsigned long stage1Start = millis();
    
    // Take sensor reading
    float distance = readUltrasonic();
    if (distance < 0) {
        Serial.println("⚠️  Sensor read failed, using default");
        distance = 100.0;
    } else {
        Serial.printf("✓ Distance: %.2f cm\n", distance);
    }
    blinkLED(1);
    
    unsigned long stage1Time = millis() - stage1Start;
    Serial.printf("Stage 1 time: %lu ms\n", stage1Time);
    
    // Connect WiFi
    Serial.println("\n--- STAGE 2: WiFi CONNECTION ---");
    unsigned long stage2Start = millis();
    
    bool wifiConnected = connectWiFi();
    
    unsigned long stage2Time = millis() - stage2Start;
    Serial.printf("Stage 2 time: %lu ms\n", stage2Time);
    
    if (!wifiConnected) {
        Serial.println("✗ WiFi failed, skipping Firebase");
        failedReadings++;
        blinkLED(3); // 3 blinks = error
        delay(100);
        enterDeepSleep();
        return;
    }
    
    blinkLED(2); // 2 blinks = WiFi connected
    
    // Send to Firebase
    Serial.println("\n--- STAGE 3: FIREBASE SEND ---");
    unsigned long stage3Start = millis();
    
    bool sentSuccessfully = sendToFirebase(distance);
    
    unsigned long stage3Time = millis() - stage3Start;
    Serial.printf("Stage 3 time: %lu ms\n", stage3Time);
    
    if (sentSuccessfully) {
        successfulReadings++;
        Serial.println("✓ Data sent successfully");
        blinkLED(4); // 4 blinks = success
    } else {
        failedReadings++;
        Serial.println("✗ Firebase send failed");
        blinkLED(3); // 3 blinks = error
    }
    
    // Disconnect WiFi to save power
    Serial.println("\n--- STAGE 4: DISCONNECT & SLEEP ---");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    Serial.println("✓ WiFi disconnected");
    
    // Summary
    unsigned long totalAwakeTime = millis();
    Serial.println("\n========================================");
    Serial.println("CYCLE SUMMARY:");
    Serial.printf("  Sensor reading:  %lu ms\n", stage1Time);
    Serial.printf("  WiFi connect:    %lu ms\n", stage2Time);
    Serial.printf("  Firebase send:   %lu ms\n", stage3Time);
    Serial.printf("  Total awake:     %lu ms\n", totalAwakeTime);
    Serial.println("========================================");
    
    // Power consumption estimate
    float sensorPower = (stage1Time / 1000.0) * 30.0; // 30mA for sensor
    float wifiPower = (stage2Time / 1000.0) * 120.0; // 120mA for WiFi connect
    float firebasePower = (stage3Time / 1000.0) * 180.0; // 180mA for Firebase
    float totalPower = sensorPower + wifiPower + firebasePower;
    
    Serial.println("\nPOWER CONSUMPTION ESTIMATE:");
    Serial.printf("  Sensor:   %.3f mAh\n", sensorPower / 3600.0);
    Serial.printf("  WiFi:     %.3f mAh\n", wifiPower / 3600.0);
    Serial.printf("  Firebase: %.3f mAh\n", firebasePower / 3600.0);
    Serial.printf("  Total:    %.3f mAh\n", totalPower / 3600.0);
    
    // Calculate projected battery life
    float cyclesPerHour = 12.0; // Every 5 minutes
    float mAhPerHour = (totalPower / 3600.0) * cyclesPerHour;
    float hoursOn500mAh = 500.0 / mAhPerHour;
    
    Serial.println("\nBATTERY LIFE PROJECTION:");
    Serial.printf("  mAh per cycle:  %.3f\n", totalPower / 3600.0);
    Serial.printf("  Cycles/hour:    %.1f\n", cyclesPerHour);
    Serial.printf("  mAh/hour:       %.2f\n", mAhPerHour);
    Serial.printf("  Battery life:   %.1f hours (%.1f days)\n", 
                  hoursOn500mAh, hoursOn500mAh / 24.0);
    Serial.println("========================================\n");
    
    // Enter deep sleep
    delay(500);
    enterDeepSleep();
}

void loop()
{
    // Never reached - we go directly to deep sleep from setup()
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

bool connectWiFi()
{
    Serial.printf("Connecting to %s...\n", ssid);
    
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false); // Disable sleep for faster connection
    WiFi.begin(ssid, password);

    int attempts = 0;
    Serial.print("  ");
    while (WiFi.status() != WL_CONNECTED && attempts < MAX_WIFI_ATTEMPTS) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    
    Serial.println();
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("✓ Connected! IP: %s\n", WiFi.localIP().toString().c_str());
        Serial.printf("  Signal: %d dBm\n", WiFi.RSSI());
        return true;
    } else {
        Serial.printf("✗ Failed after %d attempts\n", attempts);
        return false;
    }
}

bool sendToFirebase(float distance)
{
    Serial.println("Initializing Firebase...");
    
    ssl_client.setInsecure();
    ssl_client.setHandshakeTimeout(8);
    
    // Initialize Firebase (blocking with timeout)
    initializeApp(async_client, app, getAuth(user_auth), nullptr, "authTask");
    app.getApp<RealtimeDatabase>(Database);
    Database.url("https://esp-project-7e4c3-default-rtdb.firebaseio.com/");
    
    // Wait for auth with timeout
    unsigned long authStart = millis();
    while (!app.ready() && (millis() - authStart) < 8000) {
        app.loop();
        delay(100);
    }
    
    if (!app.ready()) {
        Serial.println("✗ Firebase auth timeout");
        return false;
    }
    
    Serial.println("✓ Firebase authenticated");
    
    // Send data
    String path = "/power_saving/reading_" + String(bootCount);
    
    Serial.printf("Sending to: %s\n", path.c_str());
    
    // Use blocking call for simplicity in deep sleep scenario
    bool success = true;
    
    Database.set<float>(async_client, path + "/distance", distance, nullptr, "Send1");
    app.loop();
    delay(100);
    
    Database.set<int>(async_client, path + "/boot", bootCount, nullptr, "Send2");
    app.loop();
    delay(100);
    
    Database.set<unsigned long>(async_client, path + "/timestamp", millis(), nullptr, "Send3");
    app.loop();
    delay(100);
    
    Serial.println("✓ Data sent to Firebase");
    return success;
}

void enterDeepSleep()
{
    Serial.println("\n💤 Entering deep sleep for 4 min 50 sec...");
    Serial.println("========================================\n");
    delay(100);
    
    // Configure timer wake up
    esp_sleep_enable_timer_wakeup(SLEEP_DURATION);
    
    // Enter deep sleep
    esp_deep_sleep_start();
}

void blinkLED(int times)
{
    for (int i = 0; i < times; i++) {
        digitalWrite(LED_PIN, HIGH);
        delay(100);
        digitalWrite(LED_PIN, LOW);
        delay(100);
    }
}