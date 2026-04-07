#include <Arduino.h>
#include <LittleFS.h>
#include <debug.h>

#ifdef ESP32
#include <AsyncTCP.h>
#include <WiFi.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#elif defined(TARGET_RP2040)
#include <WebServer.h>
#include <WiFi.h>
#endif
#include <ArduinoOTA.h>
#include <ESP8266mDNS.h>
#include <ESPAsyncWebServer.h>

#include "./MemoryDebugger.hpp"
#include "./PowermetersBoard.hpp"
#include "./WifiManager.hpp"
#include "./reboottracker.hpp"
#include "EEPROMEX.h"
#include "myDebugLogger.h"

RebootTrackerClass REBOOTTRACKER;

#ifdef DEBUG_MAIN
#define MAIN_DEBUG_MSG(...) DEBUG_MSG("MAIN", __VA_ARGS__)
#else
#define MAIN_DEBUG_MSG(...)
#endif

#ifndef ON_DEV
#define PMNAME "PowerMeters"
#define PMBNAME "PowermetersBoard"
#else
#define PMNAME "Dev-PowerMeters"
#define PMBNAME "Dev-PowermetersBoard"
#endif

int m_NbBootPersistanceIndex = 0;
#ifdef ENABLE_DEBUG_WEB
int m_debug_EEPROM_ALLOCATED = 0;
#endif

// FS m_fileSystem = LittleFS;
fs::FS *m_fileSystem = &LittleFS;
AsyncWebServer m_WebServer(80);
boolean webserverstarted = false;
bool g_ota_in_progress = false;
#ifdef ENABLE_DEBUG_WEB
AsyncWebSocket m_ws("/ws");
AsyncWebSocket *m_pws = &m_ws;
#else
AsyncWebSocket *m_pws = NULL;
#endif

unsigned long lastBlinkTime = 0;
unsigned long blinkdelay = 0;
boolean blinkOn = false;
#ifdef ENABLE_DEBUG_WEB
boolean m_debug_FS_STARTED = false;
size_t m_debug_FS_USED = 0;
size_t m_debug_FS_TOTAL = 0;
#endif

#define LED D4
#define FOURTHBYSECOND 225
#define TWICEBYSECOND 475
#define ONEBYSECOND 975
#define ONEBY2SECOND 1150
#define ONEBY10SECONDS 9975
#define FOURTHBYMINUTE 29975
#define TWICEBYMINUTE 29975
void blinkSetup() { pinMode(LED, OUTPUT); }
// Settings
const int FLASH_DURATION = 150; // Standard flash (ms)
const int HEARTBEAT_DURATION =
    10;                           // Very short flash for "Connected" state (ms)
const int PAUSE_DURATION = 2000;  // Gap between error sequences (ms)
const long CONNECTED_GAP = 30000; // 30 seconds gap when connected (ms)

int g_currentBlinkerState = 0;
String getBlinkerStateString(int state) {
  switch (state) {
  case -1:
    return F("Connected");
  case 0:
    return F("Initializing...");
  case 1:
    return F("WiFi OK, MQTT disconnected");
  case 2:
    return F("Access Point Mode");
  case 3:
    return F("WiFi STA disconnected");
  case 4:
    return F("Unknown/mixed mode");
  case 5:
    return F("Zombie state (No gateway ping)");
  default:
    return F("Unknown State");
  }
}

/**
 * Number of flashes, State, Priority
 * 10ms flash / 30s, Everything OK (WiFi + MQTT), -
 * 1 Flash, "WiFi OK, but MQTT disconnected", Low
 * 2 Flashes, Access Point Mode (Config mode), Medium
 * 3 Flashes, WiFi STA disconnected (Searching...), High
 * 4 Flashes, Unknown or mixed mode, High
 * 5 Flashes, ZOMBIE state (Ping Gateway failed), Critical
 */
