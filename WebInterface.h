#ifndef WEBINTERFACE_H
#define WEBINTERFACE_H

#include <WebServer.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include <SD_MMC.h>

class WebInterface {
public:
    static void init();
    static void handleClient();
    
    // Статические методы для обработки файлов
    static void handleFileUpload();
    static void handleFileList();
    static void handleFileDelete();
    static void handleFileRun();

private:
    static WebServer server;
    static WebSocketsServer webSocket;
    
    static void handleRoot();
    static void handleAPIStatus();
    static void handleAPIFiles();
    static void handleWebSocket(uint8_t num, WStype_t type, uint8_t* payload, size_t length);
    static void processWebSocketCommand(String message);
    static void sendSystemState();
    static String getMainPage();
};

#endif