
#include <WiFi.h>
#include <SPI.h>
#include <SD.h>
#include <Preferences.h>
#include <mcp_can.h>

/* ============================================================
   PIN CONFIGURATION
============================================================ */

#define SD_CS 4
#define SD_SCK 18
#define SD_MISO 19
#define SD_MOSI 23

#define CAN_CS 5
#define CAN_INT 27

MCP_CAN CAN0(CAN_CS);

long unsigned int rxId;
unsigned char len = 0;
unsigned char rxBuf[8];
byte tpoBuf[8];

/* ============================================================
   NETWORK SERVER
============================================================ */

WiFiServer server8998(8998);

WiFiClient serverClient8989;
WiFiClient serverClient8998;

/* ============================================================
   CONFIG STORAGE (Preferences)
============================================================ */

Preferences configStorage;

typedef struct
{
  char site[16];
  char vehicleType[16];
  char unitNumber[16];

  char wifiSSID[32];
  char wifiPassword[32];
  char serverIP[32];

  char destination[16];
  char username[16];
  char password[16];

  char rpmGenerator[4][8];
  char overspeedLimit[4][8];

  char coastingSpeed[8];
  char diffLockLimit[8];

  char timerWarning[4][8];
  char timerRelease[4][8];

  char volumeLevel[8];
  char vadThreshold[4][8];

} DeviceConfiguration;

DeviceConfiguration deviceConfig;

/* ============================================================
   RTOS HANDLES
============================================================ */

TaskHandle_t wifiTaskHandle;
TaskHandle_t socketTaskHandle;
TaskHandle_t serialTaskHandle;
TaskHandle_t sdTaskHandle;
TaskHandle_t canTaskHandle;

/* ============================================================
   MUTEX
============================================================ */

SemaphoreHandle_t spiMutex;

/* ============================================================
   LOG QUEUE
============================================================ */

#define LOG_QUEUE_SIZE 100
#define LOG_ITEM_SIZE 128

typedef struct
{
  char data[LOG_ITEM_SIZE];
} LogItem;

QueueHandle_t logQueue;

const char *logFile = "/buffer.log";

/* ============================================================
   DEBUG SYSTEM
============================================================ */

bool debugEnabled = false;
unsigned long lastDebug = 0;

/* ============================================================
   WIFI
============================================================ */

const char *apPassword = "12345678";

/* ============================================================
   SOCKET DEBUG HELPERS
============================================================ */

void debugRx(const char* port,const char* msg)
{
  Serial.print("[RX ");
  Serial.print(port);
  Serial.print("] ");
  Serial.println(msg);
}

void debugTx(const char* port,const char* msg)
{
  Serial.print("[TX ");
  Serial.print(port);
  Serial.print("] ");
  Serial.println(msg);
}

/* ============================================================
   HASH UTIL
============================================================ */

void joinHash(char src[4][8], char *out) {
  sprintf(out, "%s#%s#%s#%s", src[0], src[1], src[2], src[3]);
}

void splitHash(const char *src, char dest[4][8]) {
  int index = 0, pos = 0;

  for (int i = 0; src[i] && index < 4; i++) {
    if (src[i] == '#') {
      dest[index][pos] = 0;
      index++;
      pos = 0;
    } else {
      if (pos < 7) dest[index][pos++] = src[i];
    }
  }

  dest[index][pos] = 0;
}

/* ============================================================
   EXPORT CONFIG
============================================================ */

