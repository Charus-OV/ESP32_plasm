#include "StepperControl.h"

float StepperControl::currentX = 0;
float StepperControl::currentY = 0;
float StepperControl::currentZ = 0;
bool StepperControl::isHomed = false;

void StepperControl::init() {
    pinMode(Pins::X_STEP, OUTPUT);
    pinMode(Pins::X_DIR, OUTPUT);
    pinMode(Pins::Y_STEP, OUTPUT);
    pinMode(Pins::Y_DIR, OUTPUT);
    pinMode(Pins::Z_STEP, OUTPUT);
    pinMode(Pins::Z_DIR, OUTPUT);
    
    pinMode(Pins::X_LIMIT, INPUT_PULLUP);
    pinMode(Pins::Y_LIMIT, INPUT_PULLUP);
    pinMode(Pins::Z_LIMIT, INPUT_PULLUP);
    
    Serial.println("✅ Stepper control initialized");
}

void StepperControl::update() {
    // Здесь будет логика движения
    // Пока просто заглушка
}

void StepperControl::jog(String direction, float distance) {
    Serial.println("Jog: " + direction + " " + String(distance) + "mm");
    
    // Эмуляция движения для теста
    if (direction == "X+") currentX += distance;
    else if (direction == "X-") currentX -= distance;
    else if (direction == "Y+") currentY += distance;
    else if (direction == "Y-") currentY -= distance;
    else if (direction == "Z+") currentZ += distance;
    else if (direction == "Z-") currentZ -= distance;
    
    Serial.printf("New position: X%.1f Y%.1f Z%.1f\n", currentX, currentY, currentZ);
}

void StepperControl::homeAll() {
    Serial.println("🏠 Homing all axes...");
    currentX = 0;
    currentY = 0;
    currentZ = 0;
    isHomed = true;
    Serial.println("✅ Homing complete");
}

float StepperControl::getCurrentX() { return currentX; }
float StepperControl::getCurrentY() { return currentY; }
float StepperControl::getCurrentZ() { return currentZ; }