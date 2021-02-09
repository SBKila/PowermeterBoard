#ifndef PMBOARD_H
#define PMBOARD_H

#pragma once

#include "ESPAsyncWebServer.h"
#include "HALib/HALib.h"
#include "AsyncJson.h"
#include "ArduinoJson.h"

#include "Powermeter.hpp"

#ifdef DEBUG_PWBOARD
#ifdef DEBUG_ESP_PORT
#define PWBOARD_DEBUG_MSG(...)          \
    DEBUG_ESP_PORT.print("[PWBOARD] "); \
    DEBUG_ESP_PORT.printf(__VA_ARGS__)
#else
#define PWBOARD_DEBUG_MSG(...)
#endif
#else
#define PWBOARD_DEBUG_MSG(...)
#endif

struct PowermeterBoardSettings
{
    int tag;
    char node_name[32];
    char ssid_name[32];
    char ssid_key[64];
    char openhab_mqtt_url[128];
    int openhab_mqtt_port = 1883;
    char openhab_mqtt_login[32];
    char openhab_mqtt_pwd[64];
};

String mainHTMLStringProcessor(const String &var)
{
    return "";
}

class PowermeterBoard
{

public:
    PowermeterBoard(){

    };
    ~PowermeterBoard()
    {
        delete (m_pPowerMeterDevice);
    }

    static const uint8_t PM1 = D3;
    static const uint8_t PM2 = D2;
    static const uint8_t PM3 = D1;
    static const uint8_t PM4 = D5;
    static const uint8_t PM5 = D6;
    static const uint8_t PM6 = D7;
    static const uint8_t PM7 = D8;
    static const uint8_t PM8 = D9;
    static const uint8_t PM9 = D10;
    static const uint8_t PM10 = D4;
    static const uint8_t PMB_LED = D0;

    void setupPersistance()
    {
        // manage Persistance tag
        m_TagPersistanceIndex = EEPROMEX.allocate(sizeof(int));
        // manage PowerMeter Persistance
        m_DefinitionPersistanceIndex = EEPROMEX.allocate(sizeof(PowermeterDef[10]));
        // manage PowerMeter Data Persistance
        m_DataPersistanceIndex = EEPROMEX.allocate(sizeof(DDS238Data[10]));

    }
    void setup(const char *deviceName, Client &pEthClient, const char *openHabMqttUrl, const int openHabMqttport, AsyncWebServer *p_pWiFiServer, fs::FS fs)
    {
        PWBOARD_DEBUG_MSG("setup powermetersBoard\n");

        m_pPowerMeterDevice = new HALIB_NAMESPACE::HADevice(deviceName, "Kila Product", "PowerMeter", "v 0.1");
        m_pPowerMeterDevice->setup(pEthClient, openHabMqttUrl, openHabMqttport);

        //Route for root / web page
        p_pWiFiServer->on(
            "/pmb/index.htm",
            HTTP_GET,
            [this, &fs](AsyncWebServerRequest *request) {
                PWBOARD_DEBUG_MSG("GET /pmb\n");
                //FS fs = this->getFileSystem();
                //(FS &fs, const String& path, const String& contentType, bool download, AwsTemplateProcessor callback)
                request->send(
                    SPIFFS,
                    //fs,
                    "/pmb/index.htm",
                    "text/html; charset=utf-8",
                    false,
                    [this](const String &var) -> String { return this->mainHTML_StringProcessor(var); });
            });

        // Get list of configured powermeter
        p_pWiFiServer->on(
            "/pmb/pm",
            HTTP_GET,
            [this](AsyncWebServerRequest *request) {
                PWBOARD_DEBUG_MSG("GET /pmb/pm\n");
                AsyncResponseStream *response = request->beginResponseStream("application/json");
                DynamicJsonDocument doc(512);

                for (int i = 0; i < 10; i++)
                {
                    Powermeter *pPowerMeter = this->m_Powermeters[i];
                   
                    if (NULL != pPowerMeter)
                    {
                        JsonObject obj = doc.createNestedObject();
                        obj["dIO"] = pPowerMeter->getDefinition().dIO;
                        obj["name"] = pPowerMeter->getDefinition().name;
                        obj["nbTickByKW"] = pPowerMeter->getDefinition().nbTickByKW;
                        obj["voltage"] = pPowerMeter->getDefinition().voltage;
                        obj["maxAmp"] = pPowerMeter->getDefinition().maxAmp;
                    }
                }
                serializeJson(doc, *response);
                request->send(response);
            },
            NULL,
            NULL);

        AsyncCallbackJsonWebHandler *handler = new AsyncCallbackJsonWebHandler("/pmb/pm", [this](AsyncWebServerRequest *request, JsonVariant &json) {
            PWBOARD_DEBUG_MSG("POST /pmb/pm\n");
            const JsonObject &jsonObj = json.as<JsonObject>();

            PowermeterDef powermeterDef;
            strncpy(powermeterDef.name, jsonObj["pm_name"], 25);
            powermeterDef.maxAmp = jsonObj["pm_maxamp"];
            powermeterDef.nbTickByKW = jsonObj["pm_nbtick"];
            powermeterDef.voltage = jsonObj["pm_voltage"];
            powermeterDef.dIO = jsonObj["pm_ref"];

            if (_addPowermeters(powermeterDef))
            {
                backup();
                StaticJsonDocument<200> data;
                String response;
                serializeJson(jsonObj, response);
                request->send(200, "application/json", response);
            }
            else
            {
                request->send(400);
            }
        });
        handler->setMethod(HTTP_POST);
        p_pWiFiServer->addHandler(handler);

        // p_pWiFiServer->on(
        //     "/pmb/pm",
        //     HTTP_POST,
        //     [this](AsyncWebServerRequest *request) {
        //         PWBOARD_DEBUG_MSG("POST /pmb/pm\n");
        //         request->send(200);
        //     });

        p_pWiFiServer->serveStatic("/pmb/", fs, "/pmb/");

        restore();
    };
    void restore()
    {
        PWBOARD_DEBUG_MSG("Restore\n");

        // restore pmb settings
        int tag;
        EEPROMEX.get(m_TagPersistanceIndex, tag);
        if(923!=tag){
            backup();
        }

        // restore pmb settings
        EEPROMEX.get(m_SettingsPersistanceIndex, m_PowermeterBoardSettings);

        // restore powermeter data
        EEPROMEX.get(m_DataPersistanceIndex, m_PowermeterDatasPersistance);

        // restore powermeters
        PowermeterDef storedPowerMeterDefinitions[10];
        EEPROMEX.get(m_DefinitionPersistanceIndex, storedPowerMeterDefinitions);

        PWBOARD_DEBUG_MSG("Restore powermeters\n");
        // for each powermeters
        for (int i = 0; i < 10; i++)
        {
            //if powermeter is defined
            if (storedPowerMeterDefinitions[i].dIO != 255)
            {
                _addPowermeters(storedPowerMeterDefinitions[i]);

            }
        }
    };
    void backup()
    {
        PWBOARD_DEBUG_MSG("Backup\n");
       

        // store powermeter data
        EEPROMEX.put(m_DataPersistanceIndex, m_PowermeterDatasPersistance);

        // store powermeters
        PowermeterDef storedPowerMeterDefinitions[10];
        for (int i = 0; i < 10; i++)
        {
            PWBOARD_DEBUG_MSG("Backup at %d\n",i);
            if (m_Powermeters[i] != NULL)
            {
                _printPowermeterDef(m_Powermeters[i]->getDefinition());
                storedPowerMeterDefinitions[i] = m_Powermeters[i]->getDefinition();
            } 
        }

        EEPROMEX.put(m_DefinitionPersistanceIndex, storedPowerMeterDefinitions);

        // store powermeter persistance tag
        EEPROMEX.put(m_TagPersistanceIndex, 923);

        EEPROMEX.commit();
    }

