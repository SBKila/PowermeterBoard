#ifndef WIFIMGR_H
#define WIFIMGR_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <AsyncJson.h>
#include <EEPROMEX.h>
#include <ESP8266WiFi.h>
#include <ESP8266WiFiGeneric.h>
#include <ESPAsyncWebServer.h>
#include <AsyncPing.h>

#pragma once

extern const char string_WIFI_OFF[];
extern const char string_WIFI_STA[];
extern const char string_WIFI_AP[];
extern const char string_WIFI_AP_STA[];
extern const char *const strings_WiFiMode[];

#ifdef DEBUG_WIFIMGR
#define WIFIMGR_DEBUG_MSG(...) DEBUG_MSG("WIFIMGR", __VA_ARGS__)
#else
#define WIFIMGR_DEBUG_MSG(...)
#endif

#ifdef EEPROMMAGIC
#define MAGICWIFIMGR EEPROMMAGIC
#else
#define MAGICWIFIMGR 93
#endif

extern AsyncWebSocket *p_wifimanagerWebSocket;
extern WiFiEventHandler onWiFiModeChangeHandler;
extern WiFiEventHandler onStationModeGotIPHandler;

struct WIFIsettings
{
  int tag;
  char ssid_name[32];
  char ssid_key[64];
};

class WIFIManagerClass
{
public:
#ifdef ARDUINOJSON_6_COMPATIBILITY
  WIFIManagerClass(const char *p_pName);
#else
  WIFIManagerClass();
#endif
  ~WIFIManagerClass();
  int setupPersistance();
  void setup(const char *p_pName, AsyncWebServer *p_pWiFiServer, fs::FS *p_pfs);
  void loop();
  void _handlemodeSwitch();
  String stringProcessor(const String &var);
  bool isNetworkZombie();

protected:
  boolean m_setupWebHandlerAccessPointDone = false;

  void _unsetupWebHandlerAccessPoint(AsyncWebServer *p_pWiFiServer);
  void _setupWebHandlerAccessPoint(AsyncWebServer *p_pWiFiServer, fs::FS *p_pfs);
  void _setupRadioAsAccessPoint();
  void _setupRadioAsStation(AsyncWebServer *p_pWiFiServer, fs::FS *p_pfs);
  enum scanningState
  {
    SCANNING_IDLE,
    SCANNING_DELAYTOSTART,
    SCANNING_TOSTART,
    SCANNING_WAITING_RESULTS
  };
  size_t m_startDelayToStart = 0;
  scanningState m_scanningState = SCANNING_IDLE;
  enum switchingAction
  {
    SWITCHING,
    SWITCHED,
    NONE,
    SWITCHTOAP,
    SWITCHTOSTA,
    SWITCHTOAPSTA,
    REBOOT,
    SWITCHOFFANDREBOOT,

  };
  unsigned long m_switchingtime = 0;
  switchingAction m_switching = NONE;
  void switchTo(switchingAction action);
  long startReboot = 0;
  void storeSettings();
  boolean isSettingExist();
  void pushNotif(JsonVariantConst message);

  int m_SettingsPersistanceIndex = 0;
  boolean isSettingsDirty = false;
  WIFIsettings m_SettingsData;
#ifdef ARDUINOJSON_6_COMPATIBILITY
  DynamicJsonDocument m_jsonDoc;
#else
  JsonDocument m_jsonDoc;
#endif
  unsigned long m_connectingStartTime = 0;
  unsigned long m_apModeStartTime = 0;    // Track when AP mode was entered
  unsigned long m_connectionLostTime = 0; // Track when connection was lost
  unsigned long m_lastRetryTime = 0;      // Track last WiFi.begin attempt
  AsyncWebServer *m_pWiFiServer = NULL;

  fs::FS *m_pfs;

  // Non-blocking AP setup state machine
  enum WifiSetupState
  {
    WIFI_SETUP_IDLE,
    WIFI_SETUP_DISCONNECTING,
    WIFI_SETUP_WAITING_DISCONNECT,
    WIFI_SETUP_WEBHANDLER,
    WIFI_SETUP_RADIO,
    WIFI_SETUP_WAITING_RADIO,
    WIFI_SETUP_WAITING_CONNECTIVITY,
    WIFI_SETUP_DONE
  };
  WifiSetupState m_WifiSetupState = WIFI_SETUP_IDLE;
  unsigned long m_WifiSetupTimeout = 0;
  WiFiMode_t m_targetWiFiMode = WIFI_AP_STA;
  void _handleWifiSetup(AsyncWebServer *p_pWiFiServer, fs::FS *p_pfs);
  void _handleZombieDetection();

private:
  boolean m_isSoftAPDone = false;
  char *m_pName = NULL;
  int m_failedPingCount = 0;
  unsigned long m_lastPingTime = 0;
  AsyncPing m_Ping;
  bool m_isNetworkZombie = false;
  AsyncWebHandler *m_pWifiScanHandler = NULL;
  AsyncWebHandler *m_pWifiPostHandler = NULL;
  AsyncWebHandler *m_pWifiStaticHandler = NULL;
  const char *_switchingActionToString(switchingAction action);
  // char *strdup_P(PGM_P src);
};

extern WIFIManagerClass WIFIMANAGER;

#endif