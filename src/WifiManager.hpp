#ifndef WIFIMGR_H
#define WIFIMGR_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <ArduinoJson.h>
#include <EEPROMEX.h>

#pragma once

const char string_WIFI_OFF[] PROGMEM = "WIFI_OFF"; // "String 0" etc are strings to store - change to suit.
const char string_WIFI_STA[] PROGMEM = "WIFI_STA";
const char string_WIFI_AP[] PROGMEM = "WIFI_AP";
const char string_WIFI_AP_STA[] PROGMEM = "WIFI_AP_STA";
const char *const strings_WiFiMode[] PROGMEM = {string_WIFI_OFF, string_WIFI_STA, string_WIFI_AP, string_WIFI_AP_STA};

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
AsyncWebSocket m_wifimanagerWebSocket("/wifi");
WiFiEventHandler onWiFiModeChangeHandler, onStationModeGotIPHandler /*, onStationModeDisconnectedHandler*/;
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
    WIFIManagerClassconst char *p_pName() : m_jsonDoc(1 + ((1 + 32 + 1 + 1) * 5) + 1) // buffer to return array of ssid ["ssid",] (1+((1+32+1+1)*5)+1)
    {
        m_pName = strupr(p_pName);
    }
#else
    WIFIManagerClass()
    {
    }
