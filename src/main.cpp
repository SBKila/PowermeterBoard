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
#define PMNAME "Dev-PowerMeters2"
#define PMBNAME "Dev-PowermetersBoard2"
#endif

int m_NbBootPersistanceIndex = 0;
int m_debug_EEPROM_ALLOCATED = 0;

// FS m_fileSystem = LittleFS;
fs::FS *m_fileSystem = &LittleFS;
AsyncWebServer m_WebServer(80);
boolean webserverstarted = false;
#ifdef ENABLE_DEBUG_WEB
AsyncWebSocket m_ws("/ws");
AsyncWebSocket *m_pws = &ws;
#else
AsyncWebSocket *m_pws = NULL;
#endif

unsigned long lastBlinkTime = 0;
unsigned long blinkdelay = 0;
boolean blinkOn = false;
boolean m_debug_FS_STARTED = false;
size_t m_debug_FS_USED = 0;
size_t m_debug_FS_TOTAL = 0;

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
const int FLASH_DURATION = 150;    // Standard flash (ms)
const int HEARTBEAT_DURATION = 10; // Very short flash for "Connected" state (ms)
const int PAUSE_DURATION = 2000;   // Gap between error sequences (ms)
const long CONNECTED_GAP = 30000;  // 30 seconds gap when connected (ms)

/**
 * Nombre de flashs,État,Priorité
 * Flash 10ms / 30s,Tout est OK (WiFi + MQTT),-
 * 1 Flash,"WiFi OK, mais MQTT déconnecté",Basse
 * 2 Flashs,Mode Access Point (Config mode),Moyenne
 * 3 Flashs,WiFi STA déconnecté (Recherche...),Haute
 * 4 Flashs,Mode inconnu ou mixte,Haute
 * 5 Flashs,État ZOMBIE (Ping Gateway échoué),Critique
 */
void blinkLoop()
{
  static int pulsesToEmit = 0;
  static int currentPulse = 0;
  unsigned long now = millis();

  if (now - lastBlinkTime < blinkdelay)
    return;
  lastBlinkTime = now;

  // 1. Determine pulses based on WiFi state
  if (currentPulse == 0)
  {
    if (WIFIMANAGER.isNetworkZombie())
    {
      pulsesToEmit = 5; // 5 blinks = Zombie state detected (network reachable but no data)
    }
    else if (!WiFi.isConnected())
    {
      // if not connected, handle special case when WiFi is off at boot
      int mode = WiFi.getMode();
      if (mode == WIFI_OFF)
      {
        // If we have stored settings, assume device will try STA soon
        // Use public API stringProcessor to check for saved SSID instead of calling protected method
        if (WIFIMANAGER.stringProcessor(String("SSIDNAME")).length() > 0)
          pulsesToEmit = 3; // Trying to connect as STA
        else
          pulsesToEmit = 2; // No settings -> AP mode expected
      }
      else if ((mode & WIFI_AP) == WIFI_AP)
      {
        pulsesToEmit = 2; // Access Point
      }
      else if ((mode & WIFI_STA) == WIFI_STA)
      {
        pulsesToEmit = 3; // Trying to connect as STA
      }
      else
      {
        pulsesToEmit = 4; // Unknown/mixed
      }
    }
    else if (!POWERMETERBOARD.isMqttConnected())
    {
      pulsesToEmit = 1; // 1 blink = WiFi OK, MQTT not connected
    }
    else
    {
      pulsesToEmit = -1; // Heartbeat 30s
    }
  }

  // 2. State Machine logic
  if (pulsesToEmit == -1)
  {
    // HEARTBEAT MODE: Very brief flash every 30s
    blinkOn = !blinkOn;
    digitalWrite(LED, blinkOn ? LOW : HIGH); // LOW is usually ON for ESP8266

    if (blinkOn)
    {
      blinkdelay = HEARTBEAT_DURATION; // Brief "on"
    }
    else
    {
      blinkdelay = CONNECTED_GAP; // Long "off"
      currentPulse = 0;           // Reset to re-check status next time
    }
  }
  else if (currentPulse < (pulsesToEmit * 2))
  {
    // ERROR/STATUS MODE: Pulse sequence
    blinkOn = !blinkOn;
    digitalWrite(LED, blinkOn ? LOW : HIGH);
    blinkdelay = FLASH_DURATION;
    currentPulse++;
  }
  else
  {
    // Gap between sequences
    digitalWrite(LED, HIGH); // LED OFF
    blinkdelay = PAUSE_DURATION;
    currentPulse = 0;
  }
}

