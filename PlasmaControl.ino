#include "PlasmaControl.h"

bool PlasmaControl::plasmaActive = false;
bool PlasmaControl::arcOK = false;
float PlasmaControl::arcVoltage = 0;

void PlasmaControl::init() {
    pinMode(Pins::PLASMA_RELAY, OUTPUT);
    digitalWrite(Pins::PLASMA_RELAY, LOW);
    
    pinMode(Pins::ARC_SENSOR, INPUT);
    
    Serial.println("✅ Plasma control initialized");
}

void PlasmaControl::update() {
    // Эмуляция датчика напряжения дуги
    if (plasmaActive) {
        arcVoltage = 120.0 + random(-10, 10); // Случайное значение 110-130V
        arcOK = (arcVoltage > 100.0 && arcVoltage < 180.0);
    } else {
        arcVoltage = 0;
        arcOK = false;
    }
}

void PlasmaControl::startPlasma() {
    digitalWrite(Pins::PLASMA_RELAY, HIGH);
    plasmaActive = true;
    Serial.println("🔥 Plasma started");
}

void PlasmaControl::stopPlasma() {
    digitalWrite(Pins::PLASMA_RELAY, LOW);
    plasmaActive = false;
    Serial.println("💧 Plasma stopped");
}

bool PlasmaControl::isActive() { return plasmaActive; }
bool PlasmaControl::isArcOK() { return arcOK; }
float PlasmaControl::getArcVoltage() { return arcVoltage; }