void blinkLoop() {
  static int pulsesToEmit = 0;
  static int currentPulse = 0;
  unsigned long now = millis();

  if (now - lastBlinkTime < blinkdelay)
    return;
  lastBlinkTime = now;

  // 1. Determine pulses based on WiFi state
  if (currentPulse == 0) {
    if (WIFIMANAGER.isNetworkZombie()) {
      pulsesToEmit =
          5; // 5 blinks = Zombie state detected (network reachable but no data)
    } else if (!WiFi.isConnected()) {
      // if not connected, handle special case when WiFi is off at boot
      int mode = WiFi.getMode();
      if (mode == WIFI_OFF) {
        // If we have stored settings, assume device will try STA soon
        // Use public API stringProcessor to check for saved SSID instead of
        // calling protected method
        if (WIFIMANAGER.stringProcessor(String("SSIDNAME")).length() > 0)
          pulsesToEmit = 3; // Trying to connect as STA
        else
          pulsesToEmit = 2; // No settings -> AP mode expected
      } else if ((mode & WIFI_AP) == WIFI_AP) {
        pulsesToEmit = 2; // Access Point
      } else if ((mode & WIFI_STA) == WIFI_STA) {
        pulsesToEmit = 3; // Trying to connect as STA
      } else {
        pulsesToEmit = 4; // Unknown/mixed
      }
    } else if (!POWERMETERBOARD.isMqttConnected()) {
      pulsesToEmit = 1; // 1 blink = WiFi OK, MQTT not connected
    } else {
      pulsesToEmit = -1; // Heartbeat 30s
    }

    // Broadcast the new state via WebSocket if a change is detected
    if (pulsesToEmit != g_currentBlinkerState) {
      g_currentBlinkerState = pulsesToEmit;
      if (m_pws && m_pws->count() > 0) {
        char jsonBuffer[128];
        snprintf_P(jsonBuffer, sizeof(jsonBuffer),
                   PSTR("{\"type\":\"blinker\",\"datas\":%d}"),
                   g_currentBlinkerState);
        m_pws->textAll(jsonBuffer);
      }
    }
  }

  // 2. State Machine logic
  if (pulsesToEmit == -1) {
    // HEARTBEAT MODE: Very brief flash every 30s
    blinkOn = !blinkOn;
    digitalWrite(LED, blinkOn ? LOW : HIGH); // LOW is usually ON for ESP8266

    if (blinkOn) {
      blinkdelay = HEARTBEAT_DURATION; // Brief "on"
    } else {
      blinkdelay = CONNECTED_GAP; // Long "off"
      currentPulse = 0;           // Reset to re-check status next time
    }
  } else if (currentPulse < (pulsesToEmit * 2)) {
    // ERROR/STATUS MODE: Pulse sequence
    blinkOn = !blinkOn;
    digitalWrite(LED, blinkOn ? LOW : HIGH);
    blinkdelay = FLASH_DURATION;
    currentPulse++;
  } else {
    // Gap between sequences
    digitalWrite(LED, HIGH); // LED OFF
    blinkdelay = PAUSE_DURATION;
    currentPulse = 0;
  }
}

#ifdef ENABLE_DEBUG_WEB
String stringProcessor(const String &var) {
  // MAIN_DEBUG_MSG(F("stringProcessor %s\n"), var.c_str());
  if (var == "RELEASE") {
    return String(BUILD_VERSION_STRING);
  } else if (var == "EEPROM") {
    return String(m_debug_EEPROM_ALLOCATED);
  } else if (var == "FS_STARTED") {
    return String(m_debug_FS_STARTED ? "YES" : "NO");
  } else if (var == "FS_USED") {
    return String(m_debug_FS_USED);
  } else if (var == "FS_TOTAL") {
    return String(m_debug_FS_TOTAL);
  } else if (var == "BLINKER_STATE") {
    return getBlinkerStateString(g_currentBlinkerState);
  }
  return String();
}
#endif