String stringProcessor(const String &var)
{
  MAIN_DEBUG_MSG(F("stringProcessor %s\n"), var.c_str());
  if (var == "RELEASE")
  {
    return String(BUILD_VERSION_STRING);
  }
  else if (var == "EEPROM")
  {
    return String(m_debug_EEPROM_ALLOCATED);
  }
  else if (var == "FS_STARTED")
  {
    return String(m_debug_FS_STARTED ? "YES" : "NO");
  }
  else if (var == "FS_USED")
  {
    return String(m_debug_FS_USED);
  }
  else if (var == "FS_TOTAL")
  {
    return String(m_debug_FS_TOTAL);
  }
  return String();
}

WiFiEventHandler gotIpEventHandler, disconnectedEventHandler;
void setup()
{

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

  m_debug_EEPROM_ALLOCATED = EEPROMEX.getAllocatedSize();

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
      WiFi.onStationModeGotIP([](const WiFiEventStationModeGotIP &event)
                              {
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
        } });

  disconnectedEventHandler = WiFi.onStationModeDisconnected(
      [](const WiFiEventStationModeDisconnected &event)
      {
        MAIN_DEBUG_MSG(F("Station disconnected\n"));
      });

#ifdef ENABLE_DEBUG_WEB
  m_WebServer.addHandler(m_pws);

  m_WebServer.serveStatic("/debug/index.css", LittleFS, "/debug/index.css");
  m_WebServer.serveStatic("/debug/index.js", LittleFS, "/debug/index.js");
  m_WebServer.on("/debug/index.html",
                 HTTP_GET,
                 [&](AsyncWebServerRequest *request)
                 {
                   AsyncWebServerResponse *response =
                       request->beginResponse(*m_fileSystem, "/debug/index.html", "text/html",
                                              false, [&](const String &var) -> String
                                              {
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
                                
                                return ""; });
                   // Disable caching via HTTP headers
                   response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
                   response->addHeader("Pragma", "no-cache");
                   response->addHeader("Expires", "0");
                   request->send(response);
                 });
  m_WebServer.on("/debug", HTTP_GET, [](AsyncWebServerRequest *request)
                 { request->redirect("/debug/index.html"); });
  m_WebServer.serveStatic(
                 "/debug/reboot.log", LittleFS,
                 "/reboot_history.log")
      .setCacheControl("no-cache, no-store, must-revalidate");
#endif

  m_WebServer.serveStatic(
      "/favicon.ico", LittleFS,
      "/favicon.ico"); //.setCacheControl("public, max-age=604800"); // Cache
                       // for 1 week
  m_WebServer.onNotFound([](AsyncWebServerRequest *request)
                         {
    MAIN_DEBUG_MSG(F("404 Not Found: %s\n"), request->url().c_str());
    request->send(404, "text/plain", "Not found"); });

  blinkSetup();

  WIFIMANAGER.setup(PMNAME, &m_WebServer, m_fileSystem);
#ifdef ESP8266
  WiFi.setSleepMode(WIFI_NONE_SLEEP); // Disable WiFi sleep to prevent wDev_ProcessFiq crashes
#endif
  POWERMETERBOARD.setup(PMNAME, &m_WebServer, *m_fileSystem);
  REBOOTTRACKER.setup();

#ifdef ESP32
  boolean fsStarted = LittleFS.begin(true);
#else
  boolean fsStarted = LittleFS.begin();
#endif
  if (!fsStarted)
  {
    MAIN_DEBUG_MSG(F("An error has occurred while mounting LittleFS\n"));
    m_debug_FS_STARTED = false;
  }
  else
  {
    MAIN_DEBUG_MSG(F("LittleFS mounted successfully\n"));
    m_debug_FS_STARTED = true;
    FSInfo fs_info;
    LittleFS.info(fs_info);
    MAIN_DEBUG_MSG(F("LittleFS OK. Used: %u / Total: %u\n"), fs_info.usedBytes,
                   fs_info.totalBytes);
    m_debug_FS_USED = fs_info.usedBytes;
    m_debug_FS_TOTAL = fs_info.totalBytes;

    // Attempt to read a marker file
    if (!LittleFS.exists("pmb\\index.js"))
    {
      MAIN_DEBUG_MSG(F("CRITICAL: Mount OK but files missing! (Empty FS)\n"));
    }
    REBOOTTRACKER.saveToFS(*m_fileSystem);
  }
  m_WebServer.begin();
  webserverstarted = true;

#ifdef ENABLE_DEBUG_WEB
  m_ws.enable(true);
#endif
  // put your main code here, to run repeatedly:
  MAIN_DEBUG_MSG(F("Setup ending\n"));
}
unsigned long lastCommit = 0;
void loop()
{
  blinkLoop();
  POWERMETERBOARD.loop(m_pws);
  WIFIMANAGER.loop();
  MEMORYDEBUGGER.loop(m_pws);
  MDNS.update();
  if (millis() - lastCommit > 60000)
  {
    lastCommit = millis();
    EEPROMEX.commit();
  }
}
