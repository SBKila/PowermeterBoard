#ifndef WIFIMGR_H
#define WIFIMGR_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <SPIFFSEditor.h>
#include <ArduinoJson.h>
#include <EEPROMEX.h>

#pragma once

#ifdef DEBUG_WIFIMGR
#ifdef DEBUG_ESP_PORT
#define WIFIMGR_DEBUG_MSG(...)          \
    DEBUG_ESP_PORT.print("[WIFIMGR] "); \
    DEBUG_ESP_PORT.printf(__VA_ARGS__)
#else
#define WIFIMGR_DEBUG_MSG(...)
#endif
#else
#define WIFIMGR_DEBUG_MSG(...)
#endif

#ifdef EEPROMMAGIC
#define MAGICWIFIMGR EEPROMMAGIC
#else
#define MAGICWIFIMGR 93
#endif

struct WIFIsettings
{
    int tag;
    char ssid_name[32];
    char ssid_key[64];
};

class WIFIManagerClass
{
public:
    WIFIManagerClass() : m_jsonDoc(1 + ((1 + 32 + 1 + 1) * 5) + 1) // buffer to return array of ssid ["ssid",] (1+((1+32+1+1)*5)+1)
    {
    }

    void setupPersistance()
    {
        // manage WFIF setting
        m_SettingsPersistanceIndex = EEPROMEX.allocate(sizeof(struct WIFIsettings));
    }

    void setupAsAccessPoint(AsyncWebServer *p_pWiFiServer, fs::FS fs)
    {
        WIFIMGR_DEBUG_MSG("setupAsAccessPoint\n");

        /**************/
        /* AP section */
        /**************/
        WiFi.disconnect(true);
        delay(200);
        WiFi.mode(WIFI_AP);

        p_pWiFiServer->on("/", HTTP_ANY, [](AsyncWebServerRequest *request)
                          { request->redirect("/wifi/setup.html"); });

        // setup Access Point for 1 connection
        boolean result = WiFi.softAP("PowermetersBoard", "password", 8, false, 1);
        WIFIMGR_DEBUG_MSG("Setup AP: %s\n", result ? "true" : "false");
        WIFIMGR_DEBUG_MSG("AP IP address: %s\n", WiFi.softAPIP().toString().c_str());
        p_pWiFiServer->on("/wifi/scan", HTTP_GET, [&](AsyncWebServerRequest *request)
                          {
            WIFIMGR_DEBUG_MSG("GET /wifi/scan\n");
            AsyncResponseStream *response = request->beginResponseStream("application/json");
            serializeJson(this->m_jsonDoc, *response);
            request->send(response); });
        AsyncCallbackJsonWebHandler *handler = new AsyncCallbackJsonWebHandler("/wifi", [&](AsyncWebServerRequest *request, JsonVariant &json)
                                                                               {
                                                                                    const JsonObject &jsonObj = json.as<JsonObject>();
                                                                                    // WIFIMGR_DEBUG_MSG("POST /wifi (%s,%s)\n", (const char *)(jsonObj["ssid-name"]), (const char *)(jsonObj["ssid-pwd"]));

                                                                                    // p_pWiFiServer->removeHandler();
                                                                                    // p_pWiFiServer->removeHandler();

                                                                                    strncpy(this->m_SettingsData.ssid_name, (const char *)(jsonObj["ssid-name"]), 32);
                                                                                    strncpy(this->m_SettingsData.ssid_key, (const char *)(jsonObj["ssid-pwd"]), 64);
                                                                                    this->isSettingsDirty = true;
                                                                                    switching=6;
                                                                                    request->send(200); });
        handler->setMethod(HTTP_POST);
        p_pWiFiServer->addHandler(handler);

        // start scanning available wifi
        WIFIMGR_DEBUG_MSG("start scanning available wifi\n");
        WiFi.scanNetworks(true);
    }
    void setupAsStationMode(AsyncWebServer *p_pWiFiServer, fs::FS fs)
    {
        WIFIMGR_DEBUG_MSG("setupAsStationMode\n");
        /***************/
        /* STA section */
        /***************/
        WiFi.mode(WIFI_STA);
        WiFi.disconnect(true);
        WiFi.hostname("powermetersboard");
        WIFIMGR_DEBUG_MSG("Connecting WIFI %s:%s\n", m_SettingsData.ssid_name, m_SettingsData.ssid_key);
        WiFi.begin(m_SettingsData.ssid_name, m_SettingsData.ssid_key);
        m_connectingStartTime = millis();
    }

