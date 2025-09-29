#include "WebInterface.h"
#include "Config.h"
#include "StepperControl.h"
#include "PlasmaControl.h"
#include "THC_System.h"
#include "FileManager.h"

void setup() {
  Serial.begin(115200);
  delay(2000);
  
  Serial.println("\n\n✨ =================================");
  Serial.println("✨    Plasma CNC Controller");
  Serial.println("✨    ESP32-S3 Initialization");
  Serial.println("✨ =================================\n");
  
  // Инициализация компонентов
  Serial.println("🔧 Initializing components...");
  
  if (!FileManager::init()) {
    Serial.println("❌ FileManager initialization failed");
  }
  
  StepperControl::init();
  PlasmaControl::init();
  THC_System::init();
  WebInterface::init();
  
  Serial.println("\n✅ All systems ready!");
  Serial.println("👉 Connect to WiFi: " + String(WIFI_SSID));
  Serial.println("👉 Password: " + String(WIFI_PASSWORD));
  Serial.println("👉 Open: http://" + WiFi.softAPIP().toString());
}

void loop() {
  WebInterface::handleClient();
  StepperControl::update();
  PlasmaControl::update();
  THC_System::update();
  
  // Мигаем светодиодом
  static unsigned long lastBlink = 0;
  if (millis() - lastBlink > 1000) {
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    lastBlink = millis();
  }
  
  delay(10);
}