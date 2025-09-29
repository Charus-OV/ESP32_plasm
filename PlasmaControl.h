#ifndef PLASMACONTROL_H
#define PLASMACONTROL_H

#include <Arduino.h>
#include "Config.h"

class PlasmaControl {
public:
    static void init();
    static void update();
    static void startPlasma();
    static void stopPlasma();
    
    static bool isActive();
    static bool isArcOK();
    static float getArcVoltage();
    
private:
    static bool plasmaActive;
    static bool arcOK;
    static float arcVoltage;
};

#endif