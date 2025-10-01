#include "WebInterface.h"
#include "Config.h"
#include "StepperControl.h"
#include "PlasmaControl.h"
#include "THC_System.h"
#include "FileManager.h"
#include "MaterialPresets.h"

WebServer WebInterface::server(80);
WebSocketsServer WebInterface::webSocket(81);

// Структура для состояния G-code выполнения
struct GCodeState {
    String filename;
    int currentLine;
    int totalLines;
    bool isRunning;
    bool isPaused;
    String buffer;
    long startTime;
    int retryCount;
};

GCodeState gcodeState = {"", 0, 0, false, false, "", 0, 0};
const int MAX_RETRY_COUNT = 3;
String currentGCodeLine = "";
int currentLineNumber = 0;
bool isGCodeRunning = false;

void WebInterface::init() {
  // Запуск WiFi
  WiFi.softAP(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("📡 WiFi AP started: %s\n", WiFi.softAPIP().toString().c_str());

  // Маршруты обработки файлов:
  server.on(
    "/api/files/upload", HTTP_POST, []() {
      WebInterface::server.send(200, "text/plain", "Upload complete");
    },
    WebInterface::handleFileUpload);

  server.on("/api/files/list", HTTP_GET, WebInterface::handleFileList);
  server.on("/api/files/delete", HTTP_POST, WebInterface::handleFileDelete);
  server.on("/api/files/run", HTTP_POST, WebInterface::handleFileRun);

  // Новый маршрут для G-code команд
  server.on("/api/gcode", HTTP_POST, WebInterface::handleGCodeCommand);

  // Настройка HTTP маршрутов
  server.on("/", WebInterface::handleRoot);
  server.on("/api/status", HTTP_GET, WebInterface::handleAPIStatus);
  server.on("/api/files", HTTP_GET, WebInterface::handleAPIFiles);

  // Добавляем обработчик для обнуления координат через HTTP
  server.on("/api/set_zero", HTTP_POST, WebInterface::handleSetZero);

  server.begin();

  // Запуск WebSocket
  webSocket.begin();
  webSocket.onEvent(WebInterface::handleWebSocket);

  Serial.println("✅ Web Interface initialized");
}

void WebInterface::handleClient() {
  server.handleClient();
  webSocket.loop();
}

void WebInterface::handleRoot() {
  server.send(200, "text/html", getMainPage());
}

void WebInterface::handleAPIStatus() {
  DynamicJsonDocument doc(1024);
  doc["status"] = "ok";
  doc["version"] = "1.0.0";
  doc["uptime"] = millis() / 1000;
  doc["freeHeap"] = ESP.getFreeHeap();
  doc["wifiClients"] = WiFi.softAPgetStationNum();

  // Добавляем статус SD карты и THC
  doc["sdCardStatus"] = FileManager::isSDCardMounted() ? "mounted" : "error";
  doc["thcEnabled"] = THC_System::isEnabled();
  doc["thcActive"] = THC_System::isActive();
  doc["targetVoltage"] = THC_System::getTargetVoltage();
  doc["arcVoltage"] = PlasmaControl::getArcVoltage();
  doc["plasmaActive"] = PlasmaControl::isActive();

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void WebInterface::handleAPIFiles() {
  if (!FileManager::isSDCardMounted()) {
    server.send(500, "application/json", "{\"error\":\"SD card not mounted\"}");
    return;
  }

  auto files = FileManager::listFiles();
  DynamicJsonDocument doc(2048);
  JsonArray filesArray = doc.to<JsonArray>();

  for (const String& file : files) {
    filesArray.add(file);
  }

  doc["sdCardStatus"] = "mounted";
  doc["totalFiles"] = files.size();

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void WebInterface::handleSetZero() {
  if (WebInterface::server.hasArg("axis")) {
    String axis = WebInterface::server.arg("axis");

    if (axis == "X") {
      StepperControl::setCurrentX(0);
    } else if (axis == "Y") {
      StepperControl::setCurrentY(0);
    } else if (axis == "Z") {
      StepperControl::setCurrentZ(0);
    } else if (axis == "XY") {
      StepperControl::setCurrentX(0);
      StepperControl::setCurrentY(0);
    } else if (axis == "XYZ") {
      StepperControl::setCurrentX(0);
      StepperControl::setCurrentY(0);
      StepperControl::setCurrentZ(0);
    }

    Serial.printf("🎯 Координаты обнулены для оси: %s\n", axis.c_str());
    
    DynamicJsonDocument responseDoc(256);
    responseDoc["status"] = "ok";
    responseDoc["axis"] = axis;
    responseDoc["x"] = StepperControl::getCurrentX();
    responseDoc["y"] = StepperControl::getCurrentY();
    responseDoc["z"] = StepperControl::getCurrentZ();
    
    String response;
    serializeJson(responseDoc, response);
    WebInterface::server.send(200, "application/json", response);
  } else {
    WebInterface::server.send(400, "application/json", "{\"error\":\"Missing axis parameter\"}");
  }
}

void WebInterface::handleGCodeCommand() {
  if (server.hasArg("command")) {
    String gcodeCommand = server.arg("command");
    gcodeCommand.trim();
    
    Serial.printf("📝 Received G-code command: %s\n", gcodeCommand.c_str());
    
    // Отправляем ответ через WebSocket
    DynamicJsonDocument doc(512);
    doc["type"] = "gcode_response";
    doc["command"] = gcodeCommand;
    doc["response"] = "ok";
    doc["timestamp"] = millis();
    
    String response;
    serializeJson(doc, response);
    webSocket.broadcastTXT(response);
    
    // Обработка G-code команд
    processGCodeCommand(gcodeCommand);
    
    server.send(200, "application/json", "{\"status\":\"ok\", \"command\":\"" + gcodeCommand + "\"}");
  } else {
    server.send(400, "application/json", "{\"error\":\"No command provided\"}");
  }
}

void WebInterface::processGCodeCommand(String command) {
  // Базовая обработка G-code команд
  command.toUpperCase();
  
  if (command == "G28") {
    // Home all axes
    StepperControl::homeAll();
    sendGCodeResponse("Homing all axes...");
  } 
  else if (command.startsWith("G0") || command.startsWith("G1")) {
    // Linear movement
    sendGCodeResponse("Linear movement: " + command);
  }
  else if (command.startsWith("G90")) {
    // Absolute positioning
    sendGCodeResponse("Absolute positioning");
  }
  else if (command.startsWith("G91")) {
    // Relative positioning
    sendGCodeResponse("Relative positioning");
  }
  else if (command == "M3") {
    // Plasma on
    PlasmaControl::startPlasma();
    sendGCodeResponse("Plasma started");
  }
  else if (command == "M5") {
    // Plasma off
    PlasmaControl::stopPlasma();
    sendGCodeResponse("Plasma stopped");
  }
  else {
    sendGCodeResponse("Unknown command: " + command);
  }
}

void WebInterface::sendGCodeResponse(const String& response) {
  DynamicJsonDocument doc(512);
  doc["type"] = "gcode_response";
  doc["response"] = response;
  doc["timestamp"] = millis();
  
  String jsonResponse;
  serializeJson(doc, jsonResponse);
  webSocket.broadcastTXT(jsonResponse);
}

void WebInterface::sendGCodeState(const String& state) {
  DynamicJsonDocument doc(512);
  doc["type"] = "gcode_state";
  doc["state"] = state;
  doc["currentLine"] = currentLineNumber;
  doc["currentCommand"] = currentGCodeLine;
  doc["isRunning"] = isGCodeRunning;
  doc["timestamp"] = millis();
  
  String jsonResponse;
  serializeJson(doc, jsonResponse);
  webSocket.broadcastTXT(jsonResponse);
}

// G-code функции восстановления
void WebInterface::startGCodeFromLine(const String& filename, int startLine) {
    if (!FileManager::isSDCardMounted()) {
        sendGCodeResponse("❌ SD карта не доступна");
        return;
    }
    
    File file = SD_MMC.open(filename, "r");
    if (!file) {
        sendGCodeResponse("❌ Не могу открыть файл: " + filename);
        return;
    }
    
    // Считаем общее количество строк
    int totalLines = 0;
    while (file.available()) {
        file.readStringUntil('\n');
        totalLines++;
    }
    file.close();
    
    if (startLine > totalLines) {
        sendGCodeResponse("❌ Строка " + String(startLine) + " за пределами файла");
        return;
    }
    
    gcodeState.filename = filename;
    gcodeState.currentLine = startLine;
    gcodeState.totalLines = totalLines;
    gcodeState.isRunning = true;
    gcodeState.isPaused = false;
    gcodeState.startTime = millis();
    gcodeState.retryCount = 0;
    
    sendGCodeResponse("▶️ Запуск G-code с строки " + String(startLine) + " из " + String(totalLines));
    sendGCodeState("RUNNING_FROM_LINE");
    
    // Отправляем прогресс
    DynamicJsonDocument progressDoc(512);
    progressDoc["type"] = "gcode_progress";
    progressDoc["filename"] = gcodeState.filename;
    progressDoc["currentLine"] = gcodeState.currentLine;
    progressDoc["totalLines"] = gcodeState.totalLines;
    progressDoc["isRunning"] = gcodeState.isRunning;
    progressDoc["isPaused"] = gcodeState.isPaused;
    progressDoc["progress"] = gcodeState.totalLines > 0 ? 
        (float)gcodeState.currentLine / gcodeState.totalLines * 100 : 0;
    progressDoc["retryCount"] = gcodeState.retryCount;
    
    String progressResponse;
    serializeJson(progressDoc, progressResponse);
    webSocket.broadcastTXT(progressResponse);
    
    // Запускаем обработку
    processNextGCodeLine();
}

void WebInterface::processNextGCodeLine() {
    if (!gcodeState.isRunning || gcodeState.isPaused) {
        return;
    }
    
    File file = SD_MMC.open(gcodeState.filename, "r");
    if (!file) {
        sendGCodeResponse("❌ Ошибка чтения файла: " + gcodeState.filename);
        gcodeState.isRunning = false;
        sendGCodeState("ERROR");
        return;
    }
    
    // Пропускаем строки до нужной позиции
    for (int i = 0; i < gcodeState.currentLine; i++) {
        if (!file.available()) break;
        file.readStringUntil('\n');
    }
    
    // Читаем текущую строку
    if (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        
        if (line.length() > 0 && !line.startsWith(";") && !line.startsWith("(")) {
            gcodeState.buffer = line;
            currentGCodeLine = line;
            currentLineNumber = gcodeState.currentLine + 1;
            
            sendGCodeResponse("📝 Строка " + String(currentLineNumber) + ": " + line);
            executeGCodeLine(line);
        } else {
            // Пропускаем комментарии и пустые строки
            gcodeState.currentLine++;
            file.close();
            processNextGCodeLine();
        }
    } else {
        // Конец файла
        file.close();
        gcodeState.isRunning = false;
        sendGCodeResponse("✅ G-code выполнен успешно!");
        sendGCodeState("COMPLETED");
    }
}

void WebInterface::executeGCodeLine(const String& line) {
    // Симуляция выполнения G-code
    sendGCodeResponse("⚡ Выполнение: " + line);
    
    // Здесь должна быть реальная логика выполнения G-code
    // Пока просто задержка для имитации
    delay(100);
    
    // После успешного выполнения переходим к следующей строке
    gcodeState.currentLine++;
    
    // Обновляем прогресс
    DynamicJsonDocument progressDoc(512);
    progressDoc["type"] = "gcode_progress";
    progressDoc["filename"] = gcodeState.filename;
    progressDoc["currentLine"] = gcodeState.currentLine;
    progressDoc["totalLines"] = gcodeState.totalLines;
    progressDoc["isRunning"] = gcodeState.isRunning;
    progressDoc["isPaused"] = gcodeState.isPaused;
    progressDoc["progress"] = gcodeState.totalLines > 0 ? 
        (float)gcodeState.currentLine / gcodeState.totalLines * 100 : 0;
    progressDoc["retryCount"] = gcodeState.retryCount;
    
    String progressResponse;
    serializeJson(progressDoc, progressResponse);
    webSocket.broadcastTXT(progressResponse);
    
    processNextGCodeLine();
}

void WebInterface::handlePlasmaOutage() {
    if (gcodeState.isRunning) {
        sendGCodeResponse("⚠️ ОБНАРУЖЕН СБОЙ ПЛАЗМЫ! Аварийная остановка...");
        
        // Останавливаем плазму
        PlasmaControl::stopPlasma();
        
        // Сохраняем текущее состояние для восстановления
        int recoveryLine = gcodeState.currentLine - 3; // Возвращаемся на 3 строки назад
        if (recoveryLine < 0) recoveryLine = 0;
        
        sendGCodeResponse("🔄 Подготовка к восстановлению с строки " + String(recoveryLine + 1));
        
        // Пауза перед восстановлением
        gcodeState.isPaused = true;
        sendGCodeState("PLASMA_OUTAGE");
        
        // Обновляем прогресс
        DynamicJsonDocument progressDoc(512);
        progressDoc["type"] = "gcode_progress";
        progressDoc["filename"] = gcodeState.filename;
        progressDoc["currentLine"] = gcodeState.currentLine;
        progressDoc["totalLines"] = gcodeState.totalLines;
        progressDoc["isRunning"] = gcodeState.isRunning;
        progressDoc["isPaused"] = gcodeState.isPaused;
        progressDoc["progress"] = gcodeState.totalLines > 0 ? 
            (float)gcodeState.currentLine / gcodeState.totalLines * 100 : 0;
        progressDoc["retryCount"] = gcodeState.retryCount;
        
        String progressResponse;
        serializeJson(progressDoc, progressResponse);
        webSocket.broadcastTXT(progressResponse);
        
        // Ждем восстановления дуги
        sendGCodeResponse("⏳ Ожидание восстановления плазмы...");
    }
}

void WebInterface::resumeAfterPlasmaRecovery() {
    if (gcodeState.isPaused && gcodeState.retryCount < MAX_RETRY_COUNT) {
        gcodeState.retryCount++;
        sendGCodeResponse("🔄 Попытка восстановления #" + String(gcodeState.retryCount));
        
        // Возвращаемся на несколько строк назад для повторного прогрева
        int restartLine = gcodeState.currentLine - 2;
        if (restartLine < 0) restartLine = 0;
        
        gcodeState.currentLine = restartLine;
        gcodeState.isPaused = false;
        
        sendGCodeResponse("▶️ Возобновление с строки " + String(restartLine + 1));
        sendGCodeState("RECOVERY");
        
        // Обновляем прогресс
        DynamicJsonDocument progressDoc(512);
        progressDoc["type"] = "gcode_progress";
        progressDoc["filename"] = gcodeState.filename;
        progressDoc["currentLine"] = gcodeState.currentLine;
        progressDoc["totalLines"] = gcodeState.totalLines;
        progressDoc["isRunning"] = gcodeState.isRunning;
        progressDoc["isPaused"] = gcodeState.isPaused;
        progressDoc["progress"] = gcodeState.totalLines > 0 ? 
            (float)gcodeState.currentLine / gcodeState.totalLines * 100 : 0;
        progressDoc["retryCount"] = gcodeState.retryCount;
        
        String progressResponse;
        serializeJson(progressDoc, progressResponse);
        webSocket.broadcastTXT(progressResponse);
        
        processNextGCodeLine();
    } else if (gcodeState.retryCount >= MAX_RETRY_COUNT) {
        sendGCodeResponse("❌ Превышено количество попыток восстановления. Остановка.");
        gcodeState.isRunning = false;
        sendGCodeState("FAILED");
    }
}

void WebInterface::handleFileUpload() {
  HTTPUpload& upload = server.upload();
  static File uploadFile;

  if (upload.status == UPLOAD_FILE_START) {
    if (!FileManager::isSDCardMounted()) {
      server.send(500, "text/plain", "SD card not mounted");
      return;
    }
    
    String filename = "/" + upload.filename;
    uploadFile = SD_MMC.open(filename, FILE_WRITE);
    if (!uploadFile) {
      server.send(500, "text/plain", "Ошибка создания файла");
      return;
    }
    Serial.println("📤 Начало загрузки: " + upload.filename);
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFile) {
      uploadFile.write(upload.buf, upload.currentSize);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) {
      uploadFile.close();
      Serial.println("✅ Загрузка завершена: " + upload.filename);
      server.send(200, "text/plain", "Файл загружен: " + upload.filename);
    }
  }
}

void WebInterface::handleFileList() {
  if (!FileManager::isSDCardMounted()) {
    server.send(500, "application/json", "{\"error\":\"SD card not mounted\"}");
    return;
  }

  auto files = FileManager::listFiles();
  DynamicJsonDocument doc(2048);
  JsonArray filesArray = doc.to<JsonArray>();

  for (const String& file : files) {
    filesArray.add(file);
  }

  doc["sdCardStatus"] = "mounted";
  doc["totalFiles"] = files.size();

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void WebInterface::handleFileDelete() {
  if (!FileManager::isSDCardMounted()) {
    server.send(500, "text/plain", "SD card not mounted");
    return;
  }

  String filename = server.arg("filename");
  if (FileManager::deleteFile(filename)) {
    server.send(200, "text/plain", "Файл удален: " + filename);
  } else {
    server.send(500, "text/plain", "Ошибка удаления файла");
  }
}

void WebInterface::handleFileRun() {
  String filename = server.arg("filename");
  if (FileManager::runGCodeFile(filename)) {
    server.send(200, "text/plain", "Запуск файла: " + filename);
  } else {
    server.send(500, "text/plain", "Ошибка запуска файла");
  }
}

void WebInterface::handleWebSocket(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      Serial.printf("[%u] WebSocket disconnected\n", num);
      break;
    case WStype_CONNECTED:
      {
        IPAddress ip = webSocket.remoteIP(num);
        Serial.printf("[%u] WebSocket connected from %d.%d.%d.%d\n", num, ip[0], ip[1], ip[2], ip[3]);
        sendSystemState();
      }
      break;
    case WStype_TEXT:
      {
        String message = String((char*)payload);
        processWebSocketCommand(message);
      }
      break;
  }
}

void WebInterface::sendSystemState() {
  DynamicJsonDocument doc(1024);

  doc["type"] = "status";
  doc["arcVoltage"] = PlasmaControl::getArcVoltage();
  doc["arcOK"] = PlasmaControl::isArcOK();
  doc["plasmaActive"] = PlasmaControl::isActive();
  doc["currentHeight"] = StepperControl::getCurrentZ();
  doc["targetVoltage"] = THC_System::getTargetVoltage();
  doc["thcEnabled"] = THC_System::isEnabled();
  doc["thcActive"] = THC_System::isActive();
  doc["machineState"] = "Ready";
  doc["x"] = StepperControl::getCurrentX();
  doc["y"] = StepperControl::getCurrentY();
  doc["z"] = StepperControl::getCurrentZ();
  
  // Добавляем статус SD карты
  doc["sdCardStatus"] = FileManager::isSDCardMounted() ? "mounted" : "error";
  doc["sdCardFiles"] = FileManager::getFileCount();

  String jsonString;
  serializeJson(doc, jsonString);
  webSocket.broadcastTXT(jsonString);
}

void WebInterface::processWebSocketCommand(String message) {
  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, message);

  if (error) {
    Serial.print("JSON parsing failed: ");
    Serial.println(error.c_str());
    return;
  }

  String command = doc["command"];
  Serial.println("Received command: " + command);

  if (command == "plasma_on") {
    PlasmaControl::startPlasma();
  } else if (command == "plasma_off") {
    PlasmaControl::stopPlasma();
  } else if (command == "jog") {
    String direction = doc["direction"];
    float distance = doc["distance"];
    StepperControl::jog(direction, distance);
  } else if (command == "home_all") {
    StepperControl::homeAll();
  } else if (command == "emergency_stop") {
    Serial.println("🛑 EMERGENCY STOP");
    PlasmaControl::stopPlasma();
    THC_System::disable();
    isGCodeRunning = false;
    gcodeState.isRunning = false;
    sendGCodeState("EMERGENCY_STOP");
  } else if (command == "toggle_thc") {
    THC_System::toggle();
  } else if (command == "thc_settings") {
    float voltage = doc["voltage"];
    int deadZone = doc["deadZone"];
    THC_System::setTargetVoltage(voltage);
    Serial.printf("THC settings: voltage=%.1f, deadZone=%d\n", voltage, deadZone);
  } else if (command == "load_preset") {
    String presetName = doc["presetName"];
    Serial.printf("📥 Загружаем пресет: %s\n", presetName.c_str());
  } else if (command == "create_preset") {
    String name = doc["name"];
    float voltage = doc["voltage"];
    float deadZone = doc["deadZone"];
    float speed = doc["speed"];
    float pierceHeight = doc["pierceHeight"];
    float cutHeight = doc["cutHeight"];
    float pierceDelay = doc["pierceDelay"];
    Serial.printf("➕ Создание пресета: %s (%.1fV, %.0fmm/min)\n", name.c_str(), voltage, speed);
  } else if (command == "update_preset") {
    String oldName = doc["oldName"];
    String newName = doc["newData"]["name"];
    Serial.printf("✏️ Обновление пресета: %s -> %s\n", oldName.c_str(), newName.c_str());
  } else if (command == "delete_preset") {
    String presetName = doc["presetName"];
    Serial.printf("🗑️ Удаление пресета: %s\n", presetName.c_str());
  } else if (command == "set_zero") {
    String axis = doc["axis"];
    Serial.printf("🎯 Установка нуля для оси: %s\n", axis.c_str());

    if (axis == "X") {
      StepperControl::setCurrentX(0);
    } else if (axis == "Y") {
      StepperControl::setCurrentY(0);
    } else if (axis == "Z") {
      StepperControl::setCurrentZ(0);
    } else if (axis == "XY") {
      StepperControl::setCurrentX(0);
      StepperControl::setCurrentY(0);
    } else if (axis == "XYZ") {
      StepperControl::setCurrentX(0);
      StepperControl::setCurrentY(0);
      StepperControl::setCurrentZ(0);
    }

    // Отправляем обновленные координаты
    DynamicJsonDocument responseDoc(512);
    responseDoc["type"] = "status";
    responseDoc["x"] = StepperControl::getCurrentX();
    responseDoc["y"] = StepperControl::getCurrentY();
    responseDoc["z"] = StepperControl::getCurrentZ();

    String response;
    serializeJson(responseDoc, response);
    webSocket.broadcastTXT(response);

  } else if (command == "set_current_as_zero") {
    Serial.println("🎯 Установка текущей позиции как нулевой");
    StepperControl::setCurrentPositionAsZero();

    DynamicJsonDocument responseDoc(512);
    responseDoc["type"] = "status";
    responseDoc["x"] = StepperControl::getCurrentX();
    responseDoc["y"] = StepperControl::getCurrentY();
    responseDoc["z"] = StepperControl::getCurrentZ();

    String response;
    serializeJson(responseDoc, response);
    webSocket.broadcastTXT(response);
  } else if (command == "get_file_list") {
    // Отправляем список файлов через WebSocket
    auto files = FileManager::listFiles();
    DynamicJsonDocument fileDoc(2048);
    fileDoc["type"] = "file_list";
    JsonArray filesArray = fileDoc.to<JsonArray>();
    for (const String& file : files) {
      filesArray.add(file);
    }
    fileDoc["sdCardStatus"] = FileManager::isSDCardMounted() ? "mounted" : "error";
    fileDoc["totalFiles"] = files.size();
    
    String fileResponse;
    serializeJson(fileDoc, fileResponse);
    webSocket.broadcastTXT(fileResponse);
  } else if (command == "send_gcode") {
    // Обработка G-code команды из WebSocket
    String gcode = doc["gcode"];
    processGCodeCommand(gcode);
  } else if (command == "start_gcode_file") {
    // Запуск G-code файла
    String filename = doc["filename"];
    isGCodeRunning = true;
    sendGCodeState("STARTED");
    Serial.printf("▶️ Starting G-code file: %s\n", filename.c_str());
  } else if (command == "pause_gcode") {
    // Пауза выполнения G-code
    isGCodeRunning = false;
    gcodeState.isPaused = true;
    sendGCodeState("PAUSED");
    Serial.println("⏸️ G-code execution paused");
  } else if (command == "resume_gcode") {
    // Продолжение выполнения G-code
    isGCodeRunning = true;
    gcodeState.isPaused = false;
    sendGCodeState("RUNNING");
    Serial.println("▶️ G-code execution resumed");
  } else if (command == "stop_gcode") {
    // Остановка выполнения G-code
    isGCodeRunning = false;
    gcodeState.isRunning = false;
    sendGCodeState("STOPPED");
    Serial.println("⏹️ G-code execution stopped");
  } else if (command == "start_gcode_from_line") {
    // Запуск G-code с определенной строки
    String filename = doc["filename"];
    int startLine = doc["startLine"];
    startGCodeFromLine(filename, startLine);
  } else if (command == "handle_plasma_outage") {
    // Обработка сбоя плазмы
    handlePlasmaOutage();
  } else if (command == "resume_after_recovery") {
    // Продолжение после восстановления
    resumeAfterPlasmaRecovery();
  } else if (command == "get_gcode_progress") {
    // Получение прогресса выполнения
    DynamicJsonDocument progressDoc(512);
    progressDoc["type"] = "gcode_progress";
    progressDoc["filename"] = gcodeState.filename;
    progressDoc["currentLine"] = gcodeState.currentLine;
    progressDoc["totalLines"] = gcodeState.totalLines;
    progressDoc["isRunning"] = gcodeState.isRunning;
    progressDoc["isPaused"] = gcodeState.isPaused;
    progressDoc["progress"] = gcodeState.totalLines > 0 ? 
        (float)gcodeState.currentLine / gcodeState.totalLines * 100 : 0;
    progressDoc["retryCount"] = gcodeState.retryCount;
    
    String progressResponse;
    serializeJson(progressDoc, progressResponse);
    webSocket.broadcastTXT(progressResponse);
  }

  sendSystemState();
}

String WebInterface::getMainPage() {
  // Полный HTML код из следующего сообщения
  // [Здесь должен быть полный HTML код]
  return R"rawliteral(
<!DOCTYPE html>
<html lang="ru">
<head>
    <meta charset="UTF-8">
    <title>ЧПУ Плазма</title>
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <style>
        :root {
            --primary: #2196F3;
            --secondary: #FF9800;
            --success: #4CAF50;
            --danger: #f44336;
            --dark: #1a1a1a;
            --darker: #2a2a2a;
            --text: #ffffff;
        }
        
        * { margin: 0; padding: 0; box-sizing: border-box; }
        
        body {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            background: var(--dark);
            color: var(--text);
            overflow-x: hidden;
        }
        
        .app-container {
            display: grid;
            grid-template-rows: auto 1fr auto;
            height: 100vh;
            gap: 10px;
            padding: 10px;
        }
        
        /* Header */
        .header {
            background: var(--darker);
            padding: 15px 20px;
            border-radius: 10px;
            display: flex;
            justify-content: space-between;
            align-items: center;
            box-shadow: 0 2px 10px rgba(0,0,0,0.3);
        }
        
        .logo { display: flex; align-items: center; gap: 10px; }
        .logo h1 { color: var(--success); font-size: 1.5em; }
        
        .status-bar {
            display: flex;
            gap: 15px;
            align-items: center;
        }
        
        .status-item {
            padding: 5px 10px;
            border-radius: 15px;
            background: #333;
            font-size: 0.9em;
        }
        
        /* Main Content */
        .main-content {
            display: grid;
            grid-template-columns: 1fr 1fr 1fr;
            gap: 10px;
            height: 100%;
        }
        
        .panel {
            background: var(--darker);
            border-radius: 10px;
            padding: 15px;
            border: 1px solid #333;
            box-shadow: 0 2px 5px rgba(0,0,0,0.2);
            overflow-y: auto;
        }
        
        .panel h3 {
            color: var(--primary);
            margin-bottom: 15px;
            border-bottom: 1px solid #333;
            padding-bottom: 5px;
        }
        
        /* Monitoring Panel */
        .monitor-grid {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 8px;
        }
        
        .monitor-item {
            background: #333;
            padding: 10px;
            border-radius: 5px;
            text-align: center;
        }
        
        .monitor-label {
            font-size: 0.8em;
            color: #aaa;
            margin-bottom: 5px;
        }
        
        .monitor-value {
            font-size: 1.2em;
            font-weight: bold;
            color: var(--success);
        }
        
        .position-display {
            margin-top: 15px;
            background: #333;
            padding: 10px;
            border-radius: 5px;
        }
        
        /* Jog Control */
        .jog-layout {
            display: grid;
            grid-template-columns: auto auto auto;
            gap: 15px;
            align-items: start;
        }
        
        .zero-controls-column {
            display: flex;
            flex-direction: column;
            gap: 8px;
        }
        
        .jog-center-column {
            display: flex;
            flex-direction: column;
            align-items: center;
            gap: 10px;
        }
        
        .grid-controls-column {
            display: flex;
            flex-direction: column;
            gap: 8px;
        }
        
        .xy-jog-container {
            display: flex;
            align-items: center;
            gap: 10px;
        }
        
        .xy-jog-grid {
            display: grid;
            grid-template-areas: 
                ". y-plus ."
                "x-minus center x-plus"
                ". y-minus .";
            grid-template-columns: 60px 60px 60px;
            grid-template-rows: 60px 60px 60px;
            gap: 5px;
        }
        
        .z-jog-buttons {
            display: flex;
            flex-direction: column;
            gap: 5px;
        }
        
        .jog-btn {
            background: var(--primary);
            color: white;
            border: none;
            border-radius: 8px;
            font-size: 1.1em;
            font-weight: bold;
            cursor: pointer;
            transition: all 0.2s;
            display: flex;
            align-items: center;
            justify-content: center;
        }
        
        .jog-btn:hover {
            transform: scale(1.05);
            box-shadow: 0 4px 8px rgba(0,0,0,0.3);
        }
        
        .jog-btn:active {
            transform: scale(0.95);
        }
        
        /* Расположение кнопок XY */
        .btn-y-plus { grid-area: y-plus; background: #FF9800; }
        .btn-y-minus { grid-area: y-minus; background: #FF9800; }
        .btn-x-plus { grid-area: x-plus; background: #2196F3; }
        .btn-x-minus { grid-area: x-minus; background: #2196F3; }
        .btn-center { 
            grid-area: center; 
            background: #666; 
            font-size: 0.7em;
            cursor: default;
        }
        .btn-center:hover { transform: none; box-shadow: none; }
        
        /* Кнопки Z */
        .btn-z-plus { 
            background: #4CAF50; 
            width: 50px;
            height: 60px;
            font-size: 1.2em;
        }
        .btn-z-minus { 
            background: #4CAF50; 
            width: 50px;
            height: 60px;
            font-size: 1.2em;
        }
        
        .btn {
            background: var(--primary);
            color: white;
            border: none;
            padding: 12px 15px;
            border-radius: 5px;
            cursor: pointer;
            transition: all 0.3s;
            font-size: 0.9em;
        }
        
        .btn:hover {
            transform: translateY(-1px);
            box-shadow: 0 3px 8px rgba(0,0,0,0.3);
        }
        
        .btn-success { background: var(--success); }
        .btn-danger { background: var(--danger); }
        .btn-warning { background: var(--secondary); }
        .btn-info { background: #00BCD4; }
        
        /* Кнопки обнуления и сетки */
        .zero-btn {
            background: #FF9800;
            color: white;
            border: none;
            padding: 10px 8px;
            border-radius: 5px;
            cursor: pointer;
            font-size: 0.75em;
            text-align: center;
        }
        
        .zero-btn:hover {
            background: #F57C00;
        }
        
        .grid-btn {
            background: #555;
            color: white;
            border: none;
            padding: 10px 8px;
            border-radius: 5px;
            cursor: pointer;
            font-size: 0.75em;
            text-align: center;
        }
        
        .grid-btn.active {
            background: var(--primary);
            font-weight: bold;
        }
        
        .grid-btn:hover {
            background: #1976D2;
        }
        
        /* THC Controls */
        .slider-group {
            margin: 15px 0;
        }
        
        .slider-group label {
            display: block;
            margin-bottom: 5px;
            color: #ccc;
        }
        
        .slider-container {
            display: flex;
            align-items: center;
            gap: 10px;
        }
        
        input[type="range"] {
            flex: 1;
            height: 6px;
            background: #333;
            border-radius: 3px;
            outline: none;
        }
        
        .slider-value {
            min-width: 60px;
            text-align: right;
            font-weight: bold;
        }
        
        /* File Manager Styles */
        .file-item {
            transition: background 0.2s;
            border-radius: 3px;
            padding: 8px;
            margin: 2px 0;
            cursor: pointer;
            border-bottom: 1px solid #444;
        }
        
        .file-item:hover {
            background: #3a3a3a !important;
        }
        
        .file-item.selected {
            background: #555 !important;
        }
        
        .file-upload-section {
            margin-bottom: 15px;
        }
        
        .file-controls {
            display: flex;
            gap: 10px;
            margin-top: 10px;
        }
        
        /* Presets Editor */
        .preset-editor { 
            background: #333; 
            padding: 15px; 
            border-radius: 5px; 
            margin-top: 10px; 
        }
        
        .preset-field { 
            display: grid; 
            grid-template-columns: 1fr 1fr; 
            gap: 10px; 
            margin: 8px 0; 
            align-items: center; 
        }
        
        .preset-field input { 
            background: #444; 
            color: white; 
            border: 1px solid #555; 
            padding: 8px; 
            border-radius: 3px; 
        }
        
        .modal { 
            display: none; 
            position: fixed; 
            top: 0; 
            left: 0; 
            width: 100%; 
            height: 100%; 
            background: rgba(0,0,0,0.8); 
            z-index: 1000; 
        }
        
        .modal-content { 
            background: #2a2a2a; 
            margin: 10% auto; 
            padding: 20px; 
            border-radius: 10px; 
            width: 90%; 
            max-width: 500px; 
        }

        /* Connection Status */
        .connection-status {
            text-align: center;
            padding: 10px;
            border-radius: 5px;
            margin: 10px 0;
            background: #333;
        }
        
        /* G-code Panel Styles */
        .gcode-panel {
            grid-column: span 2;
        }
        
        #gcodeConsole {
            line-height: 1.4;
        }
        
        #gcodeConsole div {
            margin-bottom: 2px;
            padding: 2px 5px;
            border-radius: 2px;
        }
        
        #gcodeConsole div:nth-child(odd) {
            background: rgba(255,255,255,0.05);
        }
        
        #gcodeConsole .command {
            color: #ffff00;
        }
        
        #gcodeConsole .response {
            color: #00ffff;
        }
        
        #gcodeConsole .error {
            color: #ff4444;
            background: rgba(255,0,0,0.1);
        }
        
        #gcodeConsole .warning {
            color: #ffaa00;
        }
        
        #gcodeHistory div {
            padding: 4px;
            margin: 2px 0;
            background: #444;
            border-radius: 3px;
            border-left: 3px solid #2196F3;
        }
        
        #gcodeHistory div.success {
            border-left-color: #4CAF50;
        }
        
        #gcodeHistory div.error {
            border-left-color: #f44336;
        }
        
        /* Recovery Panel Styles */
        .recovery-panel {
            grid-column: span 2;
        }
        
        #recoveryLog div {
            margin-bottom: 2px;
            padding: 3px 5px;
            border-radius: 2px;
        }
        
        #recoveryLog .warning {
            color: #ffaa00;
            background: rgba(255,170,0,0.1);
        }
        
        #recoveryLog .error {
            color: #ff4444;
            background: rgba(255,0,0,0.1);
        }
        
        #recoveryLog .success {
            color: #44ff44;
            background: rgba(0,255,0,0.1);
        }
        
        #recoveryLog .info {
            color: #44aaff;
        }
        
        /* Responsive */
        @media (max-width: 1200px) {
            .main-content {
                grid-template-columns: 1fr;
                grid-template-rows: auto auto auto;
            }
            
            .jog-layout {
                grid-template-columns: 1fr;
                gap: 10px;
            }
            
            .zero-controls-column,
            .grid-controls-column {
                flex-direction: row;
                justify-content: center;
            }
            
            .gcode-panel,
            .recovery-panel {
                grid-column: 1;
            }
        }
    </style>
