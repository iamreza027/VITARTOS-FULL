
#include <WiFi.h>
#include <SPI.h>
#include <SD.h>
#include <Preferences.h>
#include <mcp_can.h>
#include <Wire.h>
#include "RTClib.h"
#include "DFRobotDFPlayerMini.h"
#include <HardwareSerial.h>

/* ============================================================
   PIN CONFIGURATION
============================================================ */
#define SD_CS 4
#define SD_SCK 18
#define SD_MISO 19
#define SD_MOSI 23
#define CAN_CS 5
#define CAN_INT 27
#define EXTERNAL_SERIAL_RX_PIN 14
#define EXTERNAL_SERIAL_TX_PIN 12

//RTC
RTC_DS3231 rtc;

//UART
HardwareSerial externalSerial(1);

//DFPlayer
HardwareSerial DFSerial(2);
DFRobotDFPlayerMini dfPlayer;

bool audioCondition = false;
uint8_t audioFile = 0;

//MCP
MCP_CAN CAN0(CAN_CS);

long unsigned int rxId;
unsigned char len = 0;
unsigned char rxBuf[8];
byte tpoBuf[8];

//Sensor Value CAN BUS
word speed = 0;

//Variable Global Event
uint32_t neutralStartTime = 0;
bool costingNeutralActive = false;
bool overspeedActive = false;

/* ============================================================
   NETWORK SERVER
============================================================ */

WiFiServer server8998(8998);  // ESP32 SERVER

WiFiClient serverClient;
WiFiClient incomingClient;  // client yang connect ke ESP32:8998

//Auto Reconnect
unsigned long lastReconnect8989 = 0;


const uint32_t reconnectInterval = 5000;

unsigned long lastWifiReconnect = 0;
const uint32_t wifiReconnectInterval = 5000;

//Variable Test Over Speed by Command
// bool speedSimulation = false;
// int simulatedSpeed = 0;


/* ============================================================
   CONFIG STORAGE (Preferences)
============================================================ */

Preferences configStorage;

typedef struct {
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

  char vad[2];
  char IDCardNow[16];
  char IDCardBefor[16];
  char WaktuDelayLowPower[4];

} DeviceConfiguration;

DeviceConfiguration deviceConfig;

/* ============================================================
  Event VAD (Preferences)
============================================================ */
Preferences VADStorage;
//0~0~0~0~0~0~0~0~0
typedef struct
{
  char HuntingGear212[3];
  char HuntingGear323[3];
  char CoastingNetral[3];
  char TMAbuse[3];
  char Spining[3];
  char SpiningMundur[3];
  char OverSpeedMuatan[3];
  char OverSpeedKosongan[3];
  char MundurJauh[3];
} VAD;
VAD HistoryVAD;
/* ============================================================
  Struct data CAN
============================================================ */
typedef struct
{
  int speed;
  int gear;
  int rpmGenerator;

  bool validSpeed;
  bool validGear;
  bool validRPM;

  uint32_t lastUpdate;

  // SIMULATION
  bool simSpeedEnable;
  int simSpeed;

} CANSignals;

CANSignals canData;
/* ============================================================
  Layout Sending data VTA/EVENT
============================================================ */
typedef struct
{
  char event[4];
  char kodeST[4];
  int valueSensor;
  char dateStr[16];
  char timeStr[16];
} EventItem;
/* ============================================================
  UART
=============================================================== */
typedef struct
{
  char frame[64];
} ExternalSerialFrame;


/* ============================================================
  Struct Audio
=============================================================== */
enum AudioEvent {
  AUDIO_OVERSPEED = 1,
  AUDIO_COASTING,
  AUDIO_CAN_ERROR,
  AUDIO_RTC_ERROR,
  AUDIO_DATA_MISSING
};
/* ============================================================
  Function RTC
============================================================ */
void fillEventTime(EventItem *item) {
  DateTime now = rtc.now();

  sprintf(item->dateStr, "%04d-%02d-%02d",
          now.year(),
          now.month(),
          now.day());

  sprintf(item->timeStr, "%02d:%02d:%02d",
          now.hour(),
          now.minute(),
          now.second());
}

