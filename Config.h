#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// Настройки WiFi
#define WIFI_SSID "PlasmaCNC-S3"
#define WIFI_PASSWORD "12345678"

// Пины ESP32-S3
namespace Pins {
    // Шаговые драйверы
    constexpr uint8_t X_STEP = 14;
    constexpr uint8_t X_DIR = 13;
    constexpr uint8_t Y_STEP = 12;
    constexpr uint8_t Y_DIR = 11;
    constexpr uint8_t Z_STEP = 10;
    constexpr uint8_t Z_DIR = 9;
    
    // Концевики
    constexpr uint8_t X_LIMIT = 18;
    constexpr uint8_t Y_LIMIT = 17;
    constexpr uint8_t Z_LIMIT = 16;
    
    // Плазма
    constexpr uint8_t PLASMA_RELAY = 5;
    constexpr uint8_t ARC_SENSOR = 4;
    
    // Светодиод
    constexpr uint8_t LED_R = 45;
    constexpr uint8_t LED_G = 46;
    constexpr uint8_t LED_B = 47;
}

// Настройки шагов
namespace Stepper {
    constexpr float STEPS_PER_MM_XY = 80.0f;   // Ремень GT2
    constexpr float STEPS_PER_MM_Z = 800.0f;   // Винт T8 4mm
    constexpr float MAX_SPEED_XY = 6000.0f;    // мм/мин
    constexpr float MAX_SPEED_Z = 2000.0f;     // мм/мин
    constexpr float ACCELERATION_XY = 800.0f;  // мм/с²
    constexpr float ACCELERATION_Z = 300.0f;   // мм/с²
}

// Настройки THC
namespace THC {
    constexpr float DEFAULT_VOLTAGE = 140.0f;
    constexpr float DEAD_ZONE = 5.0f;
    constexpr float CORRECTION_RATE = 0.1f;
    constexpr float MAX_CORRECTION = 10.0f;
}

// Настройки машины
namespace Machine {
    constexpr float TRAVEL_X = 1000.0f;  // мм
    constexpr float TRAVEL_Y = 1000.0f;  // мм
    constexpr float TRAVEL_Z = 150.0f;  // мм
}

#endif