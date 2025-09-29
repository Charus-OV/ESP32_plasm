#ifndef THC_SYSTEM_H
#define THC_SYSTEM_H

class THC_System {
public:
    static void init();
    static void update();
    static bool isEnabled();
    static bool isActive();
    static float getTargetVoltage();  // Должно быть float
    
    static void toggle();
    static void setTargetVoltage(float voltage);  // Должно быть float
    
private:
    static bool enabled;
    static bool active;
    static float targetVoltage;  // Должно быть float
};

#endif