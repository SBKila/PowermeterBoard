#ifndef WIFIMGR_H
#define WIFIMGR_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <SPIFFSEditor.h>
#include <ArduinoJson.h>

#pragma once

#ifdef DEBUG_WIFIMGR  
    #ifdef DEBUG_ESP_PORT
        #define WIFIMGR_DEBUG_MSG(...) DEBUG_ESP_PORT.print("[WIFIMGR] ");DEBUG_ESP_PORT.printf(__VA_ARGS__)
    #else
        #define WIFIMGR_DEBUG_MSG(...)
    #endif
#else
    #define WIFIMGR_DEBUG_MSG(...)
#endif




#define MAGICWIFIMGR 932

struct WIFIsettings
{
    int tag;
    char ssid_name[32];
    char ssid_key[64];
};

class WIFIManagerClass
{
public:
    WIFIManagerClass() : m_jsonDoc(1 + ((1 + 32 + 1 + 1) * 5) + 1)
    {
    }

    void setupPersistance()
    {
        // manage WFIF setting
        m_SettingsPersistanceIndex = EEPROMEX.allocate(sizeof(struct WIFIsettings));
    }

    void setup(AsyncWebServer *p_pWiFiServer, fs::FS fs)
    {
        EEPROMEX.get(m_SettingsPersistanceIndex, m_SettingsData);
        isSettingsDirty = false;
        WIFIMGR_DEBUG_MSG("%settings available",(m_SettingsData.tag != MAGICWIFIMGR) ? "No s" : "S");

        WiFi.mode((m_SettingsData.tag != MAGICWIFIMGR) ? WIFI_AP : WIFI_AP_STA);

        /**************/
        /* AP section */
        /**************/
        WiFi.disconnect();
        // setup Access Point for 1 connection
        boolean result = WiFi.softAP("PowermetersBoard", "password", 8, false, 1);
        WIFIMGR_DEBUG_MSG("Setup AP: %s\n",result?"true":"false");

        IPAddress IP = WiFi.softAPIP();
        WIFIMGR_DEBUG_MSG("AP IP address: %s\n",IP.toString().c_str());

        p_pWiFiServer->on("/wifi/scan", HTTP_GET, [&](AsyncWebServerRequest *request) {
            WIFIMGR_DEBUG_MSG("GET /wifi/scan\n");
            AsyncResponseStream *response = request->beginResponseStream("application/json");
            serializeJson(this->m_jsonDoc, *response);
            request->send(response);
        });

        AsyncCallbackJsonWebHandler *handler = new AsyncCallbackJsonWebHandler("/wifi", [&](AsyncWebServerRequest *request, JsonVariant &json) {
            const JsonObject &jsonObj = json.as<JsonObject>();
            WIFIMGR_DEBUG_MSG("POST /wifi (%s,%s)\n",(const char *)(jsonObj["ssid-name"]),(const char *)(jsonObj["ssid-pwd"]));
            strncpy(this->m_SettingsData.ssid_name, (const char *)(jsonObj["ssid-name"]), 32);
            strncpy(this->m_SettingsData.ssid_key, (const char *)(jsonObj["ssid-pwd"]), 64);
            this->m_SettingsData.tag = MAGICWIFIMGR;
            this->isSettingsDirty = true;
        });
        handler->setMethod(HTTP_POST);
        p_pWiFiServer->addHandler(handler);

        p_pWiFiServer->serveStatic("/wifi", fs, "/wifi").setTemplateProcessor([this](const String &var) -> String { return this->setupTemplateProcessor(var); }).setFilter(ON_AP_FILTER);

        // start scanning available wifi
        WIFIMGR_DEBUG_MSG("start scanning available wifi\n");
        WiFi.scanNetworks(true);
    }

    String setupTemplateProcessor(const String &var)
    {
        if (isSettingExist() && (var == "SSIDNAME"))
            return m_SettingsData.ssid_name;

        if (var == "WIFICONNECTIONSTATUS")
            return (WiFi.status() != WL_CONNECTED) ? "Not connected" : "Connected";

        return String();
    }

    void loop()
    {
        int found = WiFi.scanComplete();
        if (found > 0)
        {
            WIFIMGR_DEBUG_MSG("%d available wifi\n",found);
            // JsonArray root = m_jsonDoc.to<JsonArray>();

            // DynamicJsonDocument doc(200);
            // for (int i = 0; i < found && i < 5; i++)
            // {
            //       JsonObject nested = root.createNestedObject();
            //       nested["ssid"] = WiFi.SSID(i);
            //       nested["rssi"]=WiFi.RSSI(i);
            //   Serial.printf("%d: %s, Ch:%d (%ddBm) %s\n", i + 1, WiFi.SSID(i).c_str(), WiFi.channel(i), WiFi.RSSI(i), WiFi.encryptionType(i) == ENC_TYPE_NONE ? "open" : "");
            // }

            JsonArray root = m_jsonDoc.to<JsonArray>();
            for (int i = 0; i < found && i < 5; i++)
            {
                root.add(WiFi.SSID(i));
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

        if (isSettingsDirty)
        {
            EEPROMEX.put(m_SettingsPersistanceIndex, m_SettingsData);
            isSettingsDirty = false;
        }
    }

    boolean isSettingExist()
    {
        return m_SettingsData.tag == MAGICWIFIMGR;
    }

protected:
    int m_SettingsPersistanceIndex = 0;
    boolean isSettingsDirty = false;
    WIFIsettings m_SettingsData;
    DynamicJsonDocument m_jsonDoc;
};

WIFIManagerClass WIFIMANAGER;
#endif