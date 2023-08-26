
#include <Arduino.h>
#include <ESP8266mDNS.h>
#include "FS.h" // SPIFFS is declared
// #include "LittleFS.h" // LittleFS is declared
#include "EEPROMEX.h"
#include <ArduinoOTA.h>
// #include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <SPIFFSEditor.h>
#include <ArduinoJson.h>
#include "WifiManager.hpp"
#include "PowermetersBoard.hpp"

#ifdef DEBUG_MAIN
#ifdef DEBUG_ESP_PORT
#define MAIN_DEBUG_INIT(x) DEBUG_ESP_PORT.begin(x)
#define MAIN_DEBUG_MSG(...)        \
  DEBUG_ESP_PORT.print("[MAIN] "); \
  DEBUG_ESP_PORT.printf(__VA_ARGS__)
#else
#define MAIN_DEBUG_INIT(x)
#define MAIN_DEBUG_MSG(...)
#endif
#else
#define MAIN_DEBUG_INIT(x)
#define MAIN_DEBUG_MSG(...)
#endif

AsyncWebSocket m_ws("/ws");
AsyncWebServer m_WebServer(80);
FS m_fileSystem = SPIFFS;
WiFiClient m_espClient;

#ifdef EEPROMMAGIC
#define MAGIC EEPROMMAGIC
#else
#define MAGIC 159
#endif

// struct settings
// {
//   int tag;
//   char node_name[32];
//   char openhab_mqtt_url[128];
//   int openhab_mqtt_port = 1883;
//   char openhab_mqtt_login[32];
//   char openhab_mqtt_pwd[64];
// };

int _SettingsPersistanceIndex;
// boolean _IsSettingsDirty = false;
// settings _SettingsData;

// DEBUG INFO
boolean isfirst = true;
uint32_t freePreviousLoop = 0;
int memleak = 0;
int memUsed = 0;
int m_NbBootPersistanceIndex = 0;
int nbBoot = 0;
// DEBUG INFO

// boolean isSettingsDefined()
// {
//   return (_SettingsData.tag == MAGIC);
// }

String stringProcessor(const String &var)
{
  MAIN_DEBUG_MSG("stringProcessor %s\n", var.c_str());
  if (var == "BOOT")
  {
    return String(nbBoot);
  }
  else if (var == "USEDMEM")
  {
    return String(-memUsed);
  }
  return "";
}

WiFiEventHandler gotIpEventHandler, disconnectedEventHandler;
void setup()
{
  MAIN_DEBUG_INIT(9600);
  MAIN_DEBUG_MSG("\n");

  freePreviousLoop = ESP.getFreeHeap();
  memleak = 0;
  memUsed = 0;

  /**************************/
  /* Persistance Management */
  /*    Memory allocation   */
  /**************************/
  // _SettingsPersistanceIndex = EEPROMEX.allocate(sizeof(struct settings));
  POWERMETERBOARD.setupPersistance();
  WIFIMANAGER.setupPersistance();
  m_NbBootPersistanceIndex = EEPROMEX.allocate(sizeof(int));

  /**************************/
  /* Persistance Management */
  /*       Activation       */
  /**************************/
  EEPROMEX.begin();
  // EEPROMEX.get(_SettingsPersistanceIndex, _SettingsData);
  // _IsSettingsDirty = false;
  // MAIN_DEBUG_MSG("%setting available\n", isSettingsDefined() ? "S" : "No s");
  /**************************/

  /****************/
  /* DEBUG REBOOT */
  /****************/
    EEPROMEX.get(m_NbBootPersistanceIndex, nbBoot);
    nbBoot++;
    EEPROMEX.put(m_NbBootPersistanceIndex, nbBoot);
    // _IsSettingsDirty = true;
  /****************/

  /**************************/
  /* Reaction to WIFI event */
  /**************************/
  gotIpEventHandler = WiFi.onStationModeGotIP([](const WiFiEventStationModeGotIP &event)
                                              {
    MAIN_DEBUG_MSG("Station connected, IP: %s\n", WiFi.localIP().toString().c_str());
    /*******************/
    /*  Multicast DNS  */
    /*   Activation    */
    /*******************/
    MAIN_DEBUG_MSG("Starting mDNS\n");
    if (!MDNS.begin("powermetersboard"))
    {
      MAIN_DEBUG_MSG("Fails to start mDNS\n");
    } else {
      //Add service to MDNS-SD
      MDNS.addService("http", "tcp", 80);
    } });

  disconnectedEventHandler = WiFi.onStationModeDisconnected([](const WiFiEventStationModeDisconnected &event)
                                                            { MAIN_DEBUG_MSG("Station disconnected\n"); });
  /**************************/
  WIFIMANAGER.setup(&m_WebServer, m_fileSystem);
  // Add service to MDNS-SD

  /******************/
  /*  OTA services  */
  /*   Activation   */
  /******************/
  // Hostname
  // ArduinoOTA.setHostname("powermetersboard");
  // // define authentication by default
  // ArduinoOTA.setPassword((const char *)"1234!");
  // //
  // ArduinoOTA.begin();
  // // react to ota session start
  // ArduinoOTA.onStart([]() {
  //   POWERMETERBOARD.suspend(true);
  //   m_fileSystem.end();
  // });
  // // react to ota session start
  // ArduinoOTA.onEnd([]() {
  //   POWERMETERBOARD.suspend(false);
  //   m_fileSystem.begin();
  // });
  /******************/

  m_fileSystem.begin();

  if (!WIFIMANAGER.isSettingExist())
  {
    MAIN_DEBUG_MSG("start As Access point\n");
    // send a file when /index is requested
  }
  else
  {
    POWERMETERBOARD.setup("DEV_PowerMeters", m_espClient, &m_WebServer, m_fileSystem);

    m_WebServer.serveStatic("/debug", m_fileSystem, "/debug").setTemplateProcessor([](const String &var) -> String
                                                                                   {
      String value = WIFIMANAGER.stringProcessor(var);
      if (value.length() == 0)
      {
         value = POWERMETERBOARD.stringProcessor(var);
          if (value.length() == 0)
          {
            value = stringProcessor(var);
          }
      }
      return value; });
    m_WebServer.addHandler(&m_ws);
    m_ws.enable(true);
  }

  m_WebServer.onNotFound([](AsyncWebServerRequest *request)
                         { request->send(404, "text/plain", "Not found"); });

  m_WebServer.begin();
  MAIN_DEBUG_MSG("Setup ending\n");
}

unsigned int indice = 0;
int lastMemUsed = 0;
void loop()
{
  m_ws.cleanupClients();
  uint32_t freeHeap = ESP.getFreeHeap();

  memleak = freeHeap - freePreviousLoop;
  memUsed += memleak;
  freePreviousLoop = freeHeap;
  if (++indice > 10000)
  {
    // MAIN_DEBUG_MSG("%d\n",indice);
    if (lastMemUsed != memUsed)
    {
      m_ws.printfAll("{ \"memUsed\" : %d }", -memUsed);
      lastMemUsed = memUsed;
    }
    indice = 0;
  }
  /* OTA */
  // ArduinoOTA.handle();
  POWERMETERBOARD.loop();
  WIFIMANAGER.loop();
  MDNS.update();

  noInterrupts();
  EEPROMEX.commit();
  interrupts();
}