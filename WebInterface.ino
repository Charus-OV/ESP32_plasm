#include "WebInterface.h"
#include "Config.h"
#include "StepperControl.h"
#include "PlasmaControl.h"
#include "THC_System.h"
#include "FileManager.h"

WebServer WebInterface::server(80);
WebSocketsServer WebInterface::webSocket(81);

void WebInterface::init() {
    // Запуск WiFi
    WiFi.softAP(WIFI_SSID, WIFI_PASSWORD);
    Serial.printf("📡 WiFi AP started: %s\n", WiFi.softAPIP().toString().c_str());
    
    // Маршруты обработки файлов:
    server.on("/api/files/upload", HTTP_POST, []() {
        WebInterface::server.send(200, "text/plain", "Upload complete");
    }, WebInterface::handleFileUpload);
    
    server.on("/api/files/list", HTTP_GET, WebInterface::handleFileList);
    server.on("/api/files/delete", HTTP_POST, WebInterface::handleFileDelete);
    server.on("/api/files/run", HTTP_POST, WebInterface::handleFileRun);
    
    // Настройка HTTP маршрутов
    server.on("/", WebInterface::handleRoot);
    server.on("/api/status", WebInterface::handleAPIStatus);
    server.on("/api/files", WebInterface::handleAPIFiles);
    
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

// Реализации методов обработки файлов
void WebInterface::handleFileUpload() {
    HTTPUpload& upload = server.upload();
    static File uploadFile;
    
    if(upload.status == UPLOAD_FILE_START) {
        String filename = "/" + upload.filename;
        uploadFile = SD_MMC.open(filename, FILE_WRITE);
        if(!uploadFile) {
            server.send(500, "text/plain", "Ошибка создания файла");
            return;
        }
        Serial.println("📤 Начало загрузки: " + upload.filename);
    }
    else if(upload.status == UPLOAD_FILE_WRITE) {
        if(uploadFile) {
            uploadFile.write(upload.buf, upload.currentSize);
        }
    }
    else if(upload.status == UPLOAD_FILE_END) {
        if(uploadFile) {
            uploadFile.close();
            Serial.println("✅ Загрузка завершена: " + upload.filename);
            server.send(200, "text/plain", "Файл загружен: " + upload.filename);
        }
    }
}

void WebInterface::handleFileList() {
    auto files = FileManager::listFiles();
    
    DynamicJsonDocument doc(2048);
    JsonArray filesArray = doc.to<JsonArray>();
    
    for(const String& file : files) {
        filesArray.add(file);
    }
    
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

void WebInterface::handleFileDelete() {
    String filename = server.arg("filename");
    if(FileManager::deleteFile(filename)) {
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
    doc["targetVoltage"] = THC_System::getTargetVoltage();  // Теперь float
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
        Serial.println("🛑 EMERGENCY STOP");
    } else if (command == "toggle_thc") {
        THC_System::toggle();
    } else if (command == "thc_settings") {
        float voltage = doc["voltage"];  // float!
        int deadZone = doc["deadZone"];
        float correctionRate = doc["correctionRate"];
        THC_System::setTargetVoltage(voltage);
        Serial.printf("⚙️ THC settings: voltage=%.1f, deadZone=%d, correctionRate=%.2f\n", 
                     voltage, deadZone, correctionRate);
    }
    
    sendSystemState();
}

// Функция getMainPage() с исправленным JavaScript
String WebInterface::getMainPage() {
    return R"rawliteral(
<!DOCTYPE html>
<html lang="ru">
<head>
    <meta charset="UTF-8">
    <title>Плазменный ЧПУ - ESP32-S3</title>
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <style>
        /* ... весь CSS код ... */
    </style>
</head>
<body>
    <div class="app-container">
        <!-- Header -->
        <header class="header">
            <div class="logo">
                <h1>⚡ Плазменный ЧПУ Контроллер</h1>
                <span>ESP32-S3</span>
            </div>
            <div class="status-bar">
                <div class="status-item" id="connectionStatus">🟢 Подключено</div>
                <div class="status-item" id="machineStatus">Готов</div>
                <div class="status-item" id="thcStatus">THC: Выкл</div>
                <div class="status-item" id="plasmaStatus">Плазма: Выкл</div>
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
                
                <div class="jog-container">
                    <!-- XY Jog Grid -->
                    <div class="xy-jog-grid">
                        <button class="jog-btn btn-y-plus" onclick="jog('Y+')">Y+</button>
                        <button class="jog-btn btn-y-minus" onclick="jog('Y-')">Y-</button>
                        <button class="jog-btn btn-x-minus" onclick="jog('X-')">X-</button>
                        <div class="jog-btn btn-center">XY</div>
                        <button class="jog-btn btn-x-plus" onclick="jog('X+')">X+</button>
                    </div>
                    
                    <!-- Z Jog Grid -->
                    <div class="z-jog-grid">
                        <button class="jog-btn btn-z-plus" onclick="jog('Z+')">Z+</button>
                        <button class="jog-btn btn-z-minus" onclick="jog('Z-')">Z-</button>
                    </div>
                </div>
                
                <div class="control-buttons">
                    <button class="btn btn-success" onclick="plasmaOn()">M03 - Плазма ВКЛ</button>
                    <button class="btn btn-danger" onclick="plasmaOff()">M05 - Плазма ВЫКЛ</button>
                    <button class="btn btn-warning" onclick="homeAll()">🏠 Нулевание осей</button>
                    <button class="btn btn-danger" onclick="emergencyStop()">🛑 Аварийный Стоп</button>
                </div>
            </div>

            <!-- THC Control Panel -->
            <div class="panel thc-panel">
                <h3>🎛️ Управление THC</h3>
                
                <div class="slider-group">
                    <label>Целевое напряжение: <span class="slider-value" id="voltageValue">140</span> V</label>
                    <div class="slider-container">
                        <input type="range" id="voltageSlider" min="80" max="200" value="140" step="1">
                    </div>
                </div>
                
                <div class="slider-group">
                    <label>Мертвая зона: ±<span class="slider-value" id="deadZoneValue">5</span> V</label>
                    <div class="slider-container">
                        <input type="range" id="deadZoneSlider" min="1" max="20" value="5" step="1">
                    </div>
                </div>
                
                <div class="slider-group">
                    <label>Скорость коррекции: <span class="slider-value" id="correctionValue">0.10</span> mm/s</label>
                    <div class="slider-container">
                        <input type="range" id="correctionSlider" min="0.01" max="0.5" value="0.1" step="0.01">
                    </div>
                </div>
                
                <div style="display: flex; gap: 10px; margin: 15px 0;">
                    <button class="btn btn-success" id="thcToggle" onclick="toggleTHC()">THC Вкл</button>
                    <button class="btn" onclick="saveTHCSettings()">💾 Сохранить</button>
                </div>
                
                <div style="background: #333; padding: 10px; border-radius: 5px; margin-top: 10px;">
                    <div style="font-size: 0.9em; color: #aaa;">Статус THC:</div>
                    <div id="thcActiveStatus" style="color: #f44336;">Не активно</div>
                </div>
            </div>

            <!-- Files Panel -->
            <div class="panel files-panel">
                <h3>📁 Управление файлами</h3>
                
                <div class="file-upload-section">
                    <input type="file" id="fileInput" accept=".nc,.gcode,.txt" 
                           style="margin-bottom: 10px; width: 100%;">
                    <button class="btn" onclick="uploadFile()" style="width: 100%;">
                        📤 Загрузить на SD карту
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
                        ▶️ Запустить выбранный
                    </button>
                    <button class="btn btn-danger" onclick="deleteSelectedFile()">
                        🗑️ Удалить выбранный
                    </button>
                </div>
                
                <div id="selectedFileInfo" style="margin-top: 10px; font-size: 0.9em; color: #888;">
                    Выберите файл для управления
                </div>
            </div>
        </div>

        <!-- Connection Status -->
        <div class="connection-status" id="connectionInfo">
            🔴 Ожидание подключения WebSocket...
        </div>
    </div>

    <script>
        let ws = new WebSocket('ws://' + window.location.hostname + ':81/');
        let reconnectInterval;
        let selectedFile = null;
        
        // Инициализация
        document.addEventListener('DOMContentLoaded', function() {
            setupEventListeners();
            loadFileList(); // Загружаем список файлов при старте
        });
        
        function setupEventListeners() {
            // Слайдеры THC
            document.getElementById('voltageSlider').addEventListener('input', function(e) {
                document.getElementById('voltageValue').textContent = e.target.value;
            });
            
            document.getElementById('deadZoneSlider').addEventListener('input', function(e) {
                document.getElementById('deadZoneValue').textContent = e.target.value;
            });
            
            document.getElementById('correctionSlider').addEventListener('input', function(e) {
                document.getElementById('correctionValue').textContent = parseFloat(e.target.value).toFixed(2);
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
            // Попытка переподключения
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
            status.textContent = connected ? '🟢 WebSocket подключен - Система онлайн' : '🔴 WebSocket отключен - Переподключение...';
            status.style.background = connected ? '#4CAF50' : '#f44336';
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
        
        function jog(direction) {
            sendCommand('jog', { direction: direction, distance: 10 });
        }
        
        function saveTHCSettings() {
            const settings = {
                voltage: parseInt(document.getElementById('voltageSlider').value),
                deadZone: parseInt(document.getElementById('deadZoneSlider').value),
                correctionRate: parseFloat(document.getElementById('correctionSlider').value)
            };
            sendCommand('thc_settings', settings);
            alert('Настройки THC сохранены!');
        }
        
        // Функции управления файлами
        function loadFileList() {
            fetch('/api/files/list')
                .then(response => response.json())
                .then(files => {
                    const fileList = document.getElementById('fileList');
                    // Экранируем специальные символы в именах файлов
                    fileList.innerHTML = files.map(file => {
                        const safeFilename = file.replace(/'/g, "\\'").replace(/"/g, "\\\"");
                        return `<div class="file-item" onclick="selectFile('${safeFilename}')" 
                              style="padding: 8px; border-bottom: 1px solid #444; cursor: pointer; margin: 2px 0; border-radius: 3px;">
                         📄 ${file}
                         </div>`;
                    }).join('');
                })
                .catch(error => {
                    console.error('Ошибка загрузки списка файлов:', error);
                    document.getElementById('fileList').innerHTML = 'Ошибка загрузки списка файлов';
                });
        }

        function selectFile(filename) {
            selectedFile = filename;
            document.getElementById('selectedFileInfo').innerHTML = 
                `📄 Выбран: <strong>${filename}</strong>`;
            
            // Подсветка выбранного файла
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

        // Вспомогательные функции
        function sendCommand(command, data = {}) {
            const message = { command, ...data };
            if(ws.readyState === WebSocket.OPEN) {
                ws.send(JSON.stringify(message));
            } else {
                alert('WebSocket не подключен. Команда не отправлена.');
            }
        }
    </script>
</body>
</html>
)rawliteral";
}