    void setup(AsyncWebServer *p_pWiFiServer, fs::FS fs)
    {
        EEPROMEX.get(m_SettingsPersistanceIndex, m_SettingsData);
        isSettingsDirty = false;
        WIFIMGR_DEBUG_MSG("%settings available", isSettingExist() ?  "S" : "No s");

        WiFi.mode(!isSettingExist() ? WIFI_AP : WIFI_AP_STA);

        /**************/
        /* AP section */
        /**************/
        WiFi.disconnect();
        
        // setup Access Point for 1 connection
        boolean result = WiFi.softAP("PowermetersBoard", "password", 8, false, 1);
        WIFIMGR_DEBUG_MSG("Setup AP: %s\n", result ? "true" : "false");

        IPAddress IP = WiFi.softAPIP();
        WIFIMGR_DEBUG_MSG("AP IP address: %s\n", IP.toString().c_str());

        
        p_pWiFiServer->on("/", HTTP_ANY, [](AsyncWebServerRequest *request)
                    { request->redirect("/wifi/setup.html"); }).setFilter(ON_AP_FILTER);
        
        p_pWiFiServer->on("/wifi/scan", HTTP_GET, [&](AsyncWebServerRequest *request)
                          {
            WIFIMGR_DEBUG_MSG("GET /wifi/scan\n");
            AsyncResponseStream *response = request->beginResponseStream("application/json");
            serializeJson(this->m_jsonDoc, *response);
            request->send(response); });

        AsyncCallbackJsonWebHandler *handler = new AsyncCallbackJsonWebHandler("/wifi", [&](AsyncWebServerRequest *request, JsonVariant &json)
                                                                               {
            const JsonObject &jsonObj = json.as<JsonObject>();
            WIFIMGR_DEBUG_MSG("POST /wifi (%s,%s)\n",(const char *)(jsonObj["ssid-name"]),(const char *)(jsonObj["ssid-pwd"]));
            strncpy(this->m_SettingsData.ssid_name, (const char *)(jsonObj["ssid-name"]), 32);
            strncpy(this->m_SettingsData.ssid_key, (const char *)(jsonObj["ssid-pwd"]), 64);
            this->m_SettingsData.tag = MAGICWIFIMGR;
            this->isSettingsDirty = true;
            switching=7;
            request->send(200); });
        handler->setMethod(HTTP_POST);
        p_pWiFiServer->addHandler(handler);
        
        p_pWiFiServer->serveStatic("/wifi", fs, "/wifi").setTemplateProcessor([this](const String &var) -> String
                                                                              { return this->stringProcessor(var); })
            .setFilter(ON_AP_FILTER);

        // start scanning available wifi
        WIFIMGR_DEBUG_MSG("start scanning available wifi\n");
        WiFi.scanNetworks(true);
    }

