#include "WifiManager.hpp"

const char string_WIFI_OFF[] PROGMEM = "WIFI_OFF";
const char string_WIFI_STA[] PROGMEM = "WIFI_STA";
const char string_WIFI_AP[] PROGMEM = "WIFI_AP";
const char string_WIFI_AP_STA[] PROGMEM = "WIFI_AP_STA";
const char *const strings_WiFiMode[] PROGMEM = {
    string_WIFI_OFF, string_WIFI_STA, string_WIFI_AP, string_WIFI_AP_STA};

AsyncWebSocket *p_wifimanagerWebSocket = NULL;
WiFiEventHandler onWiFiModeChangeHandler;
WiFiEventHandler onStationModeGotIPHandler;

String getWiFiModeString(WiFiMode_t mode)
{
  if (mode >= 0 && mode < 4)
  {
    return FPSTR(pgm_read_ptr(&strings_WiFiMode[mode]));
  }
  return F("UNKNOWN");
}

WIFIManagerClass WIFIMANAGER;

#ifdef ARDUINOJSON_6_COMPATIBILITY
WIFIManagerClass::WIFIManagerClass(const char *p_pName)
    : m_jsonDoc(1 + ((1 + 32 + 1 + 1) * 5) + 1)
{
  m_pName = strdup(p_pName);
  strupr(m_pName);
}
#else
WIFIManagerClass::WIFIManagerClass() {}
#endif

WIFIManagerClass::~WIFIManagerClass()
{
  if (NULL != m_pName)
  {
    free(m_pName);
  }
}

int WIFIManagerClass::setupPersistance()
{
  WIFIMGR_DEBUG_MSG(F("setupPersistance\n"));
  m_SettingsPersistanceIndex = EEPROMEX.allocate(sizeof(struct WIFIsettings));
  return sizeof(struct WIFIsettings);
}

void WIFIManagerClass::setup(const char *p_pName, AsyncWebServer *p_pWiFiServer,
                             fs::FS *p_pfs)
{
  WiFi.persistent(false);
  m_pfs = p_pfs;
  m_pWiFiServer = p_pWiFiServer;

  m_pName = strdup(p_pName);

  EEPROMEX.get(m_SettingsPersistanceIndex, m_SettingsData);
  isSettingsDirty = false;

  onWiFiModeChangeHandler =
      WiFi.onWiFiModeChange([this](const WiFiEventModeChange &event)
                            { 
                              WIFIMGR_DEBUG_MSG(
                                F("wifi mode change %s->%s expected %s\n"),
                                getWiFiModeString(event.oldMode).c_str(),
                                getWiFiModeString(event.newMode).c_str(),
                                getWiFiModeString(m_targetWiFiMode).c_str()); 

                              if(SWITCHING == this->m_switching && event.newMode == m_targetWiFiMode)
                              {
                                this->m_switching = SWITCHED;
                              } });

  onStationModeGotIPHandler =
      WiFi.onStationModeGotIP([this](const WiFiEventStationModeGotIP &event)
                              {
        WIFIMGR_DEBUG_MSG(F("Station connected, IP: %s\n"),
                          WiFi.localIP().toString().c_str());
        JsonDocument notifEvent;
        notifEvent["evt"] = 0;
        notifEvent["data"] = WiFi.localIP().toString();
        this->pushNotif(notifEvent); });

  m_Ping.on(true, [this](const AsyncPingResponse &response)
            {
              if (response.answer)
              {
                m_failedPingCount = 0;
                m_isNetworkZombie = false;
              }
              else
              {
                m_failedPingCount++;
                WIFIMGR_DEBUG_MSG(F("Ping failed. Count: %d/3\n"), m_failedPingCount);
                if (m_failedPingCount >= 3)
                  m_isNetworkZombie = true;
              }
              return true; });
}