String buildExportFrame() {
  String frame = "VITA~";
  char buffer[64];

  frame += deviceConfig.site; frame += "~";
  frame += deviceConfig.vehicleType; frame += "~";
  frame += deviceConfig.unitNumber; frame += "~";
  frame += deviceConfig.wifiSSID; frame += "~";
  frame += deviceConfig.wifiPassword; frame += "~";
  frame += deviceConfig.serverIP; frame += "~";
  frame += deviceConfig.destination; frame += "~";
  frame += deviceConfig.username; frame += "~";
  frame += deviceConfig.password; frame += "~";

  joinHash(deviceConfig.rpmGenerator, buffer);
  frame += buffer; frame += "~";

  joinHash(deviceConfig.overspeedLimit, buffer);
  frame += buffer; frame += "~";

  frame += deviceConfig.coastingSpeed; frame += "~";
  frame += deviceConfig.diffLockLimit; frame += "~";

  joinHash(deviceConfig.timerWarning, buffer);
  frame += buffer; frame += "~";

  joinHash(deviceConfig.timerRelease, buffer);
  frame += buffer; frame += "~";

  frame += deviceConfig.volumeLevel; frame += "~";

  joinHash(deviceConfig.vadThreshold, buffer);
  frame += buffer;

  return frame;
}

/* ============================================================
   SAVE CONFIG
============================================================ */

void saveConfig() {
  configStorage.begin("devicecfg", false);

  char buffer[64];

  configStorage.putString("site", deviceConfig.site);
  configStorage.putString("type", deviceConfig.vehicleType);
  configStorage.putString("unit", deviceConfig.unitNumber);

  configStorage.putString("ssid", deviceConfig.wifiSSID);
  configStorage.putString("pwdwifi", deviceConfig.wifiPassword);

  configStorage.putString("server", deviceConfig.serverIP);

  configStorage.putString("dest", deviceConfig.destination);
  configStorage.putString("user", deviceConfig.username);
  configStorage.putString("pwd", deviceConfig.password);

  joinHash(deviceConfig.rpmGenerator, buffer);
  configStorage.putString("rpm", buffer);

  joinHash(deviceConfig.overspeedLimit, buffer);
  configStorage.putString("overspeed", buffer);

  configStorage.putString("coasting", deviceConfig.coastingSpeed);
  configStorage.putString("difflock", deviceConfig.diffLockLimit);

  joinHash(deviceConfig.timerWarning, buffer);
  configStorage.putString("warn", buffer);

  joinHash(deviceConfig.timerRelease, buffer);
  configStorage.putString("release", buffer);

  configStorage.putString("volume", deviceConfig.volumeLevel);

  joinHash(deviceConfig.vadThreshold, buffer);
  configStorage.putString("vad", buffer);

  configStorage.end();
}

/* ============================================================
   LOGGING
============================================================ */

void sdWriteLine(const char *line) {
  if (xSemaphoreTake(spiMutex, pdMS_TO_TICKS(200)) != pdTRUE) return;

  File f = SD.open(logFile, FILE_APPEND);

  if (f) {
    f.println(line);
    f.close();
  }

  xSemaphoreGive(spiMutex);
}

void sdTask(void *pv) {
  LogItem item;

  for (;;) {
    if (xQueueReceive(logQueue, &item, portMAX_DELAY) == pdTRUE)
      sdWriteLine(item.data);
  }
}

void logData(const char *msg) {

  if (serverClient8989 && serverClient8989.connected()) {
    serverClient8989.println(msg);
    debugTx("8989",msg);
    return;
  }

  LogItem item;

  strncpy(item.data, msg, LOG_ITEM_SIZE - 1);
  item.data[LOG_ITEM_SIZE - 1] = 0;

  xQueueSend(logQueue, &item, 0);
}

/* ================= IMPORT ================= */

