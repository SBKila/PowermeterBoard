

#include <Arduino.h>
#include <ESP8266mDNS.h>
#include "FS.h" // SPIFFS is declared
//#include "LittleFS.h" // LittleFS is declared
#include "EEPROMEX.h"
#include <ArduinoOTA.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <SPIFFSEditor.h>
#include <ArduinoJson.h>
#include "WifiManager.hpp"
#include "PowermetersBoard.hpp"

#ifdef DEBUG_MAIN
#ifdef DEBUG_ESP_PORT
#define MAIN_DEBUG_INIT(x) DEBUG_ESP_PORT.begin(x)
#define MAIN_DEBUG_MSG(...)       \
  DEBUG_ESP_PORT.print("[MAIN] "); \
  DEBUG_ESP_PORT.printf(__VA_ARGS__)
#else
#define DEBUG_INIT(x)
#define MAIN_DEBUG_MSG(...)
#endif
#else
#define DEBUG_INIT(x)
#define MAIN_DEBUG_MSG(...)
#endif

AsyncWebServer m_WiFiServer(80);
FS m_fileSystem = SPIFFS;
WiFiClient m_espClient;
#define MAGIC 933

struct settings
{
  int tag;
  char node_name[32];
  char openhab_mqtt_url[128];
  int openhab_mqtt_port = 1883;
  char openhab_mqtt_login[32];
  char openhab_mqtt_pwd[64];
};
int _SettingsPersistanceIndex;
boolean _IsSettingsDirty = false;
settings _SettingsData;
boolean isSettingsDefined()
{
  return (_SettingsData.tag != MAGIC);
}

WiFiEventHandler gotIpEventHandler, disconnectedEventHandler;

void setup()
{
  MAIN_DEBUG_INIT(9600);
  MAIN_DEBUG_MSG("\n");

  /**************************/
  /* Persistance Management */
  /*    Memory allocation   */
  /**************************/
  POWERMETERBOARD.setupPersistance();
  WIFIMANAGER.setupPersistance();
  _SettingsPersistanceIndex = EEPROMEX.allocate(sizeof(struct settings));

  /**************************/
  /* Persistance Management */
  /*       Activation       */
  /**************************/
  EEPROMEX.begin();
  EEPROMEX.get(_SettingsPersistanceIndex, _SettingsData);
  _IsSettingsDirty = false;
  MAIN_DEBUG_MSG("%setting available\n", isSettingsDefined()?"S":"No s");
  /**************************/

  /**************************/
  /* Reaction to WIFI event */
  /**************************/
  gotIpEventHandler = WiFi.onStationModeGotIP([](const WiFiEventStationModeGotIP &event) {
    MAIN_DEBUG_MSG("Station connected, IP: %s\n", WiFi.localIP().toString().c_str());
    /*******************/
    /*  Multicast DNS  */
    /*   Activation    */
    /*******************/
    MAIN_DEBUG_MSG("Starting mDNS\n");
    if (!MDNS.begin("powermetersboard"))
    {
      MAIN_DEBUG_MSG("Fails to start mDNS\n");
    }
    //Add service to MDNS-SD
    MDNS.addService("http", "tcp", 80);
  });

  disconnectedEventHandler = WiFi.onStationModeDisconnected([](const WiFiEventStationModeDisconnected &event) {
    MAIN_DEBUG_MSG("Station disconnected\n");
  });
  /**************************/

  WIFIMANAGER.setup(&m_WiFiServer, m_fileSystem);
  POWERMETERBOARD.setup("WH_PowerMeters", m_espClient, "openHabMqttUrl", 8316, &m_WiFiServer, m_fileSystem);
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
    // send a file when /index is requested
    m_WiFiServer.on("/", HTTP_ANY, [](AsyncWebServerRequest *request) { request->redirect("/wifi/setup/"); });
  }
  else
  {
    m_WiFiServer.serveStatic("/", m_fileSystem, "/").setTemplateProcessor([](const String &var) -> String {
      MAIN_DEBUG_MSG("main TemplateProcessor");
      String value = WIFIMANAGER.setupTemplateProcessor(var);
      if (value.length() == 0)
      {
        if (var == "WIFICONNECTIONSTATUS")
        {
          return (WiFi.status() != WL_CONNECTED) ? "Not connected" : "Connected";
        }
        else
        {
          return String();
        }
      }
      else
      {
        return value;
      }
    });
  }

  m_WiFiServer.onNotFound([](AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "Not found");
  });

  m_WiFiServer.begin();
  MAIN_DEBUG_MSG("Setup ending\n");
}

void loop()
{
  /* OTA */
  //ArduinoOTA.handle();
  
  WIFIMANAGER.loop();
  MDNS.update();
}