void WIFIManagerClass::loop()
{
  unsigned long now = millis();

  if (m_scanningState != SCANNING_IDLE)
  {
    if (m_scanningState == SCANNING_DELAYTOSTART)
    {
      if (m_startDelayToStart == 0)
        m_startDelayToStart = now;
      if (now - m_startDelayToStart > 500)
      {
        WIFIMGR_DEBUG_MSG(F("Delay to start scanning elapsed. Starting scan...\n"));
        m_scanningState = SCANNING_TOSTART;
        m_startDelayToStart = 0;
      }
    }
    else if (m_scanningState == SCANNING_TOSTART)
    {
      WIFIMGR_DEBUG_MSG(F("start scanning available wifi\n"));
      WiFi.scanNetworks(true);
      m_scanningState = SCANNING_WAITING_RESULTS;
    }
    else if (m_scanningState == SCANNING_WAITING_RESULTS)
    {
      int found = WiFi.scanComplete();
      if (found > 0)
      {
        WIFIMGR_DEBUG_MSG(F("Scan complete: %d networks found\n"), found);

        JsonDocument notifEvent;
        notifEvent["evt"] = 1;
        JsonArray data = notifEvent["data"].to<JsonArray>();
        for (int i = 0; i < found; i++)
        {
          data.add(String(WiFi.SSID(i)) + " ch:" + WiFi.channel(i) + " (" +
                   WiFi.RSSI(i) + ")");
          WIFIMGR_DEBUG_MSG(F("%d: %s, Ch:%d (%ddBm) %s\n"), i + 1,
                            WiFi.SSID(i).c_str(), WiFi.channel(i), WiFi.RSSI(i),
                            WiFi.encryptionType(i) == ENC_TYPE_NONE ? "open"
                                                                    : "");
        }
        WiFi.scanDelete();
        WIFIMGR_DEBUG_MSG(F("end scanning available wifi\n"));
        pushNotif(notifEvent);
        m_scanningState = SCANNING_IDLE;
      }
    }
  }
  else if ((m_switching != NONE) && (m_switching != SWITCHING) && (m_switching != SWITCHED))
  {
    // Handle switching AP mode command (non-blocking)
    _handlemodeSwitch();
  }
  else if (m_WifiSetupState != WIFI_SETUP_IDLE)
  {
    // Handle AP setup state machine (non-blocking)
    _handleWifiSetup(m_pWiFiServer, m_pfs);
  }
  else
  {
    if (isSettingExist())
    {
      if ((WiFi.getMode() & WIFI_STA) == 0)
      {
        WIFIMGR_DEBUG_MSG(F("WiFi is off. Forcing STA mode.\n"));
        switchTo(SWITCHTOSTA);
      }
      else
      {

        // --- 1. Ping / Zombie Detection ---
        _handleZombieDetection();

        // We consider "Connected" only if WiFi is connected AND we can ping gateway (not zombie)
        boolean effectivelyConnected = (WiFi.status() == WL_CONNECTED) && !m_isNetworkZombie;

        if (effectivelyConnected)
        {
          m_connectionLostTime = 0;

          // Requirement: connection re establish, switch to STA ONLY mode (disable AP if it was enabled)
          // If we are in AP+STA (or AP), switch back to STA.
          if (m_switching == NONE && (WiFi.getMode() & WIFI_AP))
          {
            WIFIMGR_DEBUG_MSG(F("Connection restored. Switching back to STA only.\n"));
            switchTo(SWITCHTOSTA);
          }
        }
        else
        {
          // DISCONNECTED (or Zombie)
          if (m_connectionLostTime == 0)
          {
            m_connectionLostTime = now;
            WIFIMGR_DEBUG_MSG(F("Connection lost or Zombie. Starting recovery timer.\n"));
          }

          // Retry every 20 seconds
          if (now - m_lastRetryTime > 20000)
          {
            m_lastRetryTime = now;
            WIFIMGR_DEBUG_MSG(F("Retry connection (20s interval)...\n"));
            if (m_isNetworkZombie)
            {
              WiFi.disconnect(); // Force disconnect to clear zombie state
              m_isNetworkZombie = false;
            }
            WiFi.begin(m_SettingsData.ssid_name, m_SettingsData.ssid_key);
          }

          // Check 15 minutes timeout (900,000 ms)
          if (now - m_connectionLostTime > 900000)
          {
            // > 15 mins: Enable AP (AP+STA) if not already in AP_STA
            if (m_switching == NONE && (WiFi.getMode() != WIFI_AP_STA))
            {
              WIFIMGR_DEBUG_MSG(F("Connection timeout (>15min). Switching to AP+STA.\n"));
              switchTo(SWITCHTOAPSTA);
            }
          }
          else
          {
            // < 15 mins: Force STA only
            // Requirement: "on essaye ... durant 15 minutes ... en mode STA"
            if (m_switching == NONE && (WiFi.getMode() != WIFI_STA))
            {
              WIFIMGR_DEBUG_MSG(F("Enforcing STA mode during first 15min.\n"));
              switchTo(SWITCHTOSTA);
            }
          }
        }
      }
    }
    else
    {
      // No settings: Force AP mode
      if (m_switching == NONE && m_WifiSetupState == WIFI_SETUP_IDLE && ((WiFi.getMode() & WIFI_AP_STA) == 0))
      {
        WIFIMGR_DEBUG_MSG(F("loop No settings. Forcing AP-STA mode.\n"));
        switchTo(SWITCHTOAPSTA);
      }
    }

    storeSettings();
  }
}