void parseSaveFrame(char *frame) {

  memset(&deviceConfig, 0, sizeof(deviceConfig));

  char *token;
  int index = 0;

  token = strtok(frame, "~");

  while (token != NULL) {

    switch (index) {

      case 1: strncpy(deviceConfig.site, token, sizeof(deviceConfig.site)); break;
      case 2: strncpy(deviceConfig.vehicleType, token, sizeof(deviceConfig.vehicleType)); break;
      case 3: strncpy(deviceConfig.unitNumber, token, sizeof(deviceConfig.unitNumber)); break;

      case 4: strncpy(deviceConfig.wifiSSID, token, sizeof(deviceConfig.wifiSSID)); break;
      case 5: strncpy(deviceConfig.wifiPassword, token, sizeof(deviceConfig.wifiPassword)); break;

      case 6: strncpy(deviceConfig.serverIP, token, sizeof(deviceConfig.serverIP)); break;

      case 7: strncpy(deviceConfig.destination, token, sizeof(deviceConfig.destination)); break;
      case 8: strncpy(deviceConfig.username, token, sizeof(deviceConfig.username)); break;
      case 9: strncpy(deviceConfig.password, token, sizeof(deviceConfig.password)); break;

      case 10: splitHash(token, deviceConfig.rpmGenerator); break;
      case 11: splitHash(token, deviceConfig.overspeedLimit); break;

      case 12: strncpy(deviceConfig.coastingSpeed, token, sizeof(deviceConfig.coastingSpeed)); break;
      case 13: strncpy(deviceConfig.diffLockLimit, token, sizeof(deviceConfig.diffLockLimit)); break;

      case 14: splitHash(token, deviceConfig.timerWarning); break;
      case 15: splitHash(token, deviceConfig.timerRelease); break;

      case 16: strncpy(deviceConfig.volumeLevel, token, sizeof(deviceConfig.volumeLevel)); break;
      case 17: splitHash(token, deviceConfig.vadThreshold); break;
    }

    token = strtok(NULL, "~");
    index++;
  }
}

/* ============================================================
   SEND COMMAND
============================================================ */

void processSendCommand(char *cmd) {

  char frame[256];
  strncpy(frame,cmd,sizeof(frame));

  if (serverClient8989 && serverClient8989.connected()) {
    serverClient8989.println(frame);
    debugTx("8989",frame);
  }

  Serial.println(frame);
}

/* ============================================================
   DYNAMIC AP NAME
============================================================ */

String currentSSID = "";

void updateAccessPoint()
{
  String newSSID;

  if(strlen(deviceConfig.unitNumber) == 0)
    newSSID = "VITA-Gen3";
  else
    newSSID = String(deviceConfig.unitNumber);

  if(newSSID != currentSSID)
  {
    Serial.print("Updating AP SSID -> ");
    Serial.println(newSSID);

    WiFi.softAPdisconnect(true);
    delay(200);

    WiFi.softAP(newSSID.c_str(), apPassword);

    currentSSID = newSSID;
  }
}

/* =

/* ============================================================
   DEBUG
============================================================ */

void sendDebug() {

  if (!debugEnabled) return;
  if (millis() - lastDebug < 1000) return;

  lastDebug = millis();

  char msg[80];

  sprintf(msg, "DEBUG~%s~IP:%s",
          deviceConfig.unitNumber,
          WiFi.localIP().toString().c_str());

  if (serverClient8998 && serverClient8998.connected()) {
    serverClient8998.println(msg);
    debugTx("8998",msg);
  }

  Serial.println(msg);
}

void sendExport(String frame)
{
  Serial.println(frame);

  if (serverClient8989 && serverClient8989.connected())
    serverClient8989.println(frame);

  if (serverClient8998 && serverClient8998.connected())
    serverClient8998.println(frame);
}

/* ============================================================
   COMMAND PROCESSOR
============================================================ */

void processCommand(char *cmd) {
  if (strcmp(cmd, "VITA?") == 0)
  {
    sendExport(buildExportFrame());
    return;
  }
 if (strncmp(cmd, "SAVE~", 5) == 0)
{
  char frame[512];

  strncpy(frame, cmd, sizeof(frame) - 1);
  frame[sizeof(frame) - 1] = 0;

  parseSaveFrame(frame);
  saveConfig();

  Serial.println("CONFIG SAVED");
  return;
}
  if (strcmp(cmd, "LOGON!") == 0) {
    debugEnabled = true;
    return;
  }

  if (strcmp(cmd, "LOGOFF!") == 0) {
    debugEnabled = false;
    return;
  }

  if (strncmp(cmd, "SEND~", 5) == 0) {
    processSendCommand(cmd);
    return;
  }

  if (strncmp(cmd, "LOG~", 4) == 0) {
    logData(cmd + 4);
    return;
  }
}