</head>
<body>
    <div class="app-container">
        <!-- Header -->
        <header class="header">
            <div class="logo">
                <h1>⚡ ЧПУ Контроллер (плазма)</h1>
            </div>
            <div class="status-bar">
                <div class="status-item" id="connectionStatus">🟢 Подключено</div>
                <div class="status-item" id="machineStatus">Готов</div>	
                <div class="status-item" id="thcStatus">THC: Выкл</div>
                <div class="status-item" id="plasmaStatus">Плазма: Выкл</div>
                <div class="status-item" id="sdCardStatus">SD Card: Проверка...</div>
            </div>
        </header>

        <!-- Main Content -->
        <div class="main-content">
            <!-- Monitoring Panel -->
            <div class="panel monitoring-panel">
                <h3>📊 Мониторинг системы</h3>
                
                <div class="monitor-grid">
                    <div class="monitor-item">
                        <div class="monitor-label">Напряжение дуги</div>
                        <div class="monitor-value" id="arcVoltage">0.0 V</div>
                    </div>
                    <div class="monitor-item">
                        <div class="monitor-label">Текущая высота</div>
                        <div class="monitor-value" id="currentHeight">0.00 mm</div>
                    </div>
                    <div class="monitor-item">
                        <div class="monitor-label">Целевое напряжение</div>
                        <div class="monitor-value" id="targetVoltage">140 V</div>
                    </div>
                    <div class="monitor-item">
                        <div class="monitor-label">Статус дуги</div>
                        <div class="monitor-value" id="arcStatus">НЕТ</div>
                    </div>
                </div>

                <div class="position-display">
                    <div style="margin-bottom: 10px; color: #aaa;">Позиция:</div>
                    <div style="display: flex; justify-content: space-between;">
                        <span>X: <span id="posX">0.0</span> mm</span>
                        <span>Y: <span id="posY">0.0</span> mm</span>
                        <span>Z: <span id="posZ">0.0</span> mm</span>
                    </div>
                </div>
            </div>

            <!-- Jog Control Panel -->
            <div class="panel jog-panel">
                <h3>🎮 Ручное управление</h3>
                
                <!-- ИСПРАВЛЕННАЯ СТРУКТУРА -->
                <div class="jog-layout">
                    <!-- Левая колонка - Обнуление координат -->
                    <div class="zero-controls-column">
                        <div style="color: #FF9800; font-size: 0.8em; text-align: center; margin-bottom: 5px;">Обнуление:</div>
                        <button class="zero-btn" onclick="setZero('X')">X=0</button>
                        <button class="zero-btn" onclick="setZero('Y')">Y=0</button>
                        <button class="zero-btn" onclick="setZero('Z')">Z=0</button>
                        <button class="zero-btn" onclick="setZero('XY')">XY=0</button>
                        <button class="zero-btn" onclick="setZero('XYZ')">XYZ=0</button>
                    </div>

                    <!-- Центральная колонка - Кнопки перемещения -->
                    <div class="jog-center-column">
                        <!-- КОНТЕЙНЕР С XY И Z КНОПКАМИ -->
                        <div class="xy-jog-container">
                            <!-- Сетка XY -->
                            <div class="xy-jog-grid">
                                <button class="jog-btn btn-y-plus" onclick="jog('Y+')">Y+</button>
                                <button class="jog-btn btn-y-minus" onclick="jog('Y-')">Y-</button>
                                <button class="jog-btn btn-x-minus" onclick="jog('X-')">X-</button>
                                <div class="jog-btn btn-center">XY</div>
                                <button class="jog-btn btn-x-plus" onclick="jog('X+')">X+</button>
                            </div>
                            
                            <!-- Кнопки Z - ВЕРТИКАЛЬНО справа от X+ -->
                            <div class="z-jog-buttons">
                                <button class="jog-btn btn-z-plus" onclick="jog('Z+')">Z+</button>
                                <button class="jog-btn btn-z-minus" onclick="jog('Z-')">Z-</button>
                            </div>
                        </div>
                        
                        <!-- Индикатор текущего шага -->
                        <div style="color: #4CAF50; font-size: 0.8em; margin-top: 5px;">
                            Шаг: <span id="currentGrid">1</span> mm
                        </div>
                    </div>

                    <!-- Правая колонка - Сетка перемещения -->
                    <div class="grid-controls-column">
                        <div style="color: #2196F3; font-size: 0.8em; text-align: center; margin-bottom: 5px;">Сетка:</div>
                        <button class="grid-btn active" onclick="setGrid(0.1)">0.1mm</button>
                        <button class="grid-btn" onclick="setGrid(1)">1mm</button>
                        <button class="grid-btn" onclick="setGrid(10)">10mm</button>
                        <button class="grid-btn" onclick="setGrid(100)">100mm</button>
                    </div>
                </div>
                
                <div class="control-buttons" style="display: grid; grid-template-columns: 1fr 1fr; gap: 10px; margin-top: 15px;">
                    <button class="btn btn-success" onclick="plasmaOn()">Плазма ВКЛ</button>
                    <button class="btn btn-danger" onclick="plasmaOff()">Плазма ВЫКЛ</button>
                    <button class="btn btn-warning" onclick="homeAll()">🏠 Домой</button>
                    <button class="btn btn-danger" onclick="emergencyStop()">🛑 А-Стоп</button>
                </div>
            </div>

            <!-- THC Control Panel -->
            <div class="panel thc-panel">
                <h3>🎛️ Управление THC</h3>
                
                <div class="status-bar" style="margin-bottom: 15px;">
                    <div class="status-item" id="thcActiveStatus">THC: Не активно</div>
                    <button class="btn" id="thcToggle" onclick="toggleTHC()">THC Вкл</button>
                </div>

                <div class="slider-group">
                    <label>Целевое напряжение: <span id="voltageValue">140</span> V</label>
                    <div class="slider-container">
                        <input type="range" id="voltageSlider" min="80" max="200" value="140" step="1">
                        <span class="slider-value" id="voltageDisplay">140 V</span>
                    </div>
                </div>

                <div class="slider-group">
                    <label>Мертвая зона: ±<span id="deadZoneValue">5</span> V</label>
                    <div class="slider-container">
                        <input type="range" id="deadZoneSlider" min="1" max="20" value="5" step="1">
                        <span class="slider-value" id="deadZoneDisplay">5 V</span>
                    </div>
                </div>

                <button class="btn btn-success" onclick="saveTHCSettings()" style="width: 100%; margin-top: 15px;">
                    💾 Сохранить настройки THC
                </button>
            </div>

            <!-- SD Card Panel -->
            <div class="panel sd-card-panel">
                <h3>📁 Управление файлами (SD Card)</h3>
                
                <!-- Статус SD карты -->
                <div class="status-bar" style="margin-bottom: 15px;">
                    <div class="status-item" id="sdCardStatusPanel">SD Card: Проверка...</div>
                    <div class="status-item" id="fileCount">Файлов: 0</div>
                </div>

                <!-- Загрузка файлов -->
                <div class="file-upload-section">
                    <input type="file" id="fileInput" accept=".gcode,.nc,.txt" style="margin-bottom: 10px; width: 100%; padding: 8px; background: #333; color: white; border: 1px solid #555; border-radius: 5px;">
                    <button class="btn btn-success" onclick="uploadFile()" style="width: 100%;">
                        📤 Загрузить файл на SD
                    </button>
                </div>

                <!-- Список файлов -->
                <div style="background: #333; padding: 10px; border-radius: 5px; max-height: 200px; overflow-y: auto;">
                    <div style="color: #aaa; margin-bottom: 10px;">Файлы на SD карте:</div>
                    <div id="fileList">
                        Загрузка списка файлов...
                    </div>
                </div>

                <!-- Управление выбранным файлом -->
                <div id="selectedFileInfo" style="background: #444; padding: 10px; border-radius: 5px; margin: 10px 0; text-align: center;">
                    Выберите файл для управления
                </div>

                <div class="file-controls">
                    <button class="btn btn-success" onclick="runSelectedFile()" style="flex: 1;">▶️ Запустить</button>
                    <button class="btn btn-danger" onclick="deleteSelectedFile()" style="flex: 1;">🗑️ Удалить</button>
                </div>
            </div>

            <!-- G-code Monitor Panel -->
            <div class="panel gcode-panel">
                <h3>📟 G-code Монитор & Консоль</h3>
                
                <!-- Статус выполнения -->
                <div class="status-bar" style="margin-bottom: 15px;">
                    <div class="status-item" id="gcodeStatus">Статус: Ожидание</div>
                    <div class="status-item" id="gcodeLine">Строка: 0</div>
                    <div class="status-item" id="gcodeRunning">Выполнение: ❌</div>
                </div>

                <!-- Консоль вывода -->
                <div style="background: #1a1a1a; border: 1px solid #333; border-radius: 5px; padding: 10px; margin-bottom: 15px;">
                    <div style="color: #aaa; font-size: 0.8em; margin-bottom: 5px;">Консоль выполнения:</div>
                    <div id="gcodeConsole" style="height: 200px; overflow-y: auto; background: #000; color: #00ff00; font-family: 'Courier New', monospace; padding: 10px; border-radius: 3px; font-size: 0.9em;">
                        <div>> G-code консоль готова...</div>
                    </div>
                </div>

                <!-- Управление выполнением -->
                <div style="display: grid; grid-template-columns: 1fr 1fr 1fr 1fr; gap: 8px; margin-bottom: 15px;">
                    <button class="btn btn-success" onclick="startGCode()" id="startGCodeBtn">▶️ Старт</button>
                    <button class="btn btn-warning" onclick="pauseGCode()" id="pauseGCodeBtn">⏸️ Пауза</button>
                    <button class="btn btn-info" onclick="resumeGCode()" id="resumeGCodeBtn">🔁 Продолжить</button>
                    <button class="btn btn-danger" onclick="stopGCode()" id="stopGCodeBtn">⏹️ Стоп</button>
                </div>

                <!-- Быстрые команды -->
                <div style="margin-bottom: 15px;">
                    <div style="color: #aaa; font-size: 0.8em; margin-bottom: 8px;">Быстрые команды:</div>
                    <div style="display: grid; grid-template-columns: repeat(4, 1fr); gap: 5px;">
                        <button class="btn" onclick="sendGCode('G28')" style="font-size: 0.8em; padding: 8px 5px;">G28 (Home)</button>
                        <button class="btn" onclick="sendGCode('G90')" style="font-size: 0.8em; padding: 8px 5px;">G90 (Abs)</button>
                        <button class="btn" onclick="sendGCode('G91')" style="font-size: 0.8em; padding: 8px 5px;">G91 (Rel)</button>
                        <button class="btn" onclick="sendGCode('M3')" style="font-size: 0.8em; padding: 8px 5px;">M3 (Plasma On)</button>
                        <button class="btn" onclick="sendGCode('M5')" style="font-size: 0.8em; padding: 8px 5px;">M5 (Plasma Off)</button>
                        <button class="btn" onclick="sendGCode('G0 X10 Y10')" style="font-size: 0.8em; padding: 8px 5px;">G0 X10 Y10</button>
                        <button class="btn" onclick="sendGCode('G1 X0 Y0 F1000')" style="font-size: 0.8em; padding: 8px 5px;">G1 X0 Y0</button>
                        <button class="btn" onclick="sendGCode('G4 P1.0')" style="font-size: 0.8em; padding: 8px 5px;">G4 P1.0</button>
                    </div>
                </div>

                <!-- Ручной ввод G-code -->
                <div>
                    <div style="color: #aaa; font-size: 0.8em; margin-bottom: 8px;">Ручной ввод G-code:</div>
                    <div style="display: flex; gap: 8px;">
                        <input type="text" id="gcodeInput" placeholder="Введите G-code команду..." 
                               style="flex: 1; background: #333; color: white; border: 1px solid #555; padding: 10px; border-radius: 5px;"
                               onkeypress="handleGCodeKeypress(event)">
                        <button class="btn btn-success" onclick="sendManualGCode()" style="white-space: nowrap;">📤 Отправить</button>
                    </div>
                </div>

                <!-- История команд -->
                <div style="margin-top: 15px;">
                    <div style="color: #aaa; font-size: 0.8em; margin-bottom: 8px;">История команд:</div>
                    <div id="gcodeHistory" style="height: 100px; overflow-y: auto; background: #333; padding: 8px; border-radius: 5px; font-size: 0.8em;">
                        <!-- История будет заполняться автоматически -->
                    </div>
                </div>
            </div>

            <!-- Recovery Panel -->
            <div class="panel recovery-panel">
                <h3>🔄 Восстановление после сбоев</h3>
                
                <!-- Прогресс выполнения -->
                <div class="status-bar" style="margin-bottom: 15px;">
                    <div class="status-item" id="recoveryStatus">Готов к работе</div>
                    <div class="status-item" id="retryCount">Попытки: 0/3</div>
                    <div class="status-item" id="progressPercent">Прогресс: 0%</div>
                </div>

                <!-- Прогресс-бар -->
                <div style="background: #333; border-radius: 10px; height: 20px; margin-bottom: 15px; overflow: hidden;">
                    <div id="progressBar" style="background: linear-gradient(90deg, #4CAF50, #8BC34A); height: 100%; width: 0%; transition: width 0.3s;"></div>
                </div>

                <!-- Информация о файле -->
                <div style="background: #333; padding: 10px; border-radius: 5px; margin-bottom: 15px;">
                    <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 10px; font-size: 0.9em;">
                        <div>Файл: <span id="currentGCodeFile">-</span></div>
                        <div>Строка: <span id="currentGCodePosition">-</span></div>
                        <div>Всего строк: <span id="totalGCodeLines">-</span></div>
                        <div>Состояние: <span id="gCodeExecutionState">-</span></div>
                    </div>
                </div>

                <!-- Управление восстановлением -->
                <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 10px; margin-bottom: 15px;">
                    <button class="btn btn-warning" onclick="simulatePlasmaOutage()" id="simulateOutageBtn">
                        ⚡ Симулировать сбой
                    </button>
                    <button class="btn btn-success" onclick="resumeAfterRecovery()" id="resumeRecoveryBtn">
                        🔄 Восстановить
                    </button>
                </div>

                <!-- Запуск с определенной строки -->
                <div style="background: #333; padding: 15px; border-radius: 5px; margin-bottom: 15px;">
                    <div style="color: #aaa; font-size: 0.8em; margin-bottom: 10px;">Запуск с определенной строки:</div>
                    <div style="display: flex; gap: 10px; align-items: center;">
                        <input type="number" id="startLineInput" placeholder="Номер строки" 
                               min="1" value="1" style="width: 100px; background: #444; color: white; border: 1px solid #555; padding: 8px; border-radius: 5px;">
                        <button class="btn btn-info" onclick="startFromLine()" style="white-space: nowrap;">
                            🚀 Запустить с строки
                        </button>
                        <button class="btn" onclick="jumpToCurrentLine()" style="white-space: nowrap; background: #666;">
                            📍 Текущая
                        </button>
                    </div>
                </div>

                <!-- Быстрый откат -->
                <div>
                    <div style="color: #aaa; font-size: 0.8em; margin-bottom: 8px;">Быстрый откат:</div>
                    <div style="display: grid; grid-template-columns: repeat(4, 1fr); gap: 5px;">
                        <button class="btn" onclick="jumpBackLines(1)" style="font-size: 0.8em; padding: 8px 5px;">-1 строка</button>
                        <button class="btn" onclick="jumpBackLines(3)" style="font-size: 0.8em; padding: 8px 5px;">-3 строки</button>
                        <button class="btn" onclick="jumpBackLines(5)" style="font-size: 0.8em; padding: 8px 5px;">-5 строк</button>
                        <button class="btn" onclick="jumpBackLines(10)" style="font-size: 0.8em; padding: 8px 5px;">-10 строк</button>
                    </div>
                </div>

                <!-- Лог восстановления -->
                <div style="margin-top: 15px;">
                    <div style="color: #aaa; font-size: 0.8em; margin-bottom: 8px;">Лог восстановления:</div>
                    <div id="recoveryLog" style="height: 120px; overflow-y: auto; background: #1a1a1a; padding: 10px; border-radius: 5px; font-size: 0.8em; font-family: 'Courier New', monospace;">
                        <div style="color: #00ff00;">> Система восстановления готова</div>
                    </div>
                </div>
            </div>

            <!-- Presets Panel -->
            <div class="panel presets-panel">
                <h3>🎯 Управление пресетами</h3>
                
                <!-- Выбор пресета -->
                <div style="display: grid; grid-template-columns: 2fr 1fr 1fr; gap: 10px; margin-bottom: 15px;">
                    <select id="materialPreset" style="background: #333; color: white; border: 1px solid #555; padding: 8px; border-radius: 5px;">
                        <option value="">Выберите материал</option>
                        <option value="Steel 3mm">Сталь 3mm</option>
                        <option value="Steel 6mm">Сталь 6mm</option>
                        <option value="Steel 10mm">Сталь 10mm</option>
                        <option value="Aluminum 3mm">Алюминий 3mm</option>
                        <option value="Stainless 3mm">Нержавейка 3mm</option>
                    </select>
                    <button class="btn btn-success" onclick="loadPreset()">📥 Загрузить</button>
                    <button class="btn btn-danger" onclick="deletePreset()">🗑️ Удалить</button>
                </div>

                <!-- Информация о пресете -->
                <div id="presetInfo" style="background: #333; padding: 15px; border-radius: 5px; margin-bottom: 15px;">
                    <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 10px; font-size: 0.9em;">
                        <div>Напряжение: <span id="presetVoltage">-</span> V</div>
                        <div>Скорость: <span id="presetSpeed">-</span> mm/min</div>
                        <div>Высота пробивки: <span id="presetPierceHeight">-</span> mm</div>
                        <div>Высота резки: <span id="presetCutHeight">-</span> mm</div>
                    </div>
                </div>

                <!-- Кнопки управления -->
                <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 10px;">
                    <button class="btn btn-info" onclick="openPresetEditor()">✏️ Редактировать пресет</button>
                    <button class="btn btn-success" onclick="openNewPresetDialog()">➕ Новый пресет</button>
                </div>
            </div>
        </div>

        <!-- Connection Status -->
        <div class="connection-status" id="connectionInfo">
            🔴 Ожидание подключения WebSocket...
        </div>
    </div>

    <!-- Модальное окно редактора пресетов -->
    <div id="presetEditorModal" class="modal">
        <div class="modal-content">
            <h3 style="margin-top: 0;">✏️ Редактор пресета</h3>
            
            <div class="preset-field">
                <label>Название материала:</label>
                <input type="text" id="editPresetName" placeholder="Например: Сталь 5mm" style="background: #444; color: white; border: 1px solid #555; padding: 8px; border-radius: 3px;">
            </div>
            
            <div class="preset-field">
                <label>Напряжение (V):</label>
                <input type="number" id="editPresetVoltage" value="140" step="1" min="80" max="200" style="background: #444; color: white; border: 1px solid #555; padding: 8px; border-radius: 3px;">
            </div>
            
            <div class="preset-field">
                <label>Мертвая зона (±V):</label>
                <input type="number" id="editPresetDeadZone" value="5" step="1" min="1" max="20" style="background: #444; color: white; border: 1px solid #555; padding: 8px; border-radius: 3px;">
            </div>
            
            <div class="preset-field">
                <label>Скорость резки (mm/min):</label>
                <input type="number" id="editPresetSpeed" value="2000" step="50" min="500" max="5000" style="background: #444; color: white; border: 1px solid #555; padding: 8px; border-radius: 3px;">
            </div>
            
            <div class="preset-field">
                <label>Высота пробивки (mm):</label>
                <input type="number" id="editPresetPierceHeight" value="5.0" step="0.1" min="1" max="20" style="background: #444; color: white; border: 1px solid #555; padding: 8px; border-radius: 3px;">
            </div>
            
            <div class="preset-field">
                <label>Высота резки (mm):</label>
                <input type="number" id="editPresetCutHeight" value="3.0" step="0.1" min="1" max="10" style="background: #444; color: white; border: 1px solid #555; padding: 8px; border-radius: 3px;">
            </div>
            
            <div class="preset-field">
                <label>Задержка пробивки (s):</label>
                <input type="number" id="editPresetPierceDelay" value="0.5" step="0.1" min="0.1" max="2.0" style="background: #444; color: white; border: 1px solid #555; padding: 8px; border-radius: 3px;">
            </div>

            <div style="display: flex; gap: 10px; margin-top: 20px;">
                <button class="btn btn-success" onclick="savePreset()" style="flex: 1;">💾 Сохранить пресет</button>
                <button class="btn btn-danger" onclick="closePresetEditor()" style="flex: 1;">❌ Отмена</button>
            </div>
        </div>
    </div>

    <script>
        let ws = new WebSocket('ws://' + window.location.hostname + ':81/');
        let reconnectInterval;
        let selectedFile = null;
        let currentEditingPreset = null;
        let currentGrid = 1;
        
        // G-code переменные
        let gcodeHistory = [];
        let maxHistoryItems = 50;

        // Инициализация
        document.addEventListener('DOMContentLoaded', function() {
            setupEventListeners();
            loadFileList();
            updateGridButtons();
            monitorPlasmaState();
        });
        
        function setupEventListeners() {
            // Слайдеры THC
            document.getElementById('voltageSlider').addEventListener('input', function(e) {
                document.getElementById('voltageValue').textContent = e.target.value;
                document.getElementById('voltageDisplay').textContent = e.target.value + ' V';
            });
            
            document.getElementById('deadZoneSlider').addEventListener('input', function(e) {
                document.getElementById('deadZoneValue').textContent = e.target.value;
                document.getElementById('deadZoneDisplay').textContent = e.target.value + ' V';
            });
        }
        
        // WebSocket handlers
        ws.onopen = function() {
            console.log('WebSocket connected');
            updateConnectionStatus(true);
            clearInterval(reconnectInterval);
            sendCommand('get_file_list');
        };
        
        ws.onclose = function() {
            console.log('WebSocket disconnected');
            updateConnectionStatus(false);
            reconnectInterval = setInterval(() => {
                console.log('Attempting to reconnect...');
                ws = new WebSocket('ws://' + window.location.hostname + ':81/');
            }, 3000);
        };
        
        ws.onmessage = function(event) {
            try {
                const data = JSON.parse(event.data);
                updateUI(data);
            } catch (e) {
                console.error('Error parsing WebSocket message:', e);
            }
        };
        
        function updateUI(data) {
            // Обновление мониторинга
            if (data.arcVoltage !== undefined) {
                document.getElementById('arcVoltage').textContent = data.arcVoltage.toFixed(1) + ' V';
            }
            if (data.currentHeight !== undefined) {
                document.getElementById('currentHeight').textContent = data.currentHeight.toFixed(2) + ' mm';
            }
            if (data.targetVoltage !== undefined) {
                document.getElementById('targetVoltage').textContent = data.targetVoltage + ' V';
            }
            if (data.arcOK !== undefined) {
                document.getElementById('arcStatus').textContent = data.arcOK ? 'ДА' : 'НЕТ';
                document.getElementById('arcStatus').style.color = data.arcOK ? '#4CAF50' : '#f44336';
            }
            
            // Обновление позиции
            if (data.x !== undefined) document.getElementById('posX').textContent = data.x.toFixed(1);
            if (data.y !== undefined) document.getElementById('posY').textContent = data.y.toFixed(1);
            if (data.z !== undefined) document.getElementById('posZ').textContent = data.z.toFixed(1);
            
            // Обновление статусов
            if (data.machineState !== undefined) document.getElementById('machineStatus').textContent = data.machineState;
            if (data.thcEnabled !== undefined) {
                document.getElementById('thcStatus').textContent = 'THC: ' + (data.thcEnabled ? 'Вкл' : 'Выкл');
                const thcToggle = document.getElementById('thcToggle');
                thcToggle.textContent = data.thcEnabled ? 'THC Выкл' : 'THC Вкл';
                thcToggle.className = data.thcEnabled ? 'btn btn-danger' : 'btn btn-success';
            }
            if (data.plasmaActive !== undefined) {
                document.getElementById('plasmaStatus').textContent = 'Плазма: ' + (data.plasmaActive ? 'Вкл' : 'Выкл');
            }
            if (data.thcActive !== undefined) {
                document.getElementById('thcActiveStatus').textContent = data.thcActive ? 'THC: Активно' : 'THC: Не активно';
                document.getElementById('thcActiveStatus').style.color = data.thcActive ? '#4CAF50' : '#f44336';
            }
            
            // Обновление статуса SD карты
            if (data.sdCardStatus !== undefined) {
                const sdStatus = document.getElementById('sdCardStatus');
                const sdStatusPanel = document.getElementById('sdCardStatusPanel');
                const statusText = data.sdCardStatus === 'mounted' ? '✅ Подключена' : '❌ Ошибка';
                const statusColor = data.sdCardStatus === 'mounted' ? '#4CAF50' : '#f44336';
                
                sdStatus.textContent = 'SD Card: ' + statusText;
                sdStatus.style.color = statusColor;
                sdStatusPanel.textContent = 'SD Card: ' + statusText;
                sdStatusPanel.style.color = statusColor;
            }
            
            if (data.sdCardFiles !== undefined) {
                document.getElementById('fileCount').textContent = 'Файлов: ' + data.sdCardFiles;
            }
            
            // Обработка списка файлов из WebSocket
            if (data.type === 'file_list') {
                updateFileList(data);
            }
            
            // Обработка G-code сообщений
            if (data.type === 'gcode_response') {
                addToConsole(`<span style="color: #ffff00">>> ${data.command}</span>`, 'command');
                addToConsole(`${data.response}`, 'response');
            }
            
            if (data.type === 'gcode_state') {
                document.getElementById('gcodeStatus').textContent = `Статус: ${data.state}`;
                document.getElementById('gcodeLine').textContent = `Строка: ${data.currentLine}`;
                document.getElementById('gcodeRunning').textContent = `Выполнение: ${data.isRunning ? '✅' : '❌'}`;
                document.getElementById('gcodeRunning').style.color = data.isRunning ? '#4CAF50' : '#f44336';
                
                // Обновляем кнопки управления
                const startBtn = document.getElementById('startGCodeBtn');
                const pauseBtn = document.getElementById('pauseGCodeBtn');
                const resumeBtn = document.getElementById('resumeGCodeBtn');
                const stopBtn = document.getElementById('stopGCodeBtn');
                
                if (data.isRunning) {
                    pauseBtn.disabled = false;
                    resumeBtn.disabled = true;
                    stopBtn.disabled = false;
                } else {
                    pauseBtn.disabled = true;
                    resumeBtn.disabled = false;
                    stopBtn.disabled = false;
                }
                
                if (data.currentCommand) {
                    addToConsole(`Выполняется: ${data.currentCommand}`, 'command');
                }
            }
            
            // Обработка прогресса G-code
            if (data.type === 'gcode_progress') {
                document.getElementById('currentGCodeFile').textContent = data.filename || '-';
                document.getElementById('currentGCodePosition').textContent = data.currentLine + 1;
                document.getElementById('totalGCodeLines').textContent = data.totalLines;
                document.getElementById('gCodeExecutionState').textContent = 
                    data.isRunning ? (data.isPaused ? 'Пауза' : 'Выполнение') : 'Остановлен';
                
                const progress = data.progress || 0;
                document.getElementById('progressPercent').textContent = `Прогресс: ${progress.toFixed(1)}%`;
                document.getElementById('progressBar').style.width = progress + '%';
                document.getElementById('retryCount').textContent = `Попытки: ${data.retryCount}/3`;
                
                // Обновляем кнопки
                const simulateBtn = document.getElementById('simulateOutageBtn');
                const resumeBtn = document.getElementById('resumeRecoveryBtn');
                
                simulateBtn.disabled = !data.isRunning;
                resumeBtn.disabled = !data.isPaused;
            }
            
            if (data.type === 'gcode_state') {
                document.getElementById('recoveryStatus').textContent = 
                    data.state === 'PLASMA_OUTAGE' ? 'СБОЙ ПЛАЗМЫ' :
                    data.state === 'RECOVERY' ? 'ВОССТАНОВЛЕНИЕ' :
                    data.state === 'RUNNING_FROM_LINE' ? 'ЗАПУСК С СТРОКИ' :
                    data.state;
            }
        }
        
        function updateFileList(data) {
            const fileList = document.getElementById('fileList');
            if (data.sdCardStatus === 'mounted' && Array.isArray(data)) {
                fileList.innerHTML = data.map(file => {
                    const safeFilename = file.replace(/'/g, "\\'").replace(/"/g, "\\\"");
                    return `<div class="file-item" onclick="selectFile('${safeFilename}')">📄 ${file}</div>`;
                }).join('');
                
                if (data.length === 0) {
                    fileList.innerHTML = '<div style="color: #aaa; text-align: center;">Нет файлов</div>';
                }
            } else {
                fileList.innerHTML = '<div style="color: #f44336; text-align: center;">Ошибка SD карты</div>';
            }
        }
        
        function updateConnectionStatus(connected) {
            const status = document.getElementById('connectionInfo');
            status.textContent = connected ? '🟢 WebSocket подключен - Система онлайн' : '🔴 WebSocket отключен - Переподключение...';
            status.style.background = connected ? '#4CAF50' : '#f44336';
        }
        
        // ФУНКЦИИ УПРАВЛЕНИЯ ОСЯМИ И СЕТКИ
        function setGrid(gridSize) {
            currentGrid = gridSize;
            document.getElementById('currentGrid').textContent = gridSize;
            updateGridButtons();
        }
        
        function updateGridButtons() {
            const buttons = document.querySelectorAll('.grid-btn');
            buttons.forEach(btn => {
                const btnGrid = parseFloat(btn.textContent.replace('mm', ''));
                if (btnGrid === currentGrid) {
                    btn.classList.add('active');
                } else {
                    btn.classList.remove('active');
                }
            });
        }
        
        function jog(direction) {
            sendCommand('jog', { direction: direction, distance: currentGrid });
        }
        
        // Функции обнуления координат
        function setZero(axis) {
            sendCommand('set_zero', { axis: axis });
            
            // Дублируем через HTTP для надежности
            fetch('/api/set_zero?axis=' + axis, { method: 'POST' })
                .then(response => response.json())
                .then(data => {
                    if (data.status === 'ok') {
                        console.log('Координаты обнулены для оси: ' + axis);
                    }
                })
                .catch(error => {
                    console.error('Ошибка обнуления координат:', error);
                });
        }
        
        // Управление системой
        function plasmaOn() {
            sendCommand('plasma_on');
        }
        
        function plasmaOff() {
            sendCommand('plasma_off');
        }
        
        function toggleTHC() {
            sendCommand('toggle_thc');
        }
        
        function homeAll() {
            sendCommand('home_all');
        }
        
        function emergencyStop() {
            if(confirm('!!! ВНИМАНИЕ !!!\n\nАварийная остановка системы. Продолжить?')) {
                sendCommand('emergency_stop');
            }
        }
        
        function saveTHCSettings() {
            const settings = {
                voltage: parseInt(document.getElementById('voltageSlider').value),
                deadZone: parseInt(document.getElementById('deadZoneSlider').value)
            };
            sendCommand('thc_settings', settings);
            alert('Настройки THC сохранены!');
        }
        
        // G-code функции
        function sendGCode(command) {
            document.getElementById('gcodeInput').value = command;
            sendManualGCode();
        }

        function sendManualGCode() {
            const gcodeInput = document.getElementById('gcodeInput');
            const command = gcodeInput.value.trim();
            
            if (!command) {
                alert('Введите G-code команду');
                return;
            }
            
            // Добавляем в историю
            addToHistory(command, 'pending');
            
            // Отправляем через WebSocket
            sendCommand('send_gcode', { gcode: command });
            
            // Также отправляем через HTTP для надежности
            fetch('/api/gcode?command=' + encodeURIComponent(command), {
                method: 'POST'
            })
            .then(response => response.json())
            .then(data => {
                if (data.status === 'ok') {
                    updateHistoryItem(command, 'success');
                }
            })
            .catch(error => {
                console.error('G-code error:', error);
                updateHistoryItem(command, 'error');
            });
            
            gcodeInput.value = '';
            gcodeInput.focus();
        }

        function handleGCodeKeypress(event) {
            if (event.key === 'Enter') {
                sendManualGCode();
            }
        }

        function addToHistory(command, status) {
            const timestamp = new Date().toLocaleTimeString();
            const historyItem = {
                command: command,
                status: status,
                timestamp: timestamp
            };
            
            gcodeHistory.unshift(historyItem);
            if (gcodeHistory.length > maxHistoryItems) {
                gcodeHistory.pop();
            }
            
            updateHistoryDisplay();
        }

        function updateHistoryItem(command, status) {
            const item = gcodeHistory.find(item => item.command === command && item.status === 'pending');
            if (item) {
                item.status = status;
                updateHistoryDisplay();
            }
        }

        function updateHistoryDisplay() {
            const historyElement = document.getElementById('gcodeHistory');
            historyElement.innerHTML = gcodeHistory.map(item => {
                const statusClass = item.status === 'success' ? 'success' : 
                                   item.status === 'error' ? 'error' : 'pending';
                return `<div class="${statusClass}">
                    <strong>${item.timestamp}</strong>: ${item.command}
                    ${item.status === 'success' ? '✅' : 
                      item.status === 'error' ? '❌' : '⏳'}
                </div>`;
            }).join('');
        }

        function addToConsole(message, type = 'response') {
            const consoleElement = document.getElementById('gcodeConsole');
            const timestamp = new Date().toLocaleTimeString();
            const messageElement = document.createElement('div');
            messageElement.className = type;
            messageElement.innerHTML = `<span style="color: #888">[${timestamp}]</span> ${message}`;
            
            consoleElement.appendChild(messageElement);
            consoleElement.scrollTop = consoleElement.scrollHeight;
        }

        function clearConsole() {
            document.getElementById('gcodeConsole').innerHTML = '<div>> G-code консоль очищена</div>';
        }

        function startGCode() {
            const filename = selectedFile;
            if (!filename) {
                alert('Выберите G-code файл для запуска');
                return;
            }
            sendCommand('start_gcode_file', { filename: filename });
            addToConsole(`▶️ Запуск файла: ${filename}`, 'command');
        }

        function pauseGCode() {
            sendCommand('pause_gcode');
            addToConsole('⏸️ Выполнение приостановлено', 'warning');
        }

        function resumeGCode() {
            sendCommand('resume_gcode');
            addToConsole('🔁 Продолжение выполнения', 'command');
        }

        function stopGCode() {
            if (confirm('Остановить выполнение G-code?')) {
                sendCommand('stop_gcode');
                addToConsole('⏹️ Выполнение остановлено', 'error');
            }
        }

        // Recovery функции
        function startFromLine() {
            const filename = selectedFile;
            const startLine = parseInt(document.getElementById('startLineInput').value) - 1;
            
            if (!filename) {
                alert('Выберите G-code файл');
                return;
            }
            
            if (isNaN(startLine) || startLine < 0) {
                alert('Введите корректный номер строки');
                return;
            }
            
            sendCommand('start_gcode_from_line', { 
                filename: filename, 
                startLine: startLine 
            });
            
            addToRecoveryLog(`🚀 Запуск с строки ${startLine + 1}`, 'info');
        }

        function jumpBackLines(lines) {
            const currentLine = parseInt(document.getElementById('currentGCodePosition').textContent) || 1;
            const newLine = Math.max(1, currentLine - lines);
            document.getElementById('startLineInput').value = newLine;
            addToRecoveryLog(`↩️ Откат на ${lines} строк до ${newLine}`, 'warning');
        }

        function jumpToCurrentLine() {
            const currentLine = parseInt(document.getElementById('currentGCodePosition').textContent) || 1;
            document.getElementById('startLineInput').value = currentLine;
        }

        function simulatePlasmaOutage() {
            if (confirm('Симулировать сбой плазмы? Это вызовет аварийную остановку.')) {
                sendCommand('handle_plasma_outage');
                addToRecoveryLog('⚡ СИМУЛЯЦИЯ СБОЯ ПЛАЗМЫ', 'error');
            }
        }

        function resumeAfterRecovery() {
            sendCommand('resume_after_recovery');
            addToRecoveryLog('🔄 Запуск процедуры восстановления', 'info');
        }

        function addToRecoveryLog(message, type = 'info') {
            const logElement = document.getElementById('recoveryLog');
            const timestamp = new Date().toLocaleTimeString();
            const messageElement = document.createElement('div');
            messageElement.className = type;
            messageElement.innerHTML = `<span style="color: #888">[${timestamp}]</span> ${message}`;
            
            logElement.appendChild(messageElement);
            logElement.scrollTop = logElement.scrollHeight;
        }

        function clearRecoveryLog() {
            document.getElementById('recoveryLog').innerHTML = '<div style="color: #00ff00;">> Лог восстановления очищен</div>';
        }

        // ФУНКЦИИ РЕДАКТОРА ПРЕСЕТОВ
        function loadPreset() {
            const presetSelect = document.getElementById('materialPreset');
            const presetName = presetSelect.value;
            
            if (!presetName) {
                alert('Выберите пресет материала');
                return;
            }
            
            sendCommand('load_preset', { presetName: presetName });
            
            const presetData = getPresetData(presetName);
            updatePresetInfo(presetData);
        }
        
        function deletePreset() {
            const presetSelect = document.getElementById('materialPreset');
            const presetName = presetSelect.value;
            
            if (!presetName) {
                alert('Выберите пресет для удаления');
                return;
            }
            
            if (confirm(`Удалить пресет "${presetName}"?`)) {
                sendCommand('delete_preset', { presetName: presetName });
                presetSelect.value = '';
                document.getElementById('presetInfo').innerHTML = 'Пресет удален';
            }
        }
        
        function openPresetEditor() {
            const presetSelect = document.getElementById('materialPreset');
            const presetName = presetSelect.value;
            
            if (!presetName) {
                alert('Выберите пресет для редактирования');
                return;
            }
            
            currentEditingPreset = presetName;
            const presetData = getPresetData(presetName);
            
            document.getElementById('editPresetName').value = presetName;
            document.getElementById('editPresetVoltage').value = presetData.voltage;
            document.getElementById('editPresetDeadZone').value = presetData.deadZone;
            document.getElementById('editPresetSpeed').value = presetData.speed;
            document.getElementById('editPresetPierceHeight').value = presetData.pierceHeight;
            document.getElementById('editPresetCutHeight').value = presetData.cutHeight;
            document.getElementById('editPresetPierceDelay').value = presetData.pierceDelay;
            
            document.getElementById('presetEditorModal').style.display = 'block';
        }
        
        function openNewPresetDialog() {
            currentEditingPreset = null;
            
            document.getElementById('editPresetName').value = '';
            document.getElementById('editPresetVoltage').value = 140;
            document.getElementById('editPresetDeadZone').value = 5;
            document.getElementById('editPresetSpeed').value = 2000;
            document.getElementById('editPresetPierceHeight').value = 5.0;
            document.getElementById('editPresetCutHeight').value = 3.0;
            document.getElementById('editPresetPierceDelay').value = 0.5;
            
            document.getElementById('presetEditorModal').style.display = 'block';
        }
        
        function closePresetEditor() {
            document.getElementById('presetEditorModal').style.display = 'none';
        }
        
        function savePreset() {
            const presetData = {
                name: document.getElementById('editPresetName').value,
                voltage: parseFloat(document.getElementById('editPresetVoltage').value),
                deadZone: parseFloat(document.getElementById('editPresetDeadZone').value),
                speed: parseFloat(document.getElementById('editPresetSpeed').value),
                pierceHeight: parseFloat(document.getElementById('editPresetPierceHeight').value),
                cutHeight: parseFloat(document.getElementById('editPresetCutHeight').value),
                pierceDelay: parseFloat(document.getElementById('editPresetPierceDelay').value)
            };
            
            if (!presetData.name) {
                alert('Введите название пресета');
                return;
            }
            
            if (currentEditingPreset) {
                sendCommand('update_preset', { 
                    oldName: currentEditingPreset,
                    newData: presetData 
                });
            } else {
                sendCommand('create_preset', presetData);
            }
            
            closePresetEditor();
            alert('Пресет сохранен: ' + presetData.name);
        }
        
        function updatePresetInfo(presetData) {
            document.getElementById('presetVoltage').textContent = presetData.voltage;
            document.getElementById('presetSpeed').textContent = presetData.speed;
            document.getElementById('presetPierceHeight').textContent = presetData.pierceHeight;
            document.getElementById('presetCutHeight').textContent = presetData.cutHeight;
        }
        
        function getPresetData(presetName) {
            const presets = {
                'Steel 3mm': { voltage: 125, deadZone: 5, speed: 2500, pierceHeight: 5.0, cutHeight: 3.0, pierceDelay: 0.5 },
                'Steel 6mm': { voltage: 135, deadZone: 6, speed: 1800, pierceHeight: 6.0, cutHeight: 4.0, pierceDelay: 0.8 },
                'Steel 10mm': { voltage: 145, deadZone: 8, speed: 1200, pierceHeight: 8.0, cutHeight: 5.0, pierceDelay: 1.2 },
                'Aluminum 3mm': { voltage: 115, deadZone: 4, speed: 2000, pierceHeight: 4.0, cutHeight: 2.5, pierceDelay: 0.3 },
                'Stainless 3mm': { voltage: 130, deadZone: 4, speed: 2200, pierceHeight: 4.0, cutHeight: 2.8, pierceDelay: 0.4 }
            };
            return presets[presetName] || { voltage: '-', deadZone: '-', speed: '-', pierceHeight: '-', cutHeight: '-', pierceDelay: '-' };
        }

        // Функции управления файлами
        function loadFileList() {
            fetch('/api/files/list')
                .then(response => response.json())
                .then(data => {
                    if (data.error) {
                        document.getElementById('fileList').innerHTML = '<div style="color: #f44336;">Ошибка: ' + data.error + '</div>';
                        return;
                    }
                    updateFileList(data);
                })
                .catch(error => {
                    console.error('Ошибка загрузки списка файлов:', error);
                    document.getElementById('fileList').innerHTML = '<div style="color: #f44336;">Ошибка загрузки</div>';
                });
        }

        function selectFile(filename) {
            selectedFile = filename;
            document.getElementById('selectedFileInfo').innerHTML = `📄 Выбран: <strong>${filename}</strong>`;
            
            const items = document.querySelectorAll('.file-item');
            items.forEach(item => {
                item.style.background = item.textContent.includes(filename) ? '#444' : 'transparent';
            });
        }

        function uploadFile() {
            const fileInput = document.getElementById('fileInput');
            const file = fileInput.files[0];
            
            if (!file) {
                alert('Выберите файл для загрузки');
                return;
            }
            
            const formData = new FormData();
            formData.append('file', file);
            
            fetch('/api/files/upload', {
                method: 'POST',
                body: formData
            })
            .then(response => response.text())
            .then(result => {
                alert('✅ ' + result);
                loadFileList();
                fileInput.value = '';
            })
            .catch(error => {
                alert('❌ Ошибка загрузки: ' + error);
            });
        }

        function runSelectedFile() {
            if (!selectedFile) {
                alert('Выберите файл для запуска');
                return;
            }
            
            if(confirm(`Запустить файл: ${selectedFile}?`)) {
                fetch('/api/files/run?filename=' + encodeURIComponent(selectedFile), {
                    method: 'POST'
                })
                .then(response => response.text())
                .then(result => {
                    alert('▶️ ' + result);
                })
                .catch(error => {
                    alert('❌ Ошибка запуска: ' + error);
                });
            }
        }

        function deleteSelectedFile() {
            if (!selectedFile) {
                alert('Выберите файл для удаления');
                return;
            }
            
            if(confirm(`Удалить файл: ${selectedFile}?`)) {
                fetch('/api/files/delete?filename=' + encodeURIComponent(selectedFile), {
                    method: 'POST'
                })
                .then(response => response.text())
                .then(result => {
                    alert('🗑️ ' + result);
                    selectedFile = null;
                    loadFileList();
                    document.getElementById('selectedFileInfo').innerHTML = 'Выберите файл для управления';
                })
                .catch(error => {
                    alert('❌ Ошибка удаления: ' + error);
                });
            }
        }

        // Автоматический мониторинг состояния плазмы
        function monitorPlasmaState() {
            setInterval(() => {
                // Здесь должна быть реальная проверка состояния плазмы
                // Например, если напряжение дуги упало ниже порога
                if (false) { // Замени на реальную проверку
                    sendCommand('handle_plasma_outage');
                }
            }, 1000);
        }

        // Вспомогательные функции
        function sendCommand(command, data = {}) {
            const message = { command, ...data };
            if(ws.readyState === WebSocket.OPEN) {
                ws.send(JSON.stringify(message));
            } else {
                alert('WebSocket не подключен. Команда не отправлена.');
            }
        }

        // Закрытие модального окна при клике вне его
        window.onclick = function(event) {
            const modal = document.getElementById('presetEditorModal');
            if (event.target == modal) {
                closePresetEditor();
            }
        }
    </script>
</body>
</html>
)rawliteral";
}
