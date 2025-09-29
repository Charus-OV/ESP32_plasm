#ifndef STEPPERCONTROL_H
#define STEPPERCONTROL_H

#include <Arduino.h>
#include "Config.h"

class StepperControl {
public:
    static void init();
    static void update();
    static void jog(String direction, float distance);
    static void homeAll();
    
    static float getCurrentX();
    static float getCurrentY();
    static float getCurrentZ();
    
private:
    static float currentX, currentY, currentZ;
    static bool isHomed;
};

#endif