/* ============================================================
   CAN
============================================================ */

void startCan()
{
  if (CAN0.begin(MCP_ANY, CAN_500KBPS, MCP_16MHZ) == CAN_OK)
    Serial.println("MCP2515 OK");
  else
    Serial.println("MCP2515 FAIL");

  CAN0.setMode(MCP_LISTENONLY);
  pinMode(CAN_INT, INPUT);
}

void handleSpeed()
{
  int speed = tpoBuf[0];
  char msg[64];
  sprintf(msg,"CAN_SPEED:%d",speed);
  Serial.println(msg);
}

void canTask(void *pv)
{
  for(;;)
  {
    if(!digitalRead(CAN_INT))
    {
      if(xSemaphoreTake(spiMutex,pdMS_TO_TICKS(5))==pdTRUE)
      {
        CAN0.readMsgBuf(&rxId,&len,rxBuf);
        xSemaphoreGive(spiMutex);

        uint32_t canId = rxId & 0x1FFFFFFF;
        memcpy(tpoBuf,rxBuf,8);

        switch(canId)
        {
          case 0x1802F3EF:
            handleSpeed();
            break;
        }
      }
    }
    vTaskDelay(1);
  }
}
/* ============================================================
   WIFI TASK
============================================================ */

void wifiTask(void *pv) {

  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);

  updateAccessPoint();

  server8998.begin();

  for (;;) {

    updateAccessPoint();
    sendDebug();

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}


/* ============================================================
   SOCKET TASK
============================================================ */

void socketTask(void *pv) {

  char buffer[256];

  for (;;) {

    // if (!serverClient8989 || !serverClient8989.connected())
    // serverClient8989 = server8998.available();
    // if (serverClient8989 && serverClient8989.available()) {

    //   int len = serverClient8989.readBytesUntil('\n', buffer, sizeof(buffer)-1);
    //   buffer[len] = 0;

    //   debugRx("8989",buffer);
    //   processCommand(buffer);
    // }

    if (!serverClient8998 || !serverClient8998.connected())
      serverClient8998 = server8998.available();

    if (serverClient8998 && serverClient8998.available()) {

      int len = serverClient8998.readBytesUntil('\n', buffer, sizeof(buffer)-1);
      buffer[len] = 0;

      debugRx("8998",buffer);
      processCommand(buffer);
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

/* ============================================================
   SERIAL TASK
============================================================ */

void serialTask(void *pv) {

  char buffer[256];
  int index = 0;

  for (;;) {

    while (Serial.available()) {

      char c = Serial.read();

      if (c == '\n') {
        buffer[index] = 0;
        processCommand(buffer);
        index = 0;
      }
      else if (index < 255)
        buffer[index++] = c;
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

/* ============================================================
   SETUP
============================================================ */

void setup() {

  Serial.begin(115200);

  spiMutex = xSemaphoreCreateMutex();

  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);

  if (!SD.begin(SD_CS))
    Serial.println("SD INIT FAIL");
  else
    Serial.println("SD INIT OK");

  logQueue = xQueueCreate(LOG_QUEUE_SIZE, sizeof(LogItem));

  startCan();

  xTaskCreatePinnedToCore(wifiTask,"wifiTask",4096,NULL,1,&wifiTaskHandle,0);
  xTaskCreatePinnedToCore(socketTask,"socketTask",4096,NULL,1,&socketTaskHandle,1);
  xTaskCreatePinnedToCore(serialTask,"serialTask",4096,NULL,1,&serialTaskHandle,1);
  xTaskCreatePinnedToCore(sdTask,"sdTask",4096,NULL,1,&sdTaskHandle,1);
  xTaskCreatePinnedToCore(canTask,"canTask",4096,NULL,2,&canTaskHandle,1);

  Serial.println("VITA CONTROLLER STARTED");
}

void loop() {}
