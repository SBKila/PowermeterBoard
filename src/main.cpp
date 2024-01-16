
#include <Arduino.h>
#include "myDebugLogs.h"
#include "global.h"

#include <ESP8266mDNS.h>
#include "FS.h" // SPIFFS is declared
#include <SPIFFSEditor.h>
// #include "LittleFS.h" // LittleFS is declared
#include "EEPROMEX.h"
#include <ArduinoOTA.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <AsyncJson.h>
// #include "magiclogger.hpp"
#include "WifiManager.hpp"
#include "PowermetersBoard.hpp"
boolean webserverstarted = false;
AsyncWebServer m_WebServer(80);
FS m_fileSystem = SPIFFS;
WiFiClient m_espClient;
AsyncWebSocket m_ws("/ws");
char logString[256];

#ifdef DEBUG_MAIN
#define MAIN_DEBUG_MSG(...) DEBUG_MSG("MAIN", __VA_ARGS__)
#else
#define MAIN_DEBUG_MSG(...)
#endif

int _SettingsPersistanceIndex;
int m_NbBootPersistanceIndex = 0;
int nbBoot = 0;

// unsigned int indice = 0;
uint32_t freePreviousLoop = 0;
int memleak = 0;
int memUsed = 0;
int lastMemUsed = 0;
void trackMemLeaksSetup()
{
  freePreviousLoop = ESP.getFreeHeap();
  memleak = 0;
  memUsed = 0;
}
void trackMemLeaksLoop()
{
  uint32_t freeHeap = ESP.getFreeHeap();
  memleak = freeHeap - freePreviousLoop;
  memUsed += memleak;
  freePreviousLoop = freeHeap;
  // if (++indice > 10000)
  // {
  // MAIN_DEBUG_MSG("%d\n",indice);
  if (lastMemUsed != memUsed)
  {
    m_ws.printfAll("{ \"memUsed\" : %d }", -memUsed);
    // WebSerial.printf("Free heap=[%u]\n", ESP.getFreeHeap());
    lastMemUsed = memUsed;
  }
  //   indice = 0;
  // }
}

unsigned long lastBlinkTime = 0;
unsigned long blinkdelay = 0;
boolean blinkOn = false;
#define LED D4
#define FOURTHBYSECOND 225
#define TWICEBYSECOND 475
#define ONEBYSECOND 975
#define ONEBY10SECONDS 9975
#define TWICEBYMINUTE 29975
void blinkSetup()
{
  pinMode(LED, OUTPUT);
}
void blinkLoop()
{
  unsigned long now = millis();
  if ((now - lastBlinkTime) > blinkdelay)
  {
    lastBlinkTime = now;
    blinkOn = !blinkOn;
    int duration = 25;
    if ((WiFi.getMode() == WIFI_AP_STA))
    {
      duration = FOURTHBYSECOND;
    }
    else if ((WiFi.getMode() & WIFI_AP) == WIFI_AP)
    {
      duration = TWICEBYSECOND;
    }
    else if ((WiFi.getMode() & WIFI_STA) == WIFI_STA)
    {
      duration = WiFi.isConnected() ? TWICEBYMINUTE : ONEBYSECOND;
    }

    // MAIN_DEBUG_MSG("Mqtt%sconnected\n",POWERMETERBOARD.isMqttConnected()?" ":" not ");
    // MAIN_DEBUG_MSG("duration %d",duration);
    blinkdelay = blinkOn ? 25 : duration; // + (blinkOn ? 0 : duration);
    // MAIN_DEBUG_MSG("%s %lu\n",blinkOn?"HIGH":"LOW",blinkdelay);
    digitalWrite(LED, blinkOn ? LOW : HIGH);
  }
}



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
  DEBUG_INIT(9600);
  delay(200);
  MAIN_DEBUG_MSG("Setup\n");

  trackMemLeaksSetup();

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
  /**************************/

  /****************/
  /* DEBUG REBOOT */
  /****************/
  EEPROMEX.get(m_NbBootPersistanceIndex, nbBoot);
  nbBoot++;
  EEPROMEX.put(m_NbBootPersistanceIndex, nbBoot);
  /****************/

  /**************************/
  /* Reaction to WIFI event */
  /**************************/
  gotIpEventHandler = WiFi.onStationModeGotIP(
      [](const WiFiEventStationModeGotIP &event)
      {
        MAIN_DEBUG_MSG("Station connected, IP: %s\n", WiFi.localIP().toString().c_str());
        /*******************/
        /*  Multicast DNS  */
        /*   Activation    */
        /*******************/
        MAIN_DEBUG_MSG("Starting mDNS %s\n", PMBNAME);
        if (!MDNS.begin(PMBNAME))
        {
          MAIN_DEBUG_MSG("Fails to start mDNS\n");
        }
        else
        {
          // Add service to MDNS-SD
          MDNS.addService("http", "tcp", 80);
        }
      });

  disconnectedEventHandler = WiFi.onStationModeDisconnected([](const WiFiEventStationModeDisconnected &event)
                                                            { MAIN_DEBUG_MSG("Station disconnected\n"); });
  /****************/
  /*  WiFiManager */
  /*  Activation  */
  /****************/
  WIFIMANAGER.setup(PMBNAME, &m_WebServer, &m_fileSystem);

  /******************/
  /*  OTA services  */
  /*   Activation   */
  /******************/
  // Hostname
  // ArduinoOTA.setHostname(PMBNAME);
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
    MAIN_DEBUG_MSG("start as Access point\n");
    // send a file when /index is requested
  }
  else
  {
    MAIN_DEBUG_MSG("start as Client\n");
    POWERMETERBOARD.setup(PMNAME, m_espClient, &m_WebServer, m_fileSystem);
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
  webserverstarted = true;

  blinkSetup();

  MAIN_DEBUG_MSG("Setup ending\n");
}



void loop()
{
  // Browsers sometimes do not correctly close the WebSocket connection explanation from the ESPAsyncWebServer library GitHub page
  m_ws.cleanupClients();

  trackMemLeaksLoop();

  /* OTA */
  // ArduinoOTA.handle();
  POWERMETERBOARD.loop();
  WIFIMANAGER.loop();
  MDNS.update();

  noInterrupts();
  EEPROMEX.commit();
  interrupts();

  blinkLoop();
}