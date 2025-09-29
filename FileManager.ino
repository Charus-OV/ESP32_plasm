#include "FileManager.h"
#include <SD_MMC.h>
#include "Config.h"

bool FileManager::sdCardReady = false;

bool FileManager::init() {
    sdCardReady = SD_MMC.begin();
    if (sdCardReady) {
        Serial.println("✅ SD Card initialized");
        // Создаем необходимые папки
        SD_MMC.mkdir("/gcode");
        SD_MMC.mkdir("/config");
        SD_MMC.mkdir("/logs");
        
        // Создаем тестовый файл если папка пустая
        if(!fileExists("/gcode/test_circle.nc")) {
            String testGcode = R"(G21 ; Set units to millimeters
G90 ; Use absolute coordinates
G00 Z5.0 ; Lift nozzle
G00 X0 Y0 ; Move to start
M03 ; Plasma ON
G04 P1.0 ; Pierce delay
G00 Z2.0 ; Cut height
G02 X50 Y0 I25 J0 ; Cut circle
G00 Z5.0 ; Lift nozzle
M05 ; Plasma OFF
G00 X0 Y0 ; Return home)";
            writeFile("/gcode/test_circle.nc", testGcode);
        }
        
        return true;
    } else {
        Serial.println("❌ SD Card initialization failed");
        return false;
    }
}

std::vector<String> FileManager::listFiles() {
    std::vector<String> files;
    
    if (!sdCardReady) {
        // Эмуляция файлов для теста
        files.push_back("test_circle.nc");
        files.push_back("rectangle.gcode");
        files.push_back("spiral.nc");
        return files;
    }
    
    // Реальная работа с SD картой
    File root = SD_MMC.open("/gcode");
    if(!root) return files;
    
    File file = root.openNextFile();
    while(file) {
        if(!file.isDirectory()) {
            String filename = String(file.name());
            // Убираем путь из имени файла
            if(filename.startsWith("/gcode/")) {
                filename = filename.substring(7);
            }
            files.push_back(filename);
        }
        file = root.openNextFile();
    }
    
    return files;
}

bool FileManager::fileExists(const String& filename) {
    if(!sdCardReady) return false;
    return SD_MMC.exists("/gcode/" + filename);
}

String FileManager::readFile(const String& filename) {
    if(!sdCardReady) return "";
    
    File file = SD_MMC.open("/gcode/" + filename);
    if(!file) return "";
    
    String content = file.readString();
    file.close();
    return content;
}

bool FileManager::writeFile(const String& filename, const String& content) {
    if(!sdCardReady) return false;
    
    File file = SD_MMC.open("/gcode/" + filename, FILE_WRITE);
    if(!file) return false;
    
    size_t bytesWritten = file.print(content);
    file.close();
    
    Serial.println("📁 File saved: " + filename + " (" + String(bytesWritten) + " bytes)");
    return bytesWritten > 0;
}

bool FileManager::deleteFile(const String& filename) {
    if(!sdCardReady) return false;
    
    bool success = SD_MMC.remove("/gcode/" + filename);
    if(success) {
        Serial.println("🗑️ File deleted: " + filename);
    }
    return success;
}

void FileManager::runGCodeFile(const String& filename) {
    Serial.println("▶️ Running G-code file: " + filename);
    
    String content = readFile(filename);
    if(content.length() > 0) {
        // Здесь будет парсинг и выполнение G-code
        Serial.println("📄 File content (" + String(content.length()) + " chars):");
        Serial.println(content);
    } else {
        Serial.println("❌ File is empty or not found: " + filename);
    }
}

// Для Web UI
String FileManager::getFileListHTML() {
    auto files = listFiles();
    String html = "";
    
    for(const String& file : files) {
        html += "<div class=\"file-item\" onclick=\"selectFile('" + file + "')\">";
        html += "📄 " + file;
        html += "</div>";
    }
    
    return html;
}

size_t FileManager::getFileSize(const String& filename) {
    if(!sdCardReady) return 0;
    
    File file = SD_MMC.open("/gcode/" + filename);
    if(!file) return 0;
    
    size_t size = file.size();
    file.close();
    return size;
}