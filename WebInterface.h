#ifndef WEBINTERFACE_H
#define WEBINTERFACE_H

#include <WebServer.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>

class WebInterface {
public:
    static void init();
    static void handleClient();
    static void sendGCodeResponse(const String& response);
    static void sendGCodeState(const String& state);
    
private:
    static WebServer server;
    static WebSocketsServer webSocket;
    
    // HTTP обработчики
    static void handleRoot();
    static void handleAPIStatus();
    static void handleAPIFiles();
    static void handleSetZero();
    static void handleGCodeCommand();
    
    // Обработчики файлов
    static void handleFileUpload();
    static void handleFileList();
    static void handleFileDelete();
    static void handleFileRun();
    
    // WebSocket обработчики
    static void handleWebSocket(uint8_t num, WStype_t type, uint8_t* payload, size_t length);
    static void processWebSocketCommand(String message);
    static void sendSystemState();
    
    // G-code функции
    static void startGCodeFromLine(const String& filename, int startLine);
    static void processNextGCodeLine();
    static void executeGCodeLine(const String& line);
    static void handlePlasmaOutage();
    static void resumeAfterPlasmaRecovery();
    
    // HTML страница
    static String getMainPage();
};

#endif