void WIFIManagerClass::_handleWifiSetup(AsyncWebServer *p_pWiFiServer, fs::FS *p_pfs)
{
  unsigned long now = millis();

  switch (m_WifiSetupState)
  {
  case WIFI_SETUP_IDLE:
    // Nothing to do
    break;

  case WIFI_SETUP_DISCONNECTING:
    if ((WiFi.getMode() & WIFI_STA) != 0)
    {
      WiFi.disconnect(true);
      WIFIMGR_DEBUG_MSG(F("_handleWifiSetup: Disconnecting WiFi STA...\n"));
    }
    if ((WiFi.getMode() & WIFI_AP) != 0)
    {
      WiFi.softAPdisconnect(true);
      m_isSoftAPDone = false;
      WiFi.disconnect(true);
      WIFIMGR_DEBUG_MSG(F("_handleWifiSetup: Disconnecting WiFi AP...\n"));
    }

    m_WifiSetupState = WIFI_SETUP_WAITING_DISCONNECT;
    m_WifiSetupTimeout = now;
    break;

  case WIFI_SETUP_WAITING_DISCONNECT:
    // Wait for disconnect to complete (2000ms)
    if (now - m_WifiSetupTimeout > 2000)
    {
      m_WifiSetupState = WIFI_SETUP_WEBHANDLER;
      m_WifiSetupTimeout = now;
      WIFIMGR_DEBUG_MSG(F("_handleWifiSetup: disconnected...\n"));
    }
    break;
  case WIFI_SETUP_WEBHANDLER:
    if (m_targetWiFiMode == WIFI_AP_STA)
    {
      WIFIMGR_DEBUG_MSG(F("_handleWifiSetup: Setting up web handlers for AP+STA mode...\n"));
      _setupWebHandlerAccessPoint(p_pWiFiServer, p_pfs);
    }
    else
    {
      WIFIMGR_DEBUG_MSG(F("_handleWifiSetup: Unsetting up web handlers for AP mode...\n"));
      _unsetupWebHandlerAccessPoint(p_pWiFiServer);
    }
    m_WifiSetupState = WIFI_SETUP_RADIO;
    m_WifiSetupTimeout = now;
    break;

  case WIFI_SETUP_RADIO:
    // Set AP mode and start softAP (do this once)
    WIFIMGR_DEBUG_MSG(F("_handleWifiSetup: WIFI_SETUP_RADIO to %s\n"), getWiFiModeString(m_targetWiFiMode).c_str());
    if (m_targetWiFiMode == WIFI_AP_STA)
    {
      _setupRadioAsAccessPoint();
    }
    else
    {
      _setupRadioAsStation(p_pWiFiServer, p_pfs);
    }

    m_WifiSetupState = WIFI_SETUP_WAITING_RADIO;
    m_WifiSetupTimeout = now;
    break;

  case WIFI_SETUP_WAITING_RADIO:
    if (SWITCHED == m_switching)
    {
      WIFIMGR_DEBUG_MSG(F("_handleWifiSetup: RADIO MODE SWITCH DONE\n"));
      WIFIMGR_DEBUG_MSG(
          F("wifi switch done in %d ms\n"),
          millis() - m_switchingtime);
      m_switching = NONE;
      m_switchingtime = 0;
      m_WifiSetupState = WIFI_SETUP_WAITING_CONNECTIVITY;
    }
    break;
  case WIFI_SETUP_WAITING_CONNECTIVITY:
    if (m_targetWiFiMode == WIFI_STA)
    {
      if (WiFi.localIP().isSet())
      {
        m_WifiSetupState = WIFI_SETUP_DONE;
        WIFIMGR_DEBUG_MSG(F("_handleWifiSetup: Mode STA ready\n"));
      }
    }
    else
    {
      if (m_isSoftAPDone)
      {
        m_WifiSetupState = WIFI_SETUP_DONE;
        WIFIMGR_DEBUG_MSG(F("_handleWifiSetup: Mode AP ready\n"));
      }
    }
    break;
  case WIFI_SETUP_DONE:
    // Reset state
    m_WifiSetupState = WIFI_SETUP_IDLE;
    WIFIMGR_DEBUG_MSG(F("_handleWifiSetup: setup done!\n"));
    break;
  }
}

