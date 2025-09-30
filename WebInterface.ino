#include "WebInterface.h"
#include "Config.h"
#include "StepperControl.h"
#include "PlasmaControl.h"
#include "THC_System.h"
#include "FileManager.h"
#include "MaterialPresets.h"

WebServer WebInterface::server(80);
WebSocketsServer WebInterface::webSocket(81);

void WebInterface::init() {
  // Запуск WiFi
  WiFi.softAP(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("WiFi AP started: %s\n", WiFi.softAPIP().toString().c_str());

  // Маршруты обработки файлов:
  server.on(
    "/api/files/upload", HTTP_POST, []() {
      WebInterface::server.send(200, "text/plain", "Upload complete");
    },
    WebInterface::handleFileUpload);

  server.on("/api/files/list", HTTP_GET, WebInterface::handleFileList);
  server.on("/api/files/delete", HTTP_POST, WebInterface::handleFileDelete);
  server.on("/api/files/run", HTTP_POST, WebInterface::handleFileRun);

  // Настройка HTTP маршрутов
  server.on("/", WebInterface::handleRoot);
  server.on("/api/status", WebInterface::handleAPIStatus);
  server.on("/api/files", WebInterface::handleAPIFiles);

  // Добавляем обработчик для обнуления координат через HTTP
  server.on("/api/set_zero", HTTP_POST, WebInterface::handleSetZero);

  server.begin();

  // Запуск WebSocket
  webSocket.begin();
  webSocket.onEvent(WebInterface::handleWebSocket);

  Serial.println("Web Interface initialized");
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

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void WebInterface::handleAPIFiles() {
  auto files = FileManager::listFiles();
  DynamicJsonDocument doc(2048);
  JsonArray filesArray = doc.to<JsonArray>();

  for (const String& file : files) {
    filesArray.add(file);
  }

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// Новый обработчик для обнуления координат через HTTP
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
      StepperControl::setCurrentZ(0);
      StepperControl::setCurrentX(0);
      StepperControl::setCurrentY(0);
    }
    
    Serial.printf("Координаты обнулены для оси: %s\n", axis.c_str());
    WebInterface::server.send(200, "application/json", "{\"status\":\"ok\"}");
  } else {
    WebInterface::server.send(400, "application/json", "{\"error\":\"Missing axis parameter\"}");
  }
}

// Реализации методов обработки файлов
void WebInterface::handleFileUpload() {
  HTTPUpload& upload = server.upload();
  static File uploadFile;

  if (upload.status == UPLOAD_FILE_START) {
    String filename = "/" + upload.filename;
    uploadFile = SD_MMC.open(filename, FILE_WRITE);
    if (!uploadFile) {
      server.send(500, "text/plain", "Ошибка создания файла");
      return;
    }
    Serial.println("Начало загрузки: " + upload.filename);
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFile) {
      uploadFile.write(upload.buf, upload.currentSize);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) {
      uploadFile.close();
      Serial.println("Загрузка завершена: " + upload.filename);
      server.send(200, "text/plain", "Файл загружен: " + upload.filename);
    }
  }
}