    void loop()
    {
        for (int index = 0; index < 10; index++)
        {
            if (m_Powermeters[index] != NULL)
            {
                m_Powermeters[index]->loop();
            }
        }
    };

    void suspend(boolean suspend)
    {
        PWBOARD_DEBUG_MSG("suspend %s\n", (suspend) ? "true" : "false");
    }; //@TODO

    String mainHTML_StringProcessor(const String &variable)
    {
        PWBOARD_DEBUG_MSG("mainHTML_StringProcessor %s\n", &variable);
        if (variable == "NODENAME")
            return m_PowermeterBoardSettings.node_name;
        if (variable == "SSIDNAME")
            return String(m_PowermeterBoardSettings.ssid_name);
        if (variable == "MQTTURL")
            return String(m_PowermeterBoardSettings.openhab_mqtt_url);
        if (variable == "MQTTPORT")
            return String(m_PowermeterBoardSettings.openhab_mqtt_port);
        if (variable == "MQTTLOGIN")
            return String(m_PowermeterBoardSettings.openhab_mqtt_login);
        return "";
    };

private:
    const uint8_t PowermeterBoardIO[10] = {PM1, PM2, PM3, PM4, PM5, PM6, PM7, PM8, PM9, PM10};
    void _printPowermeterDef(PowermeterDef powermeterDef){ 
        PWBOARD_DEBUG_MSG("pm dIO %d\n", powermeterDef.dIO);
        PWBOARD_DEBUG_MSG("pm name %s\n", powermeterDef.name);
        PWBOARD_DEBUG_MSG("pm nbTickByKW %d\n", powermeterDef.nbTickByKW);
        PWBOARD_DEBUG_MSG("pm voltage %d\n", powermeterDef.voltage);
        PWBOARD_DEBUG_MSG("pm maxAmp %d\n", powermeterDef.maxAmp);
        }
    boolean _addPowermeters(PowermeterDef powermeterDef)
    {
        _printPowermeterDef(powermeterDef);

        int powermeterIndex = powermeterDef.dIO - 1;
        Powermeter *pPowermeter = this->m_Powermeters[powermeterIndex];
        
        // allocate a PowerMeter object
        this->m_Powermeters[powermeterIndex] = new Powermeter(
            powermeterDef,
            m_pPowerMeterDevice,
            // create lambda as persistance callback
            [this, powermeterIndex](DDS238Data data) {
                this->m_PowermeterDatasPersistance[powermeterIndex] = data;
            }
        );
        
        if (NULL == pPowermeter)
        {
            PWBOARD_DEBUG_MSG("add new powermeter %d\n", powermeterDef.dIO);
            return true;
        }
        else
        {
            PWBOARD_DEBUG_MSG("replace powermeter %d\n", powermeterDef.dIO);
            delete (pPowermeter);
            return true;
        }

        return false;
    }
    // int addPM(JsonObject Name);
    // String getPM(int index);
    // void removePM(int index);
    // void updatePM(String jsonConfig);
    //void updatePersistence();

    PowermeterBoardSettings m_PowermeterBoardSettings;

    HADevice *m_pPowerMeterDevice;
    Powermeter *m_Powermeters[10];
    DDS238Data m_PowermeterDatasPersistance[10];
    // FS &m_FileSystem;

    int m_TagPersistanceIndex;
    int m_SettingsPersistanceIndex;
    int m_DataPersistanceIndex;
    int m_DefinitionPersistanceIndex;
};

PowermeterBoard POWERMETERBOARD;

#endif