void WIFIManagerClass::_handlemodeSwitch()
{
  size_t now = millis();
  if ((m_switchingtime != 0) && ((now - m_switchingtime) > 1000))
  {
    switch (m_switching)
    {
    case REBOOT:
      ESP.restart();
      break;
    case SWITCHTOAP:
      m_targetWiFiMode = WIFI_AP;
      m_WifiSetupState = WIFI_SETUP_WAITING_DISCONNECT;
      m_WifiSetupTimeout = now;
      break;
    case SWITCHTOSTA:
      m_targetWiFiMode = WIFI_STA;
      m_WifiSetupState = WIFI_SETUP_WAITING_DISCONNECT;
      m_WifiSetupTimeout = now;
      break;
    case SWITCHTOAPSTA:
      m_targetWiFiMode = WIFI_AP_STA;
      m_WifiSetupState = WIFI_SETUP_WAITING_DISCONNECT;
      m_WifiSetupTimeout = now;
      break;
    case SWITCHOFFANDREBOOT:
      WiFi.softAPdisconnect(true);
      WiFi.disconnect(true);
      // Removed delay(100) - now non-blocking
      WiFi.mode(WIFI_OFF);
      break;
    default:
      break;
    };
    // m_switchingtime = 0;
    m_switching = SWITCHING;
  }
}

String WIFIManagerClass::stringProcessor(const String &var)
{
  WIFIMGR_DEBUG_MSG(F("stringProcessor, var: %s\n"), var.c_str());
  wl_status_t wifiStatus = WiFi.status();
  if (isSettingExist() && (var == "SSIDNAME"))
    return m_SettingsData.ssid_name;

  if (var == "WIFICONNECTIONSTATUS")
    return (wifiStatus != WL_CONNECTED) ? "Not connected" : "Connected";

  if (var == "WIFINETWORKIP")
    return (wifiStatus != WL_CONNECTED) ? "Not connected"
                                        : WiFi.localIP().toString();

  if (var == "WIFIRSSI")
    return (wifiStatus != WL_CONNECTED) ? "Not connected" : String(WiFi.RSSI());

  return "";
}