void WebInterface::handleFileList() {
  auto files = FileManager::listFiles();

  DynamicJsonDocument doc(2048);
  JsonArray filesArray = doc.to<JsonArray>();

  for (const String& file : files) {
    filesArray.add(file);
  }

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void WebInterface::handleFileDelete() {
  String filename = server.arg("filename");
  if (FileManager::deleteFile(filename)) {
    server.send(200, "text/plain", "Файл удален: " + filename);
  } else {
    server.send(500, "text/plain", "Ошибка удаления файла");
  }
}

void WebInterface::handleFileRun() {
  String filename = server.arg("filename");
  FileManager::runGCodeFile(filename);
  server.send(200, "text/plain", "Запуск файла: " + filename);
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
    Serial.println("EMERGENCY STOP");
    // Добавь здесь код аварийной остановки
  } else if (command == "toggle_thc") {
    THC_System::toggle();
  } else if (command == "thc_settings") {
    float voltage = doc["voltage"];
    int deadZone = doc["deadZone"];
    float correctionRate = doc["correctionRate"];
    THC_System::setTargetVoltage(voltage);
    Serial.printf("THC settings: voltage=%.1f, deadZone=%d\n", voltage, deadZone);
  } else if (command == "load_preset") {
    String presetName = doc["presetName"];
    Serial.printf("Загружаем пресет: %s\n", presetName.c_str());
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
    Serial.printf("Обновление пресета: %s -> %s\n", oldName.c_str(), newName.c_str());
  } else if (command == "delete_preset") {
    String presetName = doc["presetName"];
    Serial.printf("Удаление пресета: %s\n", presetName.c_str());
  } else if (command == "set_zero") {
    String axis = doc["axis"];
    Serial.printf("Установка нуля для оси: %s\n", axis.c_str());
    
    // Реальная логика обнуления координат
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
    Serial.println("Установка текущей позиции как нулевой");
    
    // Устанавливаем текущую позицию как нулевую
    StepperControl::setCurrentPositionAsZero();
    
    // Отправляем обновленные координаты
    DynamicJsonDocument responseDoc(512);
    responseDoc["type"] = "status";
    responseDoc["x"] = StepperControl::getCurrentX();
    responseDoc["y"] = StepperControl::getCurrentY();
    responseDoc["z"] = StepperControl::getCurrentZ();
    
    String response;
    serializeJson(responseDoc, response);
    webSocket.broadcastTXT(response);
  }

  sendSystemState();
}

// Остальной код getMainPage() остается без изменений...