void commandTime() {  // Fucntion untuk  TIME?
  EventItem item;

  fillEventTime(&item);

  char buffer[64];

  sprintf(buffer, "TIME:%s %s", item.dateStr, item.timeStr);

  sendSocket(buffer);
  Serial.println(buffer);
}

void commandSetTime(char *cmd) {  //Function untuk SET TIME
  int year, month, day, hour, minute, second;

  if (sscanf(cmd, "SETTIME %d-%d-%d %d:%d:%d",
             &year, &month, &day,
             &hour, &minute, &second)
      == 6) {
    rtc.adjust(DateTime(year, month, day, hour, minute, second));

    sendSocket("RTC SET OK");
    Serial.println("RTC SET OK");
  } else {
    sendSocket("FORMAT: SETTIME YYYY-MM-DD HH:MM:SS");
  }
}
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
   Queue
============================================================ */
QueueHandle_t logQueue;
QueueHandle_t eventQueue;
QueueHandle_t sendQueue;
QueueHandle_t audioQueue;
QueueHandle_t externalSerialQueue;
/* ============================================================
   LOG QUEUE
============================================================ */

#define LOG_QUEUE_SIZE 100
#define LOG_ITEM_SIZE 128

typedef struct
{
  char data[LOG_ITEM_SIZE];
} LogItem;


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

void debugRx(const char *port, const char *msg) {
  Serial.print("[RX ");
  Serial.print(port);
  Serial.print("] ");
  Serial.println(msg);
}

void debugTx(const char *port, const char *msg) {
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

  frame += deviceConfig.site;
  frame += "~";
  frame += deviceConfig.vehicleType;
  frame += "~";
  frame += deviceConfig.unitNumber;
  frame += "~";
  frame += deviceConfig.wifiSSID;
  frame += "~";
  frame += deviceConfig.wifiPassword;
  frame += "~";
  frame += deviceConfig.serverIP;
  frame += "~";
  frame += deviceConfig.destination;
  frame += "~";
  frame += deviceConfig.username;
  frame += "~";
  frame += deviceConfig.password;
  frame += "~";

  joinHash(deviceConfig.rpmGenerator, buffer);
  frame += buffer;
  frame += "~";

  joinHash(deviceConfig.overspeedLimit, buffer);
  frame += buffer;
  frame += "~";

  frame += deviceConfig.coastingSpeed;
  frame += "~";
  frame += deviceConfig.diffLockLimit;
  frame += "~";

  joinHash(deviceConfig.timerWarning, buffer);
  frame += buffer;
  frame += "~";

  joinHash(deviceConfig.timerRelease, buffer);
  frame += buffer;
  frame += "~";

  frame += deviceConfig.volumeLevel;
  frame += "~";

  joinHash(deviceConfig.vadThreshold, buffer);
  frame += buffer;

  return frame;
}

/* ============================================================
   SAVE CONFIG
============================================================ */

