/*
 * СИСТЕМА ПРЕСЕТОВ МАТЕРИАЛОВ - MaterialPresets.h
 * Готовые настройки для разных материалов и толщин
 * 
 * Зачем это нужно:
 * - Не нужно каждый раз вручную настраивать параметры
 * - Оптимальные настройки для качественного реза
 * - Быстрое переключение между материалами
 */

#ifndef MATERIALPRESETS_H
#define MATERIALPRESETS_H

#include <Arduino.h>
#include <vector>

// Структура для хранения настроек материала
struct MaterialPreset {
    String name;           // Название материала (Сталь 3мм)
    float voltage;         // Целевое напряжение дуги (V)
    float speed;           // Скорость резки (мм/мин)
    float pierceHeight;    // Высота пробивки (мм)
    float cutHeight;       // Рабочая высота резки (мм)
    float pierceDelay;     // Задержка пробивки (секунды)
    float kerfWidth;       // Ширина реза (мм) - для компенсации
};

class MaterialPresets {
public:
    // Инициализация системы пресетов
    static void init();
    
    // Получить пресет по имени
    static MaterialPreset getPreset(const String& name);
    
    // Получить все доступные пресеты
    static std::vector<MaterialPreset> getAllPresets();
    
    // Применить настройки пресета к системе
    static void applyPreset(const String& name);
    
    // Найти подходящий пресет по толщине и материалу
    static MaterialPreset findPreset(const String& material, float thickness);

private:
    // Статическая переменная - база данных пресетов
    static std::vector<MaterialPreset> presets;
};

#endif