WiFiEventHandler gotIpEventHandler, disconnectedEventHandler;
void setup() {

  DEBUG_INIT();
  delay(200);
  MAIN_DEBUG_MSG(F("Setup\n"));
  MEMORYDEBUGGER.begin();
  size_t rtcoffset = 0;

  rtcoffset += POWERMETERBOARD.setupRTCPersistance(rtcoffset);

  /**************************/
  /* Persistence Management */
  /*    Memory allocation   */
  /**************************/
  WIFIMANAGER.setupPersistance();
  POWERMETERBOARD.setupPersistance();
  REBOOTTRACKER.setupPersistance();

#ifdef ENABLE_DEBUG_WEB
  m_debug_EEPROM_ALLOCATED = EEPROMEX.getAllocatedSize();
#endif

  /**************************/
  /* Persistence Management */
  /*       Activation       */
  /**************************/
  EEPROMEX.begin();
  /**************************/
  /**
   * Number of blinks, State, Priority
   * 10ms flash / 30s, Everything OK (WiFi + MQTT), -
   * 1 Blink, WiFi OK but MQTT disconnected, Low
   * 2 Blinks, Access Point Mode (Config mode), Medium
   * 3 Blinks, WiFi STA disconnected (Searching...), High
   * 4 Blinks, Unknown or mixed mode, High
   * 5 Blinks, ZOMBIE state (Gateway ping failed), Critical
   */
  /**************************/
  gotIpEventHandler =
      WiFi.onStationModeGotIP([](const WiFiEventStationModeGotIP &event) {
        MAIN_DEBUG_MSG(F("Station connected, IP: %s\n"),
                       WiFi.localIP().toString().c_str());
        /*******************/
        /*  Multicast DNS  */
        /*   Activation    */
        /*******************/
        MAIN_DEBUG_MSG(F("Starting mDNS %s\n"), PMBNAME);
        if (!MDNS.begin(PMBNAME)) {
          MAIN_DEBUG_MSG(F("Fails to start mDNS\n"));
        } else {
          // Add service to MDNS-SD
          MDNS.addService("http", "tcp", 80);
        }

        // Start OTA after WiFi is connected to ensure UDP socket is bound
        // correctly
        MAIN_DEBUG_MSG(F("Starting ArduinoOTA\n"));
        ArduinoOTA.begin();
      });

  disconnectedEventHandler = WiFi.onStationModeDisconnected(
      [](const WiFiEventStationModeDisconnected &event) {
        MAIN_DEBUG_MSG(F("Station disconnected\n"));
      });

#ifdef ENABLE_DEBUG_WEB
  m_WebServer.addHandler(m_pws);
  m_WebServer.serveStatic("/debug/index.css", LittleFS, "/debug/index.css");
  m_WebServer.serveStatic("/debug/index.js", LittleFS, "/debug/index.js");
  m_WebServer.on("/debug/index.html", HTTP_GET,
                 [&](AsyncWebServerRequest *request) {
                   AsyncWebServerResponse *response = request->beginResponse(
                       *m_fileSystem, "/debug/index.html", "text/html", false,
                       [&](const String &var) -> String {
                         String value = stringProcessor(var);
                         if (value.length() > 0)
                           return value;

                         value = POWERMETERBOARD.stringProcessor(var);
                         if (value.length() > 0)
                           return value;

                         value = WIFIMANAGER.stringProcessor(var);
                         if (value.length() > 0)
                           return value;

                         value = REBOOTTRACKER.stringProcessor(var);
                         if (value.length() > 0)
                           return value;

                         value = MEMORYDEBUGGER.stringProcessor(var);
                         if (value.length() > 0)
                           return value;

                         return "";
                       });
                   // Disable caching via HTTP headers
                   response->addHeader("Cache-Control",
                                       "no-cache, no-store, must-revalidate");
                   response->addHeader("Pragma", "no-cache");
                   response->addHeader("Expires", "0");
                   request->send(response);
                 });
  m_WebServer.serveStatic("/debug/reboot.log", LittleFS, "/reboot_history.log")
      .setCacheControl("no-cache, no-store, must-revalidate");
  m_WebServer.on("/debug", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->redirect("/debug/index.html");
  });

#endif

  m_WebServer.serveStatic(
      "/favicon.ico", LittleFS,
      "/favicon.ico"); //.setCacheControl("public, max-age=604800"); // Cache
                       // for 1 week
  m_WebServer.onNotFound([](AsyncWebServerRequest *request) {
    MAIN_DEBUG_MSG(F("404 Not Found: %s\n"), request->url().c_str());
    request->send(404, "text/plain", "Not found");
  });

  blinkSetup();

  WIFIMANAGER.setup(PMNAME, &m_WebServer, m_fileSystem);
#ifdef ESP8266
  WiFi.setSleepMode(
      WIFI_NONE_SLEEP); // Disable WiFi sleep to prevent wDev_ProcessFiq crashes