    // void setupNext(AsyncWebServer *p_pWiFiServer, fs::FS fs)
    // {
    //     m_pfs = &fs;
    //     m_pWiFiServer = p_pWiFiServer;
// 
    //     WiFi.persistent(false);
// 
    //     EEPROMEX.get(m_SettingsPersistanceIndex, m_SettingsData);
    //     isSettingsDirty = false;
    //     WIFIMGR_DEBUG_MSG("%settings available\n", isSettingExist() ? "S" : "No s");
// 
    //     if (!isSettingExist())
    //     {
    //         setupAsAccessPoint(p_pWiFiServer, fs);
    //     }
    //     else
    //     {
    //         setupAsStationMode(p_pWiFiServer, fs);
    //     }
    //     p_pWiFiServer->serveStatic("/wifi", fs, "/wifi").setTemplateProcessor([this](const String &var) -> String
    //                                                                           { return this->stringProcessor(var); })
    //         .setFilter(ON_AP_FILTER);
    // }

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
    int connectingError = 0;
    short switching = 0;
    long startReboot = 0;
    WiFiMode prevMode = WiFi.getMode();
    void loop()
    {
        if (prevMode != WiFi.getMode())
        {
            WIFIMGR_DEBUG_MSG("wifi mode change %d->%d\n", prevMode, WiFi.getMode());
            prevMode = WiFi.getMode();
        }

        if (switching == 3)
        {
            DEBUG_ESP_PORT.printf(".");
            
            if(startReboot==0) startReboot = millis();
            if((millis()-startReboot) > 10000) ESP.restart();
        }
        if (switching == 1)
        {
            setupAsAccessPoint(m_pWiFiServer, *m_pfs);
            switching = 0;
        }
        if (switching == 2)
        {
            setupAsStationMode(m_pWiFiServer, *m_pfs);
            switching = 0;
        }
        if (switching > 3)
        {
            WiFi.softAPdisconnect(true);
            WiFi.disconnect(true);
            delay(100);
            WiFi.mode(WIFI_OFF);
            delay(200);
            switching &= 3;
        }

        if ((WiFi.getMode() & WIFI_AP) == WIFI_AP)
        {
            int found = WiFi.scanComplete();
            if (found > 0)
            {
                WIFIMGR_DEBUG_MSG("%d available wifi\n", found);
                JsonArray root = m_jsonDoc.to<JsonArray>();
                for (int i = 0; i < found && i < 5; i++)
                {
                    root.add(String(WiFi.SSID(i)) + " ch:" + WiFi.channel(i) + " (" + WiFi.RSSI(i) + ")");
                    WIFIMGR_DEBUG_MSG("%d: %s, Ch:%d (%ddBm) %s\n", i + 1, WiFi.SSID(i).c_str(), WiFi.channel(i), WiFi.RSSI(i), WiFi.encryptionType(i) == ENC_TYPE_NONE ? "open" : "");
                }
                WIFIMGR_DEBUG_MSG("end scanning available wifi\n");
                WiFi.scanDelete();

                /***************/
                /* STA section */
                /***************/
                if (isSettingExist())
                {
                    WiFi.hostname("powermetersboard");
                    WIFIMGR_DEBUG_MSG("Connecting WIFI %s\n",m_SettingsData.ssid_name);
                    WiFi.begin(m_SettingsData.ssid_name, m_SettingsData.ssid_key);
                }
            }
            // if (m_setupDone)
            // {
            //     if (WiFi.getMode() != WIFI_OFF)
            //     {
            //     }
            //     else
            //     {
            //         setupAsStationMode(m_pWiFiServer, *m_pfs);
            //     }
            // }
        }
        if ((WiFi.getMode() & WIFI_STA) == WIFI_STA)
        {
            // if ((WiFi.status() != WL_CONNECTED) && ((millis() - m_connectingStartTime) > 30000))
            // {
            //     WIFIMGR_DEBUG_MSG("not able to connect wifi fallback to Access Point mode\n");
            //     switching = 5;
            // }
        }

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
        return (m_SettingsData.tag == MAGICWIFIMGR);
    }

protected:
    int m_SettingsPersistanceIndex = 0;
    boolean isSettingsDirty = false;
    WIFIsettings m_SettingsData;
    DynamicJsonDocument m_jsonDoc;
    unsigned long m_connectingStartTime = 0;
    AsyncWebServer *m_pWiFiServer = NULL;
    fs::FS *m_pfs = NULL;
    // boolean m_setupDone = false;
};

WIFIManagerClass WIFIMANAGER;
#endif