bool WIFIManagerClass::isNetworkZombie() { return m_isNetworkZombie; }

void WIFIManagerClass::_unsetupWebHandlerAccessPoint(AsyncWebServer *p_pWiFiServer)
{
  if (m_pWifiScanHandler)
  {
    p_pWiFiServer->removeHandler(m_pWifiScanHandler);
    m_pWifiScanHandler = NULL;
  }
  if (m_pWifiPostHandler)
  {
    p_pWiFiServer->removeHandler(m_pWifiPostHandler);
    m_pWifiPostHandler = NULL;
  }
  if (m_pWifiStaticHandler)
  {
    p_pWiFiServer->removeHandler(m_pWifiStaticHandler);
    m_pWifiStaticHandler = NULL;
  }

  if (p_wifimanagerWebSocket)
  {
    p_wifimanagerWebSocket->enable(false);
    p_pWiFiServer->removeHandler(p_wifimanagerWebSocket);
    p_wifimanagerWebSocket = NULL;
  }

  m_setupWebHandlerAccessPointDone = false;
}

void WIFIManagerClass::_setupWebHandlerAccessPoint(AsyncWebServer *p_pWiFiServer,
                                                   fs::FS *p_pfs)
{
  WIFIMGR_DEBUG_MSG(F("_setupWebHandlerAccessPoint\n"));
  if (!m_setupWebHandlerAccessPointDone)
  {
    WIFIMGR_DEBUG_MSG(F("HTTP attach /wifi/scan\n"));
    m_pWifiScanHandler = &p_pWiFiServer->on(
        "/wifi/scan",
        HTTP_GET,
        [&](AsyncWebServerRequest *request)
        {
          WIFIMGR_DEBUG_MSG(F("GET /wifi/scan\n"));
          request->send(200);
          m_scanningState = SCANNING_DELAYTOSTART;
        });
    m_pWifiScanHandler->setFilter(ON_AP_FILTER);

    WIFIMGR_DEBUG_MSG(F("HTTP attach POST /wifi\n"));
    AsyncCallbackJsonWebHandler *handler = new AsyncCallbackJsonWebHandler(
        "/wifi",
        [&](AsyncWebServerRequest *request, JsonVariant &json)
        {
          const JsonObject &jsonObj = json.as<JsonObject>();
          WIFIMGR_DEBUG_MSG(F("POST /wifi (%s)\n"),
                            (const char *)(jsonObj["ssid-name"]));
          strncpy(this->m_SettingsData.ssid_name,
                  (const char *)(jsonObj["ssid-name"]), 32);
          this->m_SettingsData.ssid_name[31] = '\0';
          strncpy(this->m_SettingsData.ssid_key,
                  (const char *)(jsonObj["ssid-pwd"]), 64);
          this->m_SettingsData.ssid_key[63] = '\0';
          this->m_SettingsData.tag = MAGICWIFIMGR;
          this->isSettingsDirty = true;
          request->send(200, "application/json", "{\"redirect\":\"" + String(m_pName) + "\"}");
          switchTo(SWITCHTOSTA);
        });
    handler->setMethod(HTTP_POST);
    m_pWifiPostHandler = handler;
    p_pWiFiServer->addHandler(handler);
    m_pWifiPostHandler->setFilter(ON_AP_FILTER);

    WIFIMGR_DEBUG_MSG(F("HTTP attach /wifi\n"));
    m_pWifiStaticHandler = &p_pWiFiServer->serveStatic("/wifi", *p_pfs, "/wifi")
                                .setTemplateProcessor([this](const String &var) -> String
                                                      { return this->stringProcessor(var); });
    m_pWifiStaticHandler->setFilter(ON_AP_FILTER);

    m_setupWebHandlerAccessPointDone = true;
  };

  WIFIMGR_DEBUG_MSG(F("WebScock attach /wifi\n"));
  if (!p_wifimanagerWebSocket)
    p_wifimanagerWebSocket = new AsyncWebSocket("/wifi");
  p_pWiFiServer->addHandler(p_wifimanagerWebSocket);
  p_wifimanagerWebSocket->enable(true);

  WIFIMGR_DEBUG_MSG(F("END _setupWebHandlerAccessPoint\n"));
}