#endif
  POWERMETERBOARD.setup(PMNAME, &m_WebServer, *m_fileSystem);
  REBOOTTRACKER.setup();

  // --- OTA Configuration (Over-The-Air Update) ---
  ArduinoOTA.setHostname(PMBNAME);
  ArduinoOTA.onStart([]() {
    g_ota_in_progress = true;
    String type;

    MAIN_DEBUG_MSG("=== OTA START PHASE 1: STOPPING SERVICES ===\n");
    // 1. SUSPENSION: Stop web server and websockets immediately
    if (m_pws)
      m_pws->enable(false);
    m_WebServer.end();
    webserverstarted = false;

    MAIN_DEBUG_MSG("=== OTA START PHASE 2: BACKUP & SUSPEND ===\n");
    POWERMETERBOARD.suspend(true);
    POWERMETERBOARD.backup(); 
    EEPROMEX.commit();        

    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_FS
      type = "filesystem";
    }
    MAIN_DEBUG_MSG("=== OTA START PHASE 3: UPDATE PROCESS BEGINNING (%s) ===\n", type.c_str());

    blinkdelay = 50;
  });

  ArduinoOTA.onEnd([]() {
    MAIN_DEBUG_MSG("\n=== OTA END SUCCESS! Board will reboot via Core. ===\n");
    // We intentionally don't do anything else to let the MCU commit and reboot gracefully.
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    if (progress == 0) MAIN_DEBUG_MSG("\n[OTA Progress]: Start...");
    else if (progress == total) MAIN_DEBUG_MSG("\n[OTA Progress]: 100%% (Finalizing)");
    else if (progress % (total/10) == 0) MAIN_DEBUG_MSG(".");
    
    digitalWrite(LED, !digitalRead(LED));
  });

  ArduinoOTA.onError([](ota_error_t error) {
    g_ota_in_progress = false;
    MAIN_DEBUG_MSG("\n=== OTA ERROR TRIGGERED [%u] ===\n", error);
    
    String errormessage;
    if (error == OTA_AUTH_ERROR)
      errormessage = "Auth Failed";
    else if (error == OTA_BEGIN_ERROR)
      errormessage = "Begin Failed (No Space / Bad alignment)";
    else if (error == OTA_CONNECT_ERROR)
      errormessage = "Connect Failed";
    else if (error == OTA_RECEIVE_ERROR)
      errormessage = "Receive Failed (Network drop / Timeout)";
    else if (error == OTA_END_ERROR)
      errormessage = "End Failed (MD5 check failed or Write failed)";
    else
      errormessage = "Unknown Error";

    MAIN_DEBUG_MSG("OTA Reason: %s\n", errormessage.c_str());
    MAIN_DEBUG_MSG("=== REBOOTING SYSTEM TO RECOVER ===\n");
    delay(500);
    ESP.restart();
  });

  // ArduinoOTA.begin() moved to gotIpEventHandler
  // // --------------------------------------------------

#ifdef ESP32
  boolean fsStarted = LittleFS.begin(true);
#else
  boolean fsStarted = LittleFS.begin();
#endif
  if (!fsStarted) {
    MAIN_DEBUG_MSG(F("An error has occurred while mounting LittleFS\n"));
#ifdef ENABLE_DEBUG_WEB
    m_debug_FS_STARTED = false;
#endif
  } else {
    MAIN_DEBUG_MSG(F("LittleFS mounted successfully\n"));
    FSInfo fs_info;
    LittleFS.info(fs_info);
    MAIN_DEBUG_MSG(F("LittleFS OK. Used: %u / Total: %u\n"), fs_info.usedBytes,
                   fs_info.totalBytes);
#ifdef ENABLE_DEBUG_WEB
    m_debug_FS_STARTED = true;
    m_debug_FS_USED = fs_info.usedBytes;
    m_debug_FS_TOTAL = fs_info.totalBytes;
#endif

    // Attempt to read a marker file
    if (!LittleFS.exists("pmb\\index.js")) {
      MAIN_DEBUG_MSG(F("CRITICAL: Mount OK but files missing! (Empty FS)\n"));
    }
#ifdef ENABLE_DEBUG_WEB
    REBOOTTRACKER.saveToFS(*m_fileSystem);
#endif
  }
  m_WebServer.begin();
  webserverstarted = true;

#ifdef ENABLE_DEBUG_WEB
  m_ws.enable(true);
#endif
  // Main code setup is complete
  MAIN_DEBUG_MSG(F("Setup ending\n"));
}
unsigned long lastCommit = 0;
void loop() {
  ArduinoOTA.handle();
  if (g_ota_in_progress) {
    blinkLoop(); // Let LED pulse for OTA feedback
    return;      // Skip everything else to prevent EEPROM commit & I/O
  }
  blinkLoop();
  POWERMETERBOARD.loop(m_pws);
  WIFIMANAGER.loop();
  MEMORYDEBUGGER.loop(m_pws);
  MDNS.update();
  if (millis() - lastCommit > 60000) {
    lastCommit = millis();
    EEPROMEX.commit();
  }
}