String WebInterface::getMainPage() {
  return R"rawliteral(
<!DOCTYPE html>
<html lang="ru">
<head>
    <meta charset="UTF-8">
    <title>ЧПУ плазма</title>
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
            grid-template-columns: 250px 0,5fr 250px;
            gap: 10px;
            height: 100%;
        }
        
        .panel {
            background: var(--darker);
            border-radius: 10px;
            padding: 15px;
            border: 1px solid #333;
            box-shadow: 0 2px 5px rgba(0,0,0,0.2);
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
        
        /* Jog Control - НОВАЯ КОМПАКТНАЯ СТРУКТУРА */
        .jog-layout {
            display: grid;
            grid-template-columns: 100px 1fr 80px;
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
            display: grid;
            grid-template-columns: 1fr;
            gap: 5px;
            width: 100%;
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
        
        /* Кнопки Z - В 2 РАЗА ШИРЕ */
        .btn-z-plus { 
            background: #4CAF50; 
            height: 60px;
            font-size: 1.2em;
            width: 50px;
        }
        .btn-z-minus { 
            background: #4CAF50; 
            height: 60px;
            font-size: 1.2em;
            width: 50px;
        }
        
        /* Control Buttons */
        .control-buttons {
            display: flex;
            flex-direction: column;
            gap: 10px;
            margin-top: 20px;
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
        }
    </style>
</head>
<body>
    <div class="app-container">
        <!-- Header -->
        <header class="header">
            <div class="logo">
                <h1>ЧПУ Контроллер</h1>
                <span>ESP32-S3</span>
            </div>
            <div class="status-bar">
                <div class="status-item" id="connectionStatus">Подключено</div>
                <div class="status-item" id="machineStatus">Готов</div>
                <div class="status-item" id="thcStatus">THC: Выкл</div>
                <div class="status-item" id="plasmaStatus">Плазма: Выкл</div>
            </div>
        </header>

        <!-- Main Content -->
        <div class="main-content">
            <!-- Monitoring Panel -->
            <div class="panel monitoring-panel">
                <h3>Мониторинг системы</h3>
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
                
                <!-- НОВАЯ КОМПАКТНАЯ СТРУКТУРА -->
                <div class="jog-layout">
                    <!-- Левая колонка - Обнуление координат -->
                    <div class="zero-controls-column">
                        <div style="color: #FF9800; font-size: 0.8em; text-align: center; margin-bottom: 5px;">Обнуление:</div>
                        <button class="zero-btn" onclick="setZero('X')">X=0</button>
                        <button class="zero-btn" onclick="setZero('Y')">Y=0</button>
                        <button class="zero-btn" onclick="setZero('Z')">Z=0</button>
                        <button class="zero-btn" onclick="setZero('XY')">XY=0</button>
                        <button class="zero-btn" onclick="setZero('XYZ')">XYZ=0</button>
                        <button class="zero-btn" onclick="setCurrentAsZero()">Текущая=0</button>
                    </div>

                    <!-- Центральная колонка - Кнопки перемещения -->
                    <div class="jog-center-column">
                        <!-- XY Jog Grid -->
                        <div class="xy-jog-grid">
                            <button class="jog-btn btn-y-plus" onclick="jog('Y+')">Y+</button>
                            <button class="jog-btn btn-y-minus" onclick="jog('Y-')">Y-</button>
                            <button class="jog-btn btn-x-minus" onclick="jog('X-')">X-</button>
                            <div class="jog-btn btn-center">XY</div>
                            <button class="jog-btn btn-x-plus" onclick="jog('X+')">X+</button>
                        </div>
                        
                        <!-- Z Jog Buttons - В 2 РАЗА ШИРЕ -->
                        <div class="z-jog-buttons">
                            <button class="jog-btn btn-z-plus" onclick="jog('Z+')">Z+</button>
                            <button class="jog-btn btn-z-minus" onclick="jog('Z-')">Z-</button>
                        </div>
                        
                        <!-- Индикатор текущего шага -->
                        <div style="color: #4CAF50; font-size: 0.8em; margin-top: 5px;">
                            Шаг: <span id="currentGrid">1</span> mm
                        </div>
                    </div>

                    <!-- Правая колонка - Сетка перемещения -->
                    <div class="grid-controls-column">
                        <div style="color: #2196F3; font-size: 0.8em; text-align: center; margin-bottom: 5px;">Сетка:</div>
                        <button class="grid-btn active" onclick="setGrid(1)">1mm</button>
                        <button class="grid-btn" onclick="setGrid(5)">5mm</button>
                        <button class="grid-btn" onclick="setGrid(10)">10mm</button>
                        <button class="grid-btn" onclick="setGrid(100)">100mm</button>
                    </div>
                </div>
                
                <div class="control-buttons">
                    <button class="btn btn-success" onclick="plasmaOn()">M03 - Плазма ВКЛ</button>
                    <button class="btn btn-danger" onclick="plasmaOff()">M05 - Плазма ВЫКЛ</button>
                    <button class="btn btn-warning" onclick="homeAll()">К Началу</button>
                    <button class="btn btn-danger" onclick="emergencyStop()">Аварийный Стоп</button>
                </div>
            </div>

            <!-- Presets Panel -->
            <div class="panel presets-panel">
                <h3>Управление пресетами</h3>
                
                <!-- Выбор пресета -->
                <div style="display: grid; grid-template-columns: 2fr 1fr 1fr; gap: 10px; margin-bottom: 15px;">
                    <select id="materialPreset">
                        <option value="">Выберите материал</option>
                        <option value="Steel 3mm">Сталь 3mm</option>
                        <option value="Steel 6mm">Сталь 6mm</option>
                        <option value="Steel 10mm">Сталь 10mm</option>
                        <option value="Aluminum 3mm">Алюминий 3mm</option>
                        <option value="Stainless 3mm">Нержавейка 3mm</option>
                    </select>
                    <button class="btn btn-success" onclick="loadPreset()">Загрузить</button>
                    <button class="btn btn-danger" onclick="deletePreset()">Удалить</button>
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
                    <button class="btn btn-info" onclick="openPresetEditor()">Редактировать пресет</button>
                    <button class="btn btn-success" onclick="openNewPresetDialog()">Новый пресет</button>
                </div>
            </div>

            <!-- THC Control Panel -->
            <div class="panel thc-panel">
                <h3>Управление THC</h3>
                
                <div class="slider-group">
                    <label>Целевое напряжение: <span class="slider-value" id="voltageValue">140</span> V</label>
                    <div class="slider-container">
                        <input type="range" id="voltageSlider" min="80" max="200" value="140" step="1">
                    </div>
                </div>
                
                <div class="slider-group">
                    <label>Слепая зона: ±<span class="slider-value" id="deadZoneValue">5</span> V</label>
                    <div class="slider-container">
                        <input type="range" id="deadZoneSlider" min="1" max="20" value="5" step="1">
                    </div>
                </div>
                
                <div style="display: flex; gap: 10px; margin: 15px 0;">
                    <button class="btn btn-success" id="thcToggle" onclick="toggleTHC()">THC Вкл</button>
                    <button class="btn" onclick="saveTHCSettings()">Сохранить</button>
                </div>
                
                <div style="background: #333; padding: 10px; border-radius: 5px; margin-top: 10px;">
                    <div style="font-size: 0.9em; color: #aaa;">Статус THC:</div>
                    <div id="thcActiveStatus" style="color: #f44336;">Не активно</div>
                </div>
            </div>

            <!-- Files Panel -->
            <div class="panel files-panel">
                <h3>Управление файлами</h3>
                
                <div class="file-upload-section">
                    <input type="file" id="fileInput" accept=".nc,.gcode,.txt" 
                           style="margin-bottom: 10px; width: 100%;">
                    <button class="btn" onclick="uploadFile()" style="width: 100%;">
                        Загрузить на SD карту
                    </button>
                </div>
                
                <div class="file-list" id="fileList" 
                     style="max-height: 200px; overflow-y: auto; background: #333; 
                            border-radius: 5px; padding: 10px; margin: 10px 0;">
                    <!-- Файлы будут здесь -->
                </div>
                
                <div class="file-controls">
                    <button class="btn btn-success" onclick="runSelectedFile()" 
                            style="margin-right: 10px;">
                        Запустить выбранный
                    </button>
                    <button class="btn btn-danger" onclick="deleteSelectedFile()">
                        Удалить выбранный
                    </button>
                </div>
                
                <div id="selectedFileInfo" style="margin-top: 10px; font-size: 0.9em; color: #888;">
                    Выберите файл для управления
                </div>
            </div>
        </div>

        <!-- Connection Status -->
        <div class="connection-status" id="connectionInfo">
            Ожидание подключения...
        </div>
    </div>

    <!-- Модальное окно редактора пресетов -->
    <div id="presetEditorModal" class="modal">
        <div class="modal-content">
            <h3 style="margin-top: 0;">Редактор пресета</h3>
            
            <div class="preset-field">
                <label>Название материала:</label>
                <input type="text" id="editPresetName" placeholder="Например: Сталь 5mm">
            </div>
            
            <div class="preset-field">
                <label>Напряжение (V):</label>
                <input type="number" id="editPresetVoltage" value="140" step="1" min="80" max="200">
            </div>
            
            <div class="preset-field">
                <label>Мертвая зона (±V):</label>
                <input type="number" id="editPresetDeadZone" value="5" step="1" min="1" max="20">
            </div>
            
            <div class="preset-field">
                <label>Скорость резки (mm/min):</label>
                <input type="number" id="editPresetSpeed" value="2000" step="50" min="500" max="5000">
            </div>
            
            <div class="preset-field">
                <label>Высота пробивки (mm):</label>
                <input type="number" id="editPresetPierceHeight" value="5.0" step="0.1" min="1" max="20">
            </div>
            
            <div class="preset-field">
                <label>Высота резки (mm):</label>
                <input type="number" id="editPresetCutHeight" value="3.0" step="0.1" min="1" max="10">
            </div>
            
            <div class="preset-field">
                <label>Задержка пробивки (s):</label>
                <input type="number" id="editPresetPierceDelay" value="0.5" step="0.1" min="0.1" max="4.0">
            </div>

            <div style="display: flex; gap: 10px; margin-top: 20px;">
                <button class="btn btn-success" onclick="savePreset()" style="flex: 1;">Сохранить пресет</button>
                <button class="btn btn-danger" onclick="closePresetEditor()" style="flex: 1;">Отмена</button>
            </div>
        </div>
    </div>

    <script>
        let ws = new WebSocket('ws://' + window.location.hostname + ':81/');
        let reconnectInterval;
        let selectedFile = null;
        let currentEditingPreset = null;
        let currentGrid = 1; // Текущий шаг сетки по умолчанию 1mm
        
        // Инициализация
        document.addEventListener('DOMContentLoaded', function() {
            setupEventListeners();
            loadFileList();
            updateGridButtons(); // Активируем кнопку сетки по умолчанию
        });
        
        function setupEventListeners() {
            // Слайдеры THC
            document.getElementById('voltageSlider').addEventListener('input', function(e) {
                document.getElementById('voltageValue').textContent = e.target.value;
            });
            
            document.getElementById('deadZoneSlider').addEventListener('input', function(e) {
                document.getElementById('deadZoneValue').textContent = e.target.value;
            });
        }
        
        // WebSocket handlers
        ws.onopen = function() {
            console.log('WebSocket connected');
            updateConnectionStatus(true);
            clearInterval(reconnectInterval);
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
            const data = JSON.parse(event.data);
            updateUI(data);
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
                document.getElementById('thcActiveStatus').textContent = data.thcActive ? 'Активно' : 'Не активно';
                document.getElementById('thcActiveStatus').style.color = data.thcActive ? '#4CAF50' : '#f44336';
            }
        }
        
        function updateConnectionStatus(connected) {
            const status = document.getElementById('connectionInfo');
            status.textContent = connected ? 'Web подключен - Система онлайн' : 'Web отключен - Переподключение...';
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
        
        // Функции обнуления координат - ИСПРАВЛЕНЫ
        function setZero(axis) {
            // Отправляем через WebSocket
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
        
        function setCurrentAsZero() {
            sendCommand('set_current_as_zero');
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
            
            // Заполняем форму данными пресета
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
            
            // Очищаем форму
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
                // Редактирование существующего пресета
                sendCommand('update_preset', { 
                    oldName: currentEditingPreset,
                    newData: presetData 
                });
            } else {
                // Создание нового пресета
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
                .then(files => {
                    const fileList = document.getElementById('fileList');
                    fileList.innerHTML = files.map(file => {
                        const safeFilename = file.replace(/'/g, "\\'").replace(/"/g, "\\\"");
                        return `<div class="file-item" onclick="selectFile('${safeFilename}')">${file}</div>`;
                    }).join('');
                })
                .catch(error => {
                    console.error('Ошибка загрузки списка файлов:', error);
                    document.getElementById('fileList').innerHTML = 'Ошибка загрузки списка файлов';
                });
        }

        function selectFile(filename) {
            selectedFile = filename;
            document.getElementById('selectedFileInfo').innerHTML = `Выбран: <strong>${filename}</strong>`;
            
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
                alert('ОК ' + result);
                loadFileList();
                fileInput.value = '';
            })
            .catch(error => {
                alert('Ошибка загрузки: ' + error);
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
                    alert('ЗАПУСК ' + result);
                })
                .catch(error => {
                    alert('Ошибка запуска: ' + error);
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
                    alert('Ошибка удаления: ' + error);
                });
            }
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