void saveConfig() {
  configStorage.begin("devicecfg", false);

  char buffer[128];

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
   LOAD CONFIG
============================================================ */

void loadConfig() {
  configStorage.begin("devicecfg", true);

  String s;

  s = configStorage.getString("site", "");
  strncpy(deviceConfig.site, s.c_str(), sizeof(deviceConfig.site));

  s = configStorage.getString("type", "");
  strncpy(deviceConfig.vehicleType, s.c_str(), sizeof(deviceConfig.vehicleType));

  s = configStorage.getString("unit", "");
  strncpy(deviceConfig.unitNumber, s.c_str(), sizeof(deviceConfig.unitNumber));

  s = configStorage.getString("ssid", "");
  strncpy(deviceConfig.wifiSSID, s.c_str(), sizeof(deviceConfig.wifiSSID));

  s = configStorage.getString("pwdwifi", "");
  strncpy(deviceConfig.wifiPassword, s.c_str(), sizeof(deviceConfig.wifiPassword));

  s = configStorage.getString("server", "");
  strncpy(deviceConfig.serverIP, s.c_str(), sizeof(deviceConfig.serverIP));

  s = configStorage.getString("dest", "");
  strncpy(deviceConfig.destination, s.c_str(), sizeof(deviceConfig.destination));

  s = configStorage.getString("user", "");
  strncpy(deviceConfig.username, s.c_str(), sizeof(deviceConfig.username));

  s = configStorage.getString("pwd", "");
  strncpy(deviceConfig.password, s.c_str(), sizeof(deviceConfig.password));

  char buffer[128];

  s = configStorage.getString("rpm", "");
  strncpy(buffer, s.c_str(), sizeof(buffer));
  splitHash(buffer, deviceConfig.rpmGenerator);

  s = configStorage.getString("overspeed", "");
  strncpy(buffer, s.c_str(), sizeof(buffer));
  splitHash(buffer, deviceConfig.overspeedLimit);

  s = configStorage.getString("coasting", "");
  strncpy(deviceConfig.coastingSpeed, s.c_str(), sizeof(deviceConfig.coastingSpeed));

  s = configStorage.getString("difflock", "");
  strncpy(deviceConfig.diffLockLimit, s.c_str(), sizeof(deviceConfig.diffLockLimit));

  s = configStorage.getString("warn", "");
  strncpy(buffer, s.c_str(), sizeof(buffer));
  splitHash(buffer, deviceConfig.timerWarning);

  s = configStorage.getString("release", "");
  strncpy(buffer, s.c_str(), sizeof(buffer));
  splitHash(buffer, deviceConfig.timerRelease);

  s = configStorage.getString("volume", "");
  strncpy(deviceConfig.volumeLevel, s.c_str(), sizeof(deviceConfig.volumeLevel));

  s = configStorage.getString("vad", "");
  strncpy(buffer, s.c_str(), sizeof(buffer));
  splitHash(buffer, deviceConfig.vadThreshold);

  configStorage.end();

  Serial.println("CONFIG LOADED");
}
/* ============================================================
                        SD UTIL
============================================================ */

/*
Ensure filename always begins with '/'
*/
void normalizePath(const char *name, char *out) {
  if (name[0] != '/')
    sprintf(out, "/%s", name);
  else
    strcpy(out, name);
}

/* ============================================================
   LOGGING
============================================================ */
/* ============================================================
                        SD FILE DELETE
============================================================ */

void fileDelete(const char *name) {
  char path[64];
  normalizePath(name, path);

  if (xSemaphoreTake(spiMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
    if (SD.exists(path))
      SD.remove(path);

    xSemaphoreGive(spiMutex);
  }
}

/* ============================================================
                    DELETE ALL FILES (FIXED)
============================================================ */
void deleteAllFiles() {
  if (xSemaphoreTake(spiMutex, pdMS_TO_TICKS(500)) != pdTRUE)
    return;

  File root = SD.open("/");

  if (!root) {
    xSemaphoreGive(spiMutex);
    return;
  }

  while (true) {
    File entry = root.openNextFile();

    if (!entry)
      break;

    if (!entry.isDirectory()) {
      char path[64];

      snprintf(path, sizeof(path), "/%s", entry.name());

      SD.remove(path);
    }

    entry.close();

    vTaskDelay(1);  // yield RTOS
  }

  root.close();

  xSemaphoreGive(spiMutex);



  char msg[] = "All File Is Delete";
  sendSocket(msg);

  Serial.println("All File Is Delete");
}

/* ============================================================
                        COUNT FILE
============================================================ */

int fileCount() {
  int count = 0;

  if (xSemaphoreTake(spiMutex, pdMS_TO_TICKS(200)) != pdTRUE)
    return 0;

  File root = SD.open("/");

  while (true) {
    File file = root.openNextFile();

    if (!file)
      break;

    count++;
    file.close();
  }

  root.close();

  xSemaphoreGive(spiMutex);

  return count;
}

/* ============================================================
                        LIST FILES
============================================================ */
void listFiles() {

  if (xSemaphoreTake(spiMutex, pdMS_TO_TICKS(200)) != pdTRUE)
    return;

  File root = SD.open("/");

  char msg[96];

  while (true) {

    File file = root.openNextFile();

    if (!file)
      break;

    sprintf(msg, "FILE:%s", file.name());

    sendSocket(msg);
    Serial.println(msg);

    file.close();
  }

  root.close();

  sendSocket("END FILE LIST");

  xSemaphoreGive(spiMutex);
}

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
  sendSocket(msg);

  LogItem item;

  strncpy(item.data, msg, LOG_ITEM_SIZE - 1);
  item.data[LOG_ITEM_SIZE - 1] = 0;

  xQueueSend(logQueue, &item, 0);
}
//Function Penomoran file
int getNextFileNumber() {
  int index = 1;
  char name[32];

  while (true) {
    sprintf(name, "/EVT%04d.txt", index);

    if (!SD.exists(name))
      return index;

    index++;
  }
}
//Function Penulisan data Event to SD Card
void saveEventToSD(EventItem item) {

  fillEventTime(&item);  // isi tanggal & jam dari RTC

  int num = getNextFileNumber();

  char filename[32];
  sprintf(filename, "/EVT%04d.txt", num);

  File f = SD.open(filename, FILE_WRITE);

  if (!f) return;

  f.printf("%s,%s,%s,%s,%d,%s,%s,%s,%s\n",
           deviceConfig.unitNumber,
           deviceConfig.IDCardNow,
           item.event,
           item.kodeST,
           item.valueSensor,
           deviceConfig.vad,
           item.dateStr,
           item.timeStr,
           deviceConfig.site);

  f.close();

  Serial.print("EVENT SAVED ");
  Serial.println(filename);
}
int findOldestEventFile() {
  for (int i = 1; i < 10000; i++) {
    char name[32];

    sprintf(name, "/EVT%04d.txt", i);

    if (SD.exists(name))
      return i;
  }

  return 0;
}
void deleteOldestEventFile() {  //Function Menghapus File yang sudah terkirim ke Server TxWave
  int index = findOldestEventFile();

  if (index == 0) return;

  char filename[32];

  sprintf(filename, "/EVT%04d.txt", index);

  SD.remove(filename);

  Serial.print("EVENT SENT DELETE ");
  Serial.println(filename);
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
   SEND Data Event
============================================================ */
void sendTask(void *pv) {
  char filename[32];
  char buffer[256];

  for (;;) {
    if (!serverClient.connected()) {
      vTaskDelay(pdMS_TO_TICKS(5000));
      continue;
    }

    int fileIndex = findOldestEventFile();

    if (fileIndex == 0) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    sprintf(filename, "/EVT%04d.txt", fileIndex);

    File f = SD.open(filename);

    if (!f) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    String line = f.readStringUntil('\n');
    f.close();

    sendEventFrame(line.c_str());

    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}

void sendEventFrame(const char *line) {  //Frame untuk pengiriman ke Server TxWave
  char frame[256];

  sprintf(frame,
          "SEND~B55~VTA(%s)~%s~%s~1~",
          line,
          deviceConfig.password,
          deviceConfig.username);

  sendSocket(frame);

  Serial.println(frame);
}
/* ============================================================
   SEND COMMAND
============================================================ */
void processSendCommand(char *cmd) {
  char frame[256];
  strncpy(frame, cmd, sizeof(frame));

  if (serverClient.connected()) {
    serverClient.println(frame);
    debugTx("SERVER", frame);
  }

  Serial.println(frame);
}

void sendEvent1() {

  char frame[256];

  EventItem item;

  strcpy(item.event, "V5");
  strcpy(item.kodeST, "1");
  item.valueSensor = 70;

  // isi waktu dari RTC
  fillEventTime(&item);

  sprintf(frame,
          "SEND~B55~VTA('%s','%s','%s','%s','%d','%s','%s','%s','%s')~%s~%s~1~",
          deviceConfig.unitNumber,
          deviceConfig.IDCardNow,
          item.event,
          item.kodeST,
          item.valueSensor,
          deviceConfig.vad,
          item.dateStr,
          item.timeStr,
          deviceConfig.site,
          deviceConfig.password,
          deviceConfig.username);

  sendSocket(frame);

  debugTx("EVENT1", frame);

  Serial.println(frame);
}

/* ============================================================
   DYNAMIC AP NAME
============================================================ */

String currentSSID = "";

void updateAccessPoint() {
  String newSSID;

  if (strlen(deviceConfig.unitNumber) == 0)
    newSSID = "VITA-Gen3";
  else
    newSSID = String(deviceConfig.unitNumber);

  if (newSSID != currentSSID) {
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

  EventItem item;

  if (!debugEnabled) return;
  if (millis() - lastDebug < 1000) return;

  lastDebug = millis();

  fillEventTime(&item);

  char msg[128];

  snprintf(msg, sizeof(msg),
           "IDCardNow=%s,IDCardLast=%s,Speed=%d,Gear=%d,Date=%s,Time=%s",
           deviceConfig.IDCardNow,
           deviceConfig.IDCardBefor,
           CAN_getSpeed(),
           canData.gear,
           item.dateStr,
           item.timeStr);

  sendSocket(msg);

  Serial.println(msg);
  Serial.print("Speed Source: ");

  if (canData.simSpeedEnable)
    Serial.println("SIMULATION");
  else
    Serial.println("CAN BUS");
}
void sendExport(String frame) {
  Serial.println(frame);

  sendSocket(frame.c_str());
}

void debugWiFiStatus() {
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("===== WIFI CONNECTED =====");

    Serial.print("SSID : ");
    Serial.println(WiFi.SSID());

    Serial.print("IP   : ");
    Serial.println(WiFi.localIP());

    Serial.print("RSSI : ");
    Serial.println(WiFi.RSSI());

    Serial.println("==========================");
  }
}



/* ============================================================
   COMMAND PROCESSOR
============================================================ */

void processCommand(char *cmd) {
  if (strcmp(cmd, "VITA?") == 0) {
    sendExport(buildExportFrame());
    return;
  }
  if (strncmp(cmd, "SAVE~", 5) == 0) {
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
  if (strcmp(cmd, "COUNT") == 0) {
    char msg[32];
    sprintf(msg, "FILES:%d", fileCount());

    sendSocket(msg);
  }
  if (strncmp(cmd, "DELETE~", 7) == 0) {
    char *name = cmd + 7;
    fileDelete(name);
    return;
  }

  if (strcmp(cmd, "LIST") == 0) {
    listFiles();
    return;
  }

  if (strcmp(cmd, "DELALLFILE") == 0) {
    deleteAllFiles();
  }


  if (strncmp(cmd, "LOG~", 4) == 0) {
    logData(cmd + 4);
    return;
  }

  if (strcmp(cmd, "TIME?") == 0) {
    commandTime();
    return;
  }

  if (strncmp(cmd, "SETTIME", 7) == 0) {
    commandSetTime(cmd);
    return;
  }

  if (strcmp(cmd, "1") == 0) {
    sendEvent1();
    return;
  }
  if (strncmp(cmd, "SET SPEED", 9) == 0) {
    int value = atoi(cmd + 10);

    canData.simSpeedEnable = true;
    canData.simSpeed = value;

    Serial.print("SIM SPEED SET : ");
    Serial.println(value);
  }
  if (strcmp(cmd, "SET SPEED CAN") == 0) {
    canData.simSpeedEnable = false;

    Serial.println("BACK TO CAN SPEED");
  }
  if (strncmp(cmd, "SET GEAR", 8) == 0) {
    int value = atoi(cmd + 9);

    canData.gear = value;
    canData.validGear = true;

    Serial.print("GEAR SET : ");
    Serial.println(value);
  }
}

/* ============================================================
   Event Builder Task
============================================================ */
void eventTask(void *pv) {
  EventItem item;

  for (;;) {
    if (xQueueReceive(eventQueue, &item, portMAX_DELAY) == pdTRUE) {
      saveEventToSD(item);
      xQueueSend(sendQueue, &item, 0);
    }
  }
}

/* ============================================================
   CAN
============================================================ */
void canTask(void *pv) {
  for (;;) {
    if (!digitalRead(CAN_INT)) {
      if (xSemaphoreTake(spiMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        CAN0.readMsgBuf(&rxId, &len, rxBuf);
        xSemaphoreGive(spiMutex);

        uint32_t canId = rxId & 0x1FFFFFFF;

        memcpy(tpoBuf, rxBuf, 8);

        switch (canId) {

          case 0x1802F3EF:  // SPEED
            {
              canData.speed = rxBuf[0];
              canData.validSpeed = true;
              canData.lastUpdate = millis();
            }
            break;

          case 0x18F00517:  // GEAR
            {
              canData.gear = rxBuf[1];
              canData.validGear = true;
            }
            break;

          case 0x18F101D0:  // RPM
            {
              canData.rpmGenerator = (rxBuf[0] << 8) | rxBuf[1];
              canData.validRPM = true;
            }
            break;
        }
      }
    }

    vTaskDelay(1);
  }
}

//Fungsi Test by Command
void processTestCommand(char *cmd) {
  int value;

  // simulasi speed
  if (strncmp(cmd, "SET SPEED", 9) == 0) {
    int value = atoi(cmd + 10);

    canData.simSpeedEnable = true;
    canData.simSpeed = value;

    Serial.print("SIM SPEED SET : ");
    Serial.println(value);
  }

  // kembali ke CAN asli
  if (strcmp(cmd, "SET SPEED CAN") == 0) {
    canData.simSpeedEnable = false;

    Serial.println("BACK TO CAN SPEED");
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

  static bool wifiPrinted = false;

  for (;;) {

    if (WiFi.status() == WL_CONNECTED && !wifiPrinted) {
      debugWiFiStatus();
      debugWiFiConnected();

      wifiPrinted = true;
    }

    if (WiFi.status() != WL_CONNECTED)
      wifiPrinted = false;

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
  static bool clientConnected = false;

  for (;;) {
    ensureWiFiConnected();
    ensureServerConnection();

    /* =============================
       SERVER ACCEPT CLIENT
    ============================= */

    if (!incomingClient || !incomingClient.connected()) {
      WiFiClient newClient = server8998.available();

      if (newClient) {
        incomingClient = newClient;

        Serial.println("===== CLIENT CONNECTED =====");

        Serial.print("Client IP : ");
        Serial.println(incomingClient.remoteIP());

        Serial.print("Client Port : ");
        Serial.println(incomingClient.remotePort());

        Serial.println("============================");

        clientConnected = true;
      }
    }

    /* =============================
       CLIENT RECEIVE
    ============================= */

    if (incomingClient && incomingClient.available()) {
      int len = incomingClient.readBytesUntil('\n', buffer, sizeof(buffer) - 1);

      if (len > 0) {
        buffer[len] = 0;

        debugRx("CLIENT", buffer);

        processCommand(buffer);
      }
    }

    /* =============================
       CLIENT DISCONNECT
    ============================= */

    if (clientConnected && (!incomingClient || !incomingClient.connected())) {
      Serial.println("CLIENT DISCONNECTED");

      clientConnected = false;
    }

    /* =============================
       SERVER RESPONSE
    ============================= */

    if (serverClient.connected() && serverClient.available()) {
      int len = serverClient.readBytesUntil('\n', buffer, sizeof(buffer) - 1);

      if (len > 0) {
        buffer[len] = 0;

        debugRx("SERVER", buffer);
        // cek respon server
        if (strstr(buffer, "Sending#") != NULL) {
          Serial.println("OK");
          deleteOldestEventFile();
        }
      }
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}


//Function reconnect WiFi
void ensureWiFiConnected() {
  if (WiFi.status() == WL_CONNECTED) return;

  if (millis() - lastWifiReconnect < wifiReconnectInterval) return;

  Serial.println("WIFI CONNECTING...");

  WiFi.begin(deviceConfig.wifiSSID, deviceConfig.wifiPassword);

  lastWifiReconnect = millis();
}

void ensureServerConnection() {
  if (WiFi.status() != WL_CONNECTED) return;

  if (serverClient.connected()) return;

  if (millis() - lastReconnect8989 < reconnectInterval) return;

  uint16_t port = 8998;

  if (strlen(deviceConfig.destination) > 0)
    port = 8989;

  Serial.print("CONNECT SERVER ");
  Serial.print(deviceConfig.serverIP);
  Serial.print(":");
  Serial.println(port);

  serverClient.connect(deviceConfig.serverIP, port);

  lastReconnect8989 = millis();
}
void sendSocket(const char *msg) {
  if (incomingClient && incomingClient.connected()) {
    incomingClient.println(msg);
    debugTx("CLIENT", msg);
    return;
  }

  if (serverClient.connected()) {
    serverClient.println(msg);
    debugTx("SERVER", msg);
    return;
  }
}

void debugWiFiConnected() {
  Serial.println("===== WIFI CONNECTED =====");

  Serial.print("SSID : ");
  Serial.println(WiFi.SSID());

  Serial.print("IP   : ");
  Serial.println(WiFi.localIP());

  Serial.print("RSSI : ");
  Serial.println(WiFi.RSSI());

  Serial.println("==========================");
}
/* ============================================================
   DFPlayer TASK
============================================================ */
void taskAudio(void *pv) {

  uint8_t audioID;

  for (;;) {

    if (xQueueReceive(audioQueue, &audioID, portMAX_DELAY)) {

      do {

        dfPlayer.play(audioID);

        vTaskDelay(pdMS_TO_TICKS(3500));  // tunggu MP3 selesai

      } while (audioCondition && audioFile == audioID);
    }
  }
}

void requestAudio(uint8_t file, bool condition) {
  audioFile = file;
  audioCondition = condition;
}

uint32_t lastAudioTime = 0;

void playAudio(uint8_t id) {
  if (millis() - lastAudioTime < 2000) return;

  lastAudioTime = millis();

  xQueueSend(audioQueue, &id, 0);
}
/* ============================================================
   Task Event
============================================================ */
void taskCANLogic(void *pv) {
  for (;;) {
    CheckOverspeed();
    CheckCostingNetral();

    vTaskDelay(pdMS_TO_TICKS(100));
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
      } else if (index < 255)
        buffer[index++] = c;
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

/* ============================================================
   HARDWARE SERIAL TASK
============================================================ */
void taskExternalSerial(void *pv) {
  static char buffer[64];
  int index = 0;
  bool receiving = false;

  ExternalSerialFrame frame;

  for (;;) {
    while (externalSerial.available()) {
      char c = externalSerial.read();

      if (c == '*') {
        index = 0;
        receiving = true;
      }

      if (receiving) {
        if (index < sizeof(buffer) - 1)
          buffer[index++] = c;
      }

      if (c == '#') {
        buffer[index] = '\0';

        strcpy(frame.frame, buffer);

        xQueueSend(externalSerialQueue, &frame, 0);

        receiving = false;
        index = 0;
      }
    }

    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

void taskExternalParser(void *pv) {
  ExternalSerialFrame frame;

  for (;;) {
    if (xQueueReceive(externalSerialQueue, &frame, portMAX_DELAY)) {
      parseExternalFrame(frame.frame);
    }
  }
}

float AngelX,AngelY;

void parseExternalFrame(char *msg){
  char *start = strchr(msg, '*');
  char *end   = strchr(msg, '#');

  if (!start || !end) return;

  *end = '\0';
  start++;

  char *token;

  token = strtok(start, ",");

  // ===== RFID =====
  if (token)
  {
    if (strcmp(token, "-") != 0)   // hanya proses jika bukan "-"
    {
      updateIDCard(token);

      Serial.println("UART RFID RECEIVED");
      Serial.print("RFID : ");
      Serial.println(token);
    }
  }

  // ===== ANGLE X =====
  token = strtok(NULL, ",");
  if (token)
    AngelX = atof(token);

  // ===== ANGLE Y =====
  token = strtok(NULL, ",");
  if (token)
    AngelY = atof(token);
}
void updateIDCard(const char *newID){
  // abaikan jika "-"
  if (strcmp(newID, "-") == 0)
    return;

  // hanya update jika berbeda
  if (strcmp(deviceConfig.IDCardNow, newID) != 0)
  {
    // geser ID lama
    strncpy(deviceConfig.IDCardBefor,
            deviceConfig.IDCardNow,
            sizeof(deviceConfig.IDCardBefor) - 1);

    deviceConfig.IDCardBefor[sizeof(deviceConfig.IDCardBefor) - 1] = '\0';

    // simpan ID baru
    strncpy(deviceConfig.IDCardNow,
            newID,
            sizeof(deviceConfig.IDCardNow) - 1);

    deviceConfig.IDCardNow[sizeof(deviceConfig.IDCardNow) - 1] = '\0';

    Serial.println("RFID UPDATED");

    Serial.print("IDCardNow   : ");
    Serial.println(deviceConfig.IDCardNow);

    Serial.print("IDCardBefor : ");
    Serial.println(deviceConfig.IDCardBefor);

    saveIDCardPreference();
  }
}
void saveIDCardPreference() {
  configStorage.begin("devicecfg", false);

  configStorage.putString("IDNow", deviceConfig.IDCardNow);
  configStorage.putString("IDPrev", deviceConfig.IDCardBefor);

  configStorage.end();

  Serial.println("ID CARD SAVED");
}
/* ============================================================
   SETUP
============================================================ */

void setup() {

  Serial.begin(115200);
  externalSerial.begin(115200, SERIAL_8N1, EXTERNAL_SERIAL_RX_PIN, EXTERNAL_SERIAL_TX_PIN);  //UART to NANO
  loadConfig();                                                                              //Load Config
  loadHistoryVAD();                                                                          //Load VAD

  spiMutex = xSemaphoreCreateMutex();

  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);

  if (!SD.begin(SD_CS))
    Serial.println("SD INIT FAIL");
  else
    Serial.println("SD INIT OK");

  initRTC();
  startCan();
  initDFPlayer();


  logQueue = xQueueCreate(LOG_QUEUE_SIZE, sizeof(LogItem));
  eventQueue = xQueueCreate(20, sizeof(EventItem));
  sendQueue = xQueueCreate(20, sizeof(EventItem));
  audioQueue = xQueueCreate(10, sizeof(uint8_t));
  externalSerialQueue = xQueueCreate(10, sizeof(ExternalSerialFrame));

  xTaskCreatePinnedToCore(eventTask, "eventTask", 4096, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(sendTask, "sendTask", 4096, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(wifiTask, "wifiTask", 4096, NULL, 1, &wifiTaskHandle, 0);
  xTaskCreatePinnedToCore(socketTask, "socketTask", 8192, NULL, 1, &socketTaskHandle, 1);
  xTaskCreatePinnedToCore(serialTask, "serialTask", 4096, NULL, 1, &serialTaskHandle, 1);
  xTaskCreatePinnedToCore(sdTask, "sdTask", 4096, NULL, 1, &sdTaskHandle, 1);
  xTaskCreatePinnedToCore(canTask, "canTask", 4096, NULL, 2, &canTaskHandle, 1);
  xTaskCreatePinnedToCore(taskAudio, "AudioTask", 4096, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(taskCANLogic, "CANLogic", 4096, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(taskExternalSerial, "UART_RX", 4096, NULL, 3, NULL, 1);
  xTaskCreatePinnedToCore(taskExternalParser, "UART_PARSE", 4096, NULL, 2, NULL, 1);

  Serial.println("VITA CONTROLLER STARTED");
}

void loop() {}