const char *WIFIManagerClass::_switchingActionToString(switchingAction action)
{
  switch (action)
  {
  case NONE:
    return "NONE";
  case SWITCHTOAP:
    return "SWITCHTOAP";
  case SWITCHTOSTA:
    return "SWITCHTOSTA";
  case SWITCHTOAPSTA:
    return "SWITCHTOAPSTA";
  case REBOOT:
    return "REBOOT";
  case SWITCHOFFANDREBOOT:
    return "SWITCHOFFANDREBOOT";
  case SWITCHING:
    return "SWITCHING";
  default:
    return "UNKNOWN";
  }
}

void WIFIManagerClass::_setupRadioAsStation(AsyncWebServer *p_pWiFiServer,
                                            fs::FS *p_pfs)
{
  WiFi.mode(WIFI_STA);
  WIFIMGR_DEBUG_MSG(F("Connecting AS %s\n"), m_pName);
  WiFi.hostname(m_pName);
  WIFIMGR_DEBUG_MSG(F("Connecting WIFI %s\n"), m_SettingsData.ssid_name);
  WiFi.begin(m_SettingsData.ssid_name, m_SettingsData.ssid_key);
  m_connectingStartTime = millis();
  m_lastRetryTime = millis(); // Avoid immediate retry in loop
}

void WIFIManagerClass::_setupRadioAsAccessPoint()
{
  WiFi.mode(WIFI_AP_STA);
  WIFIMGR_DEBUG_MSG(F("Starting softAP (%s)\n"), m_pName);
  m_isSoftAPDone = WiFi.softAP(m_pName, "password", 8, false, 1);
}

void WIFIManagerClass::switchTo(switchingAction action)
{
  WIFIMGR_DEBUG_MSG(F("switchTo %s (%s)\n"), _switchingActionToString(action), getWiFiModeString(WiFi.getMode()).c_str());
  m_switchingtime = millis();
  m_switching = action;
}

void WIFIManagerClass::storeSettings()
{
  if (isSettingsDirty)
  {
    WIFIMGR_DEBUG_MSG(F("Store settings\n"));
    m_SettingsData.tag = MAGICWIFIMGR;
    EEPROMEX.put(m_SettingsPersistanceIndex, m_SettingsData);
    isSettingsDirty = false;
  }
}

boolean WIFIManagerClass::isSettingExist()
{
  return (m_SettingsData.tag == MAGICWIFIMGR &&
          strlen(m_SettingsData.ssid_name) != 0 &&
          strlen(m_SettingsData.ssid_key) != 0);
}

void WIFIManagerClass::_handleZombieDetection()
{
  if (WiFi.status() == WL_CONNECTED)
  {
    unsigned long now = millis();
    // If connected, check if we can ping the gateway to detect "zombie" state (connected but no real connectivity)
    // Ping every 60 seconds
    if (now - m_lastPingTime > 60000)
    {
      m_lastPingTime = now;
      IPAddress gw = WiFi.gatewayIP();
      if (gw != IPAddress(0, 0, 0, 0))
      {
        m_Ping.begin(gw, 1, 10000);
      }
    }
  }
  else
  {
    m_failedPingCount = 0;
    m_isNetworkZombie = false;
    m_Ping.cancel();
  }
}

void WIFIManagerClass::pushNotif(JsonVariantConst message)
{
  if (!p_wifimanagerWebSocket || p_wifimanagerWebSocket->count() == 0)
    return;

  String output;
#ifdef ARDUINOJSON_5_COMPATIBILITY
  message.printTo(output);
#else
  serializeJson(message, output);
#endif
  p_wifimanagerWebSocket->printfAll(output.c_str());
}
