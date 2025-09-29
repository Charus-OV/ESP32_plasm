/*
 * РЕАЛИЗАЦИЯ СИСТЕМЫ ПРЕСЕТОВ - MaterialPresets.cpp
 * Здесь хранятся и обрабатываются настройки материалов
 */

#include "MaterialPresets.h"
#include "THC_System.h"
#include <vector>

// Статическая переменная - база данных пресетов
std::vector<MaterialPreset> MaterialPresets::presets;

/*
 * ИНИЦИАЛИЗАЦИЯ СИСТЕМЫ ПРЕСЕТОВ
 * Заполняем базу данных готовыми настройками
 */
void MaterialPresets::init() {
    Serial.println("🎯 Инициализация системы пресетов материалов...");
    
    // Очищаем старые пресеты (если есть)
    presets.clear();
    
    // ДОБАВЛЯЕМ ПРЕСЕТЫ ДЛЯ РАЗНЫХ МАТЕРИАЛОВ И ТОЛЩИН:
    
    // === СТАЛЬ НИЗКОУГЛЕРОДИСТАЯ ===
    presets.push_back({"Сталь 1мм", 85, 4700, 2.2, 2.0, 0.5, 1.0});
    presets.push_back({"Сталь 2мм", 90, 4200, 2.3, 2.0, 0.6, 1.1});
    presets.push_back({"Сталь 3мм", 95, 3800, 2.5, 2.0, 0.7, 1.2});
    presets.push_back({"Сталь 5мм", 105, 3200, 2.8, 2.0, 0.9, 1.3});
    presets.push_back({"Сталь 6мм", 115, 2800, 3.0, 2.0, 1.0, 1.4});
    presets.push_back({"Сталь 8мм", 125, 2200, 3.5, 2.0, 1.2, 1.5});
    presets.push_back({"Сталь 10мм", 135, 1800, 3.8, 2.0, 1.4, 1.6});
    presets.push_back({"Сталь 12мм", 140, 1500, 4.0, 2.0, 1.5, 1.7});
    presets.push_back({"Сталь 15мм", 150, 1200, 4.5, 2.0, 1.8, 1.8});
    presets.push_back({"Сталь 20мм", 160, 900, 5.0, 2.0, 2.0, 2.0});
    
    // === НЕРЖАВЕЮЩАЯ СТАЛЬ ===
    presets.push_back({"Нержавейка 2мм", 100, 3500, 2.5, 2.2, 0.8, 1.2});
    presets.push_back({"Нержавейка 4мм", 115, 2800, 3.0, 2.2, 1.0, 1.4});
    presets.push_back({"Нержавейка 6мм", 130, 2200, 3.5, 2.2, 1.2, 1.6});
    
    // === АЛЮМИНИЙ ===
    presets.push_back({"Алюминий 2мм", 95, 4000, 3.0, 2.5, 0.7, 1.3});
    presets.push_back({"Алюминий 4мм", 110, 3200, 3.5, 2.5, 0.9, 1.5});
    presets.push_back({"Алюминий 6мм", 125, 2500, 4.0, 2.5, 1.1, 1.7});
    presets.push_back({"Алюминий 10мм", 140, 1800, 5.0, 2.5, 1.5, 2.0});
    
    // === МЕДЬ ===
    presets.push_back({"Медь 3мм", 120, 3000, 3.5, 2.8, 1.0, 1.6});
    presets.push_back({"Медь 6мм", 140, 2000, 4.5, 2.8, 1.4, 1.9});
    
    Serial.println("✅ Загружено пресетов: " + String(presets.size()));
    
    // Выводим список загруженных пресетов
    for(const auto& preset : presets) {
        Serial.println("   📋 " + preset.name + " - " + 
                      String(preset.speed) + " мм/мин, " + 
                      String(preset.voltage) + "V");
    }
}

/*
 * ПОЛУЧИТЬ ПРЕСЕТ ПО ИМЕНИ
 * Ищет пресет в базе данных по точному имени
 */
MaterialPreset MaterialPresets::getPreset(const String& name) {
    // Ищем пресет с указанным именем
    for(const auto& preset : presets) {
        if(preset.name == name) {
            return preset;  // Нашли - возвращаем
        }
    }
    
    // Если не нашли - возвращаем пресет по умолчанию
    Serial.println("⚠  Пресет не найден: " + name + ", использую Сталь 6мм");
    return {"Сталь 6мм", 115, 2800, 3.0, 2.0, 1.0, 1.4};
}

/*
 * ПОЛУЧИТЬ ВСЕ ДОСТУПНЫЕ ПРЕСЕТЫ
 * Возвращает копию всей базы данных пресетов
 */
std::vector<MaterialPreset> MaterialPresets::getAllPresets() {
    return presets;  // Возвращаем копию базы данных
}

/*
 * ПРИМЕНИТЬ НАСТРОЙКИ ПРЕСЕТА
 * Устанавливает параметры пресета в THC систему
 */
void MaterialPresets::applyPreset(const String& name) {
    // Получаем настройки пресета
    MaterialPreset preset = getPreset(name);
    
    Serial.println("🎯 Применяю пресет: " + preset.name);
    Serial.println("   ⚡ Напряжение: " + String(preset.voltage) + "V");
    Serial.println("   🚀 Скорость: " + String(preset.speed) + " мм/мин");
    Serial.println("   📏 Высота пробивки: " + String(preset.pierceHeight) + "мм");
    Serial.println("   🔪 Рабочая высота: " + String(preset.cutHeight) + "мм");
    Serial.println("   ⏱  Задержка пробивки: " + String(preset.pierceDelay) + "с");
    
    // Здесь будем применять настройки к THC системе
    // THC_System::setTargetVoltage(preset.voltage);
    // THC_System::setCutHeight(preset.cutHeight);
    // и т.д.
}

/*
 * НАЙТИ ПОДХОДЯЩИЙ ПРЕСЕТ
 * Ищет пресет по материалу и толщине (умный поиск)
 */
MaterialPreset MaterialPresets::findPreset(const String& material, float thickness) {
    String searchPattern = material + " " + String(thickness) + "мм";
    
    // Сначала ищем точное совпадение
    for(const auto& preset : presets) {
        if(preset.name == searchPattern) {
            return preset;
        }
    }
    
    // Если точного совпадения нет, ищем ближайшую толщину
    MaterialPreset closestPreset = presets[0];  // Первый пресет как запасной
    float minDifference = 1000.0f;  // Большое число для сравнения
    
    for(const auto& preset : presets) {
        if(preset.name.startsWith(material)) {
            // Извлекаем толщину из имени пресета
            int mmPos = preset.name.indexOf("мм");
            if(mmPos != -1) {
                String thicknessStr = preset.name.substring(material.length() + 1, mmPos);
                float presetThickness = thicknessStr.toFloat();
                
                // Сравниваем разницу толщин
                float difference = fabs(presetThickness - thickness);
                if(difference < minDifference) {
                    minDifference = difference;
                    closestPreset = preset;
                }
            }
        }
    }
    
    Serial.println("🔍 Найден ближайший пресет: " + closestPreset.name + 
                  " (разница: " + String(minDifference) + "мм)");
    return closestPreset;
}