#endif
    ~WIFIManagerClass()
    {
        if (NULL != m_pName)
        {
            free(m_pName);
        }
    }
    void setupPersistance()
    {
        // manage WFIF setting
        m_SettingsPersistanceIndex = EEPROMEX.allocate(sizeof(struct WIFIsettings));
    }

    void setupAsBothMode(AsyncWebServer *p_pWiFiServer, fs::FS *p_pfs)
    {
        WIFIMGR_DEBUG_MSG("setupAsBothMode\n");
        WiFi.mode(WIFI_AP_STA);
        _setupAccessPoint(p_pWiFiServer, p_pfs);
        _setupStation(p_pWiFiServer, p_pfs);
    }

    void setupAsAccessPointMode(AsyncWebServer *p_pWiFiServer, fs::FS *p_pfs)
    {
        WIFIMGR_DEBUG_MSG("setupAsAccessPoint\n");
        WiFi.disconnect(true);
        delay(200);
        WiFi.mode(WIFI_AP);
        _setupAccessPoint(p_pWiFiServer, p_pfs);
    }
    boolean setupAccessPointDone = false;
    boolean startScanning = false;
    void _setupAccessPoint(AsyncWebServer *p_pWiFiServer, fs::FS *p_pfs)
    {
        if (!setupAccessPointDone)
        {
            // attach a http handlers
            // to redirect root to wifisetup
            p_pWiFiServer->on(
                             "/",
                             HTTP_ANY,
                             [](AsyncWebServerRequest *request)
                             {
                                 request->redirect("/wifi/setup.html");
                             })
                .setFilter(ON_AP_FILTER);
            // to get list of available SSID
            p_pWiFiServer->on(
                             "/wifi/scan",
                             HTTP_GET,
                             [&](AsyncWebServerRequest *request)
                             {
                                 WIFIMGR_DEBUG_MSG("GET /wifi/scan\n");
                                 startScanning = true;
                                 request->send(200);
                             })
                .setFilter(ON_AP_FILTER);
            // to setup wifi
            AsyncCallbackJsonWebHandler *handler = new AsyncCallbackJsonWebHandler(
                "/wifi",
                [&](AsyncWebServerRequest *request, JsonVariant &json)
                {
                    const JsonObject &jsonObj = json.as<JsonObject>();
                    WIFIMGR_DEBUG_MSG("POST /wifi (%s,%s)\n", (const char *)(jsonObj["ssid-name"]), (const char *)(jsonObj["ssid-pwd"]));
                    strncpy(this->m_SettingsData.ssid_name, (const char *)(jsonObj["ssid-name"]), 32);
                    strncpy(this->m_SettingsData.ssid_key, (const char *)(jsonObj["ssid-pwd"]), 64);
                    this->m_SettingsData.tag = MAGICWIFIMGR;
                    this->isSettingsDirty = true;
                    request->send(200,"");
                    switchTo(SWITCHTOAPSTA);
                });
            handler->setMethod(HTTP_POST);
            p_pWiFiServer->addHandler(handler).setFilter(ON_AP_FILTER);

            p_pWiFiServer->serveStatic("/wifi", *p_pfs, "/wifi")
                .setTemplateProcessor([this](const String &var) -> String
                                      { return this->stringProcessor(var); })
                .setFilter(ON_AP_FILTER);

            setupAccessPointDone = true;
        };
        // start Access Point for 1 connection
        WIFIMGR_DEBUG_MSG("start wifi access point\n");
        boolean result = WiFi.softAP(m_pName, "password", 8, false, 1);
        if (result)
        {
            WIFIMGR_DEBUG_MSG("AP IP address: %s\n", WiFi.softAPIP().toString().c_str());
        }

        p_pWiFiServer->addHandler(&m_wifimanagerWebSocket);
        m_wifimanagerWebSocket.enable(true);
    }
    void setupAsStationMode(AsyncWebServer *p_pWiFiServer, fs::FS *p_pfs)
    {
        /***************/
        /* STA section */
        /***************/
        if (WiFi.getMode() != WIFI_STA)
        {
            WIFIMGR_DEBUG_MSG("setupAsStationMode %s\n",(const __FlashStringHelper *)strings_WiFiMode[WiFi.getMode()]);
            WiFi.mode(WIFI_STA);
            //WiFi.disconnect(true);
            _setupStation(p_pWiFiServer, p_pfs);
            //p_pWiFiServer->reset();
        }
    }
    void _setupStation(AsyncWebServer *p_pWiFiServer, fs::FS *p_pfs)
    {
        WiFi.hostname(m_pName);
        WIFIMGR_DEBUG_MSG("Connecting WIFI %s:%s\n", m_SettingsData.ssid_name, m_SettingsData.ssid_key);
        WiFi.begin(m_SettingsData.ssid_name, m_SettingsData.ssid_key);
        // WiFi.begin((const char *)"toto", (const char *)m_SettingsData.ssid_key);
        m_connectingStartTime = millis();
    }
    void setup(const char *p_pName, AsyncWebServer *p_pWiFiServer, fs::FS *p_pfs)
    {
        WiFi.persistent(false);
        m_pfs = p_pfs;
        m_pWiFiServer = p_pWiFiServer;

        // set wifi name
        m_pName = strdup(p_pName);

        // restore config from persistence
        EEPROMEX.get(m_SettingsPersistanceIndex, m_SettingsData);
        isSettingsDirty = false;

        /**************************/
        /* Reaction to WIFI event */
        /**************************/
        onWiFiModeChangeHandler = WiFi.onWiFiModeChange(
            [](const WiFiEventModeChange &event)
            {
                WIFIMGR_DEBUG_MSG("wifi mode change %s->%s\n", (const __FlashStringHelper *)strings_WiFiMode[event.oldMode], (const __FlashStringHelper *)strings_WiFiMode[event.newMode]);
            });
        onStationModeGotIPHandler = WiFi.onStationModeGotIP(
            [this](const WiFiEventStationModeGotIP &event)
            {
                WIFIMGR_DEBUG_MSG("Station connected, IP: %s\n", WiFi.localIP().toString().c_str());
                // Allocate the JSON document
                JsonDocument notifEvent;
                // Add evnt type
                notifEvent["evt"] = 0;
                // Add ip
                notifEvent["data"] = WiFi.localIP().toString();
                this->pushNotif(notifEvent);
                switchTo(SWITCHTOSTA);
            });
        // onStationModeDisconnectedHandler = WiFi.onStationModeDisconnected([](const WiFiEventStationModeDisconnected &event)
        //                                { WIFIMGR_DEBUG_MSG("Station disconnected\n"); });

        WIFIMGR_DEBUG_MSG("%settings available\n", isSettingExist() ? "S" : "No s");
        if (isSettingExist())
        {
            setupAsStationMode(p_pWiFiServer, p_pfs);
        }
        else
        {
            setupAsAccessPointMode(p_pWiFiServer, p_pfs);
        }
    }

    String stringProcessor(const String &var)
    {
        wl_status_t wifiStatus = WiFi.status();
        if (isSettingExist() && (var == "SSIDNAME"))
            return m_SettingsData.ssid_name;

        if (var == "WIFICONNECTIONSTATUS")
            return (wifiStatus != WL_CONNECTED) ? "Not connected" : "Connected";

        if (var == "WIFINETWORKIP")
            return (wifiStatus != WL_CONNECTED) ? "Not connected" : WiFi.localIP().toString();

        if (var == "WIFIRSSI")
            return (wifiStatus != WL_CONNECTED) ? "Not connected" : String(WiFi.RSSI());

        return String();
    }

    // int connectingError = 0;
    enum switchingAction
    {
        NONE,
        SWITCHTOAP,
        SWITCHTOSTA,
        SWITCHTOAPSTA,
        REBOOT,
        SWITCHOFFANDREBOOT
    };
    unsigned long m_switchingtime = 0;
    switchingAction m_switching = NONE;
    void switchTo(switchingAction action)
    {
        m_switchingtime = millis();
        m_switching = action;
    }
    long startReboot = 0;
    void loop()
    {
        if (m_switching != NONE)
        {
            if ((m_switchingtime!=0) && ((millis() - m_switchingtime) > 1000))
            {
                switch (m_switching)
                {
                case REBOOT:
                    ESP.restart();
                    break;
                case SWITCHTOAP:
                    setupAsAccessPointMode(m_pWiFiServer, m_pfs);
                    break;
                case SWITCHTOSTA:
                    setupAsStationMode(m_pWiFiServer, m_pfs);
                    break;
                case SWITCHTOAPSTA:
                    setupAsBothMode(m_pWiFiServer, m_pfs);
                    break;
                case SWITCHOFFANDREBOOT:
                    WiFi.softAPdisconnect(true);
                    WiFi.disconnect(true);
                    delay(100);
                    WiFi.mode(WIFI_OFF);
                    break;
                };
                m_switchingtime = 0;
                m_switching = NONE;
            }
        }

        if ((WiFi.getMode() & WIFI_AP) == WIFI_AP)
        {
            int found = WiFi.scanComplete();
            if (found > 0)
            {
                WIFIMGR_DEBUG_MSG("%d available wifi\n", found);

                // Allocate the JSON document
                JsonDocument notifEvent;
                // Add evnt type
                notifEvent["evt"] = 1;
                // Add SSIDs
                JsonArray data = notifEvent["data"].to<JsonArray>();
                for (int i = 0; i < found /*&& i < 10*/; i++)
                {
                    data.add(String(WiFi.SSID(i)) + " ch:" + WiFi.channel(i) + " (" + WiFi.RSSI(i) + ")");
                    WIFIMGR_DEBUG_MSG("%d: %s, Ch:%d (%ddBm) %s\n", i + 1, WiFi.SSID(i).c_str(), WiFi.channel(i), WiFi.RSSI(i), WiFi.encryptionType(i) == ENC_TYPE_NONE ? "open" : "");
                }
                WiFi.scanDelete();
                WIFIMGR_DEBUG_MSG("end scanning available wifi\n");

                pushNotif(notifEvent);
                switchTo(SWITCHTOAP);
            }
        }
        if ((WiFi.getMode() & WIFI_STA) == WIFI_STA)
        {
            if (((WiFi.getMode() & WIFI_AP) != WIFI_AP) && (WiFi.status() != WL_CONNECTED) && ((millis() - m_connectingStartTime) > 30000))
            {
                WIFIMGR_DEBUG_MSG("Fallback Access Point mode\n");
                m_connectingStartTime = millis();
                switchTo(SWITCHTOAPSTA);
            }
        }

        storeSettings();

        if (startScanning)
        {
            startScanning = false;
            WIFIMGR_DEBUG_MSG("start scanning available wifi\n");
            WiFi.scanNetworks(true);
        }
    }
    void storeSettings()
    {
        if (isSettingsDirty)
        {
            WIFIMGR_DEBUG_MSG("Store settings\n");
            m_SettingsData.tag = MAGICWIFIMGR;
            // WIFIMGR_DEBUG_MSG("Data %d %s:%s\n", m_SettingsData.tag, m_SettingsData.ssid_name, m_SettingsData.ssid_key);
            EEPROMEX.put(m_SettingsPersistanceIndex, m_SettingsData);
            isSettingsDirty = false;
        }
    }
    boolean isSettingExist()
    {
        // WIFIMGR_DEBUG_MSG("%d==%d %s\n",m_SettingsData.tag,MAGICWIFIMGR,(m_SettingsData.tag == MAGICWIFIMGR)?"true":"false");
        return (m_SettingsData.tag == MAGICWIFIMGR && strlen(m_SettingsData.ssid_name) != 0 && strlen(m_SettingsData.ssid_key) != 0);
    }
    void pushNotif(JsonVariantConst message)
    {
        String output;
#ifdef ARDUINOJSON_5_COMPATIBILITY
        message.printTo(output);
#else
        serializeJson(message, output);
#endif
        m_wifimanagerWebSocket.printfAll(output.c_str());
    }

protected:
    int m_SettingsPersistanceIndex = 0;
    boolean isSettingsDirty = false;
    WIFIsettings m_SettingsData;
#ifdef ARDUINOJSON_6_COMPATIBILITY
    DynamicJsonDocument m_jsonDoc;
#else
    JsonDocument m_jsonDoc;
#endif
    unsigned long m_connectingStartTime = 0;
    AsyncWebServer *m_pWiFiServer = NULL;

    fs::FS *m_pfs;
    // boolean m_setupDone = false;
private:
    char *m_pName = NULL;
    char *strdup_P(PGM_P src)
    {
        size_t length = strlen_P(src);
        char *dst = (char *)malloc(length + 1);
        dst[length] = 0;
        strncpy_P(dst, src, length);
        return dst;
    }
};

WIFIManagerClass WIFIMANAGER;
#endif