#include "THC_System.h"

bool THC_System::enabled = false;
bool THC_System::active = false;
float THC_System::targetVoltage = 140.0;  // float с точкой!

void THC_System::init() {
    enabled = false;
    active = false;
    targetVoltage = 140.0;
}

void THC_System::update() {
    // Логика обновления THC
}

bool THC_System::isEnabled() {
    return enabled;
}

bool THC_System::isActive() {
    return active;
}

float THC_System::getTargetVoltage() {  // float!
    return targetVoltage;
}

void THC_System::toggle() {
    enabled = !enabled;
    Serial.printf("THC %s\n", enabled ? "enabled" : "disabled");
}

void THC_System::setTargetVoltage(float voltage) {  // float!
    targetVoltage = voltage;
    Serial.printf("THC target voltage set to: %.1f\n", voltage);
}