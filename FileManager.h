#ifndef FILEMANAGER_H
#define FILEMANAGER_H

#include <Arduino.h>
#include <vector>

class FileManager {
public:
    static bool init();
    static std::vector<String> listFiles();
    static bool fileExists(const String& filename);
    static String readFile(const String& filename);
    static bool writeFile(const String& filename, const String& content);
    static bool deleteFile(const String& filename);
    static void runGCodeFile(const String& filename);
    
    // Для Web UI
    static String getFileListHTML();
    static size_t getFileSize(const String& filename);
    
private:
    static bool sdCardReady;
};

#endif