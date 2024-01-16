#ifndef PMBOARD_H
#define PMBOARD_H

#pragma once
#include "EEPROMEX.h"
#include "ESPAsyncWebServer.h"
#include "HALib/HALib.h"
#include "AsyncJson.h"
#include "ArduinoJson.h"
#include "Powermeter.hpp"


#ifdef DEBUG_PWBOARD
#define PWBOARD_DEBUG_MSG(...) DEBUG_MSG("PWBOARD", __VA_ARGS__)
#else
#define PWBOARD_DEBUG_MSG(...)
#endif


#ifdef EEPROMMAGIC
#define PMBMAGIC EEPROMMAGIC
#else
#define PMBMAGIC 928
#endif

AsyncWebSocket ws("/pmb/ws"); // access at ws://[esp ip]/pmb/ws
// AsyncEventSource events("pmb/events"); // event source (Server-Sent events)

struct PowermeterBoardSettings
{
    int tag;
    char node_name[32];
    char ssid_name[32];
    char ssid_key[64];
    char mqtt_domain[128];
    int mqtt_port = 1883;
    char mqtt_login[32];
    char mqtt_pwd[64];
};
class PowermeterBoard
{
    Client *m_pEthClient;

public:
    PowermeterBoard()
    {
        memset(m_Powermeters, 0, sizeof(m_Powermeters));
    }
    ~PowermeterBoard()
    {
        delete (m_pPowerMeterDevice);
    }

    void setupPersistance()
    {
        // manage Persistance tag
        m_TagPersistanceIndex = EEPROMEX.allocate(sizeof(int));
        // manage PowerMeter Persistance
        m_DefinitionPersistanceIndex = EEPROMEX.allocate(sizeof(PowermeterDef[10]));
        // manage PowerMeter Data Persistance
        m_DataPersistanceIndex = EEPROMEX.allocate(sizeof(DDS238Data[10]));
        // manage PowerMeter setting
        m_SettingsPersistanceIndex = EEPROMEX.allocate(sizeof(PowermeterBoardSettings));
    }

    void setupUpdatePowerMeterHandler(const char *deviceName, Client &pEthClient, AsyncWebServer *p_pWebServer, fs::FS fs)
    {
        AsyncCallbackJsonWebHandler *handler = new AsyncCallbackJsonWebHandler(
            "/pmb/pm",
            [this](AsyncWebServerRequest *request, JsonVariant &json)
            {
                PWBOARD_DEBUG_MSG("POST /pmb/pm\n");
                const JsonObject &pmDefinition = json.as<JsonObject>();

                uint8 powermeterIndex = (int)pmDefinition["pmref"] - 1;

                PowermeterDef powermeterDef;
                strncpy(powermeterDef.name, pmDefinition["name"], 25);
                powermeterDef.maxAmp = pmDefinition["maxAmp"];
                powermeterDef.nbTickByKW = pmDefinition["nbTickByKW"];
                powermeterDef.voltage = pmDefinition["voltage"];
                powermeterDef.dIO = PowermeterIndexToBoardIO[powermeterIndex];
                DDS238Data powermeterValues;
                powermeterValues.tag = m_PowermeterDatasPersistance[powermeterIndex].tag;
                powermeterValues.ticks = 0;
                powermeterValues.cumulative = pmDefinition["cumulative"];

                if (_removePowermeter(powermeterIndex))
                {
                    // update data storage
                    m_PowermeterDatasPersistance[powermeterIndex].tag = powermeterValues.tag;
                    m_PowermeterDatasPersistance[powermeterIndex].ticks = powermeterValues.ticks;
                    m_PowermeterDatasPersistance[powermeterIndex].cumulative = powermeterValues.cumulative;

                    if (_addPowermeter(powermeterDef, powermeterValues))
                    {
                        request->send(200, "application/json", _getPowermeterAsJsonString(powermeterDef));
                    }
                }
                request->send(400);
            });
        handler->setMethod(HTTP_POST);
        p_pWebServer->addHandler(handler);
    };
    void setupAddPowerMeterHandler(const char *deviceName, Client &pEthClient, AsyncWebServer *p_pWebServer, fs::FS fs)
    {
        AsyncCallbackJsonWebHandler *handler = new AsyncCallbackJsonWebHandler(
            "/pmb/pm",
            [this](AsyncWebServerRequest *request, JsonVariant &json)
            {
                PWBOARD_DEBUG_MSG("PUT /pmb/pm\n");

                if (json.is<JsonArray>())
                {
                    PWBOARD_DEBUG_MSG("data is array\n");
                    const JsonArray &pmDefinitions = json.as<JsonArray>();

                    int index = 0;
                    PowermeterDef storedPowerMeterDefinitions[10];
                    // DDS238Data powermeterDatasPersistance[10];

                    for (JsonObject pmDefinition : pmDefinitions)
                    {
                        PWBOARD_DEBUG_MSG("index %d\n", index);
                        PWBOARD_DEBUG_MSG("=> ticks %d d\n", (int)pmDefinition["ticks"]);
                        PWBOARD_DEBUG_MSG("=> cumulative %f f\n", (float)pmDefinition["cumulative"]);
                        PWBOARD_DEBUG_MSG("=> nbTickByKW %d d\n", (int)pmDefinition["nbTickByKW"]);

                        strncpy(storedPowerMeterDefinitions[index].name, pmDefinition["name"], 25);
                        storedPowerMeterDefinitions[index].maxAmp = pmDefinition["maxAmp"];
                        storedPowerMeterDefinitions[index].nbTickByKW = pmDefinition["nbTickByKW"];
                        storedPowerMeterDefinitions[index].voltage = pmDefinition["voltage"];
                        storedPowerMeterDefinitions[index].dIO = pmDefinition["dIO"];
                        PowermeterDef newDef;
                        strncpy(newDef.name, pmDefinition["name"], 25);
                        newDef.maxAmp = pmDefinition["maxAmp"];
                        newDef.nbTickByKW = pmDefinition["nbTickByKW"];
                        newDef.voltage = pmDefinition["voltage"];
                        newDef.dIO = pmDefinition["dIO"];
                        // PowermeterDef curDef = m_Powermeters[index]->getDefinition();
                        DDS238Data powerMeterData;
                        powerMeterData.ticks = 0;
                        powerMeterData.cumulative = pmDefinition["cumulative"];
                        _updatePowermeters(newDef, powerMeterData);

                        // powermeterDatasPersistance[index].tag = 963;
                        // powermeterDatasPersistance[index].ticks = 0; // pmDefinition["ticks"];
                        // powermeterDatasPersistance[index].cumulative = pmDefinition["cumulative"];
                        // m_PowermeterDatasPersistance[index].tag = 963;
                        // m_PowermeterDatasPersistance[index].ticks = 0; // pmDefinition["ticks"];
                        // m_PowermeterDatasPersistance[index].cumulative = pmDefinition["cumulative"];

                        // _printPowermeterDef(storedPowerMeterDefinitions[index]);
                        // _printPowermeterData(powermeterDatasPersistance[index]);
                        // _printPowermeterData(m_PowermeterDatasPersistance[index]);
                        index++;
                    }
                    EEPROMEX.put(m_DefinitionPersistanceIndex, storedPowerMeterDefinitions);
                    EEPROMEX.put(m_DataPersistanceIndex, m_PowermeterDatasPersistance);
                    // EEPROMEX.put(m_DataPersistanceIndex, powermeterDatasPersistance);
                    isPersistanceDirty = true;
                    // noInterrupts();
                    // EEPROMEX.commit();
                    // interrupts();
                    request->send(200);
                    forceRestart = 2000;
                }
                else if (json.is<JsonObject>())
                {
                    PWBOARD_DEBUG_MSG("data is object\n");
                    const JsonObject &jsonObj = json.as<JsonObject>();
                    PWBOARD_DEBUG_MSG("1\n");
                    PowermeterDef powermeterDef;
                    strncpy(powermeterDef.name, jsonObj["name"], 25);
                    PWBOARD_DEBUG_MSG("2\n");
                    powermeterDef.maxAmp = jsonObj["maxAmp"];
                    powermeterDef.nbTickByKW = jsonObj["nbTickByKW"];
                    powermeterDef.voltage = jsonObj["voltage"];
                    powermeterDef.dIO = (int)jsonObj["dIO"];
                    PWBOARD_DEBUG_MSG("3\n");
                    DDS238Data powermeterValues;
                    powermeterValues.ticks = 0;
                    powermeterValues.cumulative = jsonObj["cumulative"];

                    if (_addPowermeter(powermeterDef, powermeterValues))
                    {
                        request->send(200, "application/json", _getPowermeterAsJsonString(powermeterDef));
                    }
                    else
                    {
                        request->send(400);
                    }
                }
                else
                {
                    PWBOARD_DEBUG_MSG("data is something else\n");
                    request->send(400);
                }
            });
        handler->setMethod(HTTP_PUT);
        p_pWebServer->addHandler(handler);
    }
    void setupSetListPMsHandler(const char *deviceName, Client &pEthClient, AsyncWebServer *p_pWebServer, fs::FS fs)
    {
        p_pWebServer->on(
            "/pmb/pm",
            HTTP_GET,
            [this](AsyncWebServerRequest *request)
            {
                PWBOARD_DEBUG_MSG("GET /pmb/pm\n");
                AsyncResponseStream *response = request->beginResponseStream("application/json");
#ifdef ARDUINOJSON_6_COMPATIBILITY
                DynamicJsonDocument doc(1024);
            // create an object
//                JsonObject obj = doc.to<JsonObject>();
#else
                JsonDocument doc;
//                JsonObject obj = doc.to<JsonObject>();
#endif
                boolean atleastone = false;
                for (int i = 0; i < 10; i++)
                {
                    Powermeter *pPowerMeter = this->m_Powermeters[i];

                    // PWBOARD_DEBUG_MSG("(%d,%d),", i, (NULL != pPowerMeter) ? pPowerMeter->getDefinition().dIO : -1);
                    if (NULL != pPowerMeter)
                    {
                        atleastone = true;
#ifdef ARDUINOJSON_6_COMPATIBILITY
                        JsonObject obj = doc.createNestedObject();
#else
                        JsonObject obj = doc.add<JsonObject>();
#endif
                        _fillDefinitionToJson(pPowerMeter->getDefinition(), obj);
                        _fillPMDatatoJson(pPowerMeter->getDefinition().dIO, obj);
                    }
                }
                // PWBOARD_DEBUG_MSG("\n");

                if (atleastone)
                {
                    serializeJson(doc, *response);
                }
                else
                {
                    response->println("[]");
                }

                request->send(response);
            },
            NULL,
            NULL);
    }
    void setupSetMqttHandler(const char *deviceName, Client &pEthClient, AsyncWebServer *p_pWebServer, fs::FS fs)
    {

        AsyncCallbackJsonWebHandler *handler = new AsyncCallbackJsonWebHandler("/pmb/mqtt", [this](AsyncWebServerRequest *request, JsonVariant &json)
                                                                               {
            PWBOARD_DEBUG_MSG("POST /pmb/mqtt\n");
            const JsonObject &jsonObj = json.as<JsonObject>();

            strncpy(this->m_PowermeterBoardSettings.mqtt_domain, jsonObj["mqtt_url"],sizeof(this->m_PowermeterBoardSettings.mqtt_domain));
            this->m_PowermeterBoardSettings.mqtt_port = atoi(jsonObj["mqtt_port"]);
            strncpy(this->m_PowermeterBoardSettings.mqtt_login, jsonObj["mqtt_login"],sizeof(this->m_PowermeterBoardSettings.mqtt_login));
            strncpy(this->m_PowermeterBoardSettings.mqtt_pwd, jsonObj["mqtt_pwd"],sizeof(this->m_PowermeterBoardSettings.mqtt_pwd));

            this->m_PowermeterBoardSettings.tag = PMBMAGIC;
            this->isPersistanceDirty = true;
            
            PWBOARD_DEBUG_MSG("POST %s:%d (%s:%s)\n",this->m_PowermeterBoardSettings.mqtt_domain,this->m_PowermeterBoardSettings.mqtt_port, m_PowermeterBoardSettings.mqtt_login, m_PowermeterBoardSettings.mqtt_pwd);

            m_pPowerMeterDevice->setup(*m_pEthClient, this->m_PowermeterBoardSettings.mqtt_domain, this->m_PowermeterBoardSettings.mqtt_port, m_PowermeterBoardSettings.mqtt_login, m_PowermeterBoardSettings.mqtt_pwd);

            request->send(200); });
        handler->setMethod(HTTP_POST);
        p_pWebServer->addHandler(handler);
    }
    void setup(const char *deviceName, Client &pEthClient, AsyncWebServer *p_pWebServer, fs::FS fs)
    {
        PWBOARD_DEBUG_MSG("setup powermetersBoard\n");

        m_pEthClient = &pEthClient;

        // attach set mqtt settings
        setupSetMqttHandler(deviceName, pEthClient, p_pWebServer, fs);

        // Get list of configured powermeter
        setupSetListPMsHandler(deviceName, pEthClient, p_pWebServer, fs);

        setupUpdatePowerMeterHandler(deviceName, pEthClient, p_pWebServer, fs);
        setupAddPowerMeterHandler(deviceName, pEthClient, p_pWebServer, fs);

        m_pPowerMeterDevice = new HALIB_NAMESPACE::HADevice(deviceName, "Kila Product", "PowerMeter", "v 0.1");

        // attach AsyncWebSocket
        ws.onEvent([this](AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len)
                   {
            PWBOARD_DEBUG_MSG("onEvent /pmb/ws\n");
            if (type == WS_EVT_CONNECT)
            {
                PWBOARD_DEBUG_MSG("websocket client connected\n");
                _broadcastPowerMeterInfo(255, client);
                _broadcastMQTTConnectionStatus(client);
                _broadcastWIFIConfig(client);
            }
            else if (type == WS_EVT_DISCONNECT)
            {
                PWBOARD_DEBUG_MSG("Client disconnected\n");
            } });
        p_pWebServer->addHandler(&ws);

        // attach AsyncEventSource
        // p_pWebServer->addHandler(&events);

        p_pWebServer->serveStatic("/pmb/", fs, "/pmb/")
            .setTemplateProcessor([this](const String &var) -> String
                                  { return this->stringProcessor(var); })
            .setDefaultFile("index.htm");

        // m_pRebootComponent = new HAComponent("nb_reboot", ComponentType.Number);

        restore();

        if ((strlen(m_PowermeterBoardSettings.node_name) != 0) && (0 != strcmp(m_PowermeterBoardSettings.node_name, deviceName)))
        {
            strcpy(m_PowermeterBoardSettings.node_name, deviceName);
            isPersistanceDirty = true;
        }

        if (strlen(m_PowermeterBoardSettings.mqtt_domain) != 0)
        {
            m_pPowerMeterDevice->setup(pEthClient, m_PowermeterBoardSettings.mqtt_domain, m_PowermeterBoardSettings.mqtt_port, m_PowermeterBoardSettings.mqtt_login, m_PowermeterBoardSettings.mqtt_pwd);
        };
    };
    void restore()
    {
        PWBOARD_DEBUG_MSG("Restore\n");

        // restore pmb settings
        int tag;
        EEPROMEX.get(m_TagPersistanceIndex, tag);
        if (PMBMAGIC != tag)
        {
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
            // if powermeter is defined
            if (storedPowerMeterDefinitions[i].dIO != 255)
            {
                _addPowermeter(storedPowerMeterDefinitions[i], m_PowermeterDatasPersistance[i]);
            }
        }

        ws.enable(true);
    };
    void backup()
    {
        PWBOARD_DEBUG_MSG("Backup\n");

        // store pmb settings
        EEPROMEX.put(m_SettingsPersistanceIndex, m_PowermeterBoardSettings);

        // store powermeter data
        EEPROMEX.put(m_DataPersistanceIndex, m_PowermeterDatasPersistance);

        // store powermeters
        PowermeterDef storedPowerMeterDefinitions[10];
        for (int i = 0; i < 10; i++)
        {
            if (m_Powermeters[i] != NULL)
            {
                PWBOARD_DEBUG_MSG("Backup at %d\n", i);
                //_printPowermeterDef(m_Powermeters[i]->getDefinition());
                storedPowerMeterDefinitions[i] = m_Powermeters[i]->getDefinition();
            }
        }

        EEPROMEX.put(m_DefinitionPersistanceIndex, storedPowerMeterDefinitions);

        // store powermeter persistance tag
        EEPROMEX.put(m_TagPersistanceIndex, PMBMAGIC);

        //      EEPROMEX.commit();
    }

    int forceRestart = -1;
    boolean forceRestartAfterPersistance = false;
    void loop()
    {
        if (isPersistanceDirty)
        {
            backup();
            isPersistanceDirty = false;
        }

        if (lastWifiStatus != WiFi.status())
        {
            lastWifiStatus = WiFi.status();
            _broadcastWIFIStatus(NULL);
        }

        for (int index = 0; index < 10; index++)
        {
            if (m_Powermeters[index] != NULL)
            {
                m_Powermeters[index]->loop();
            }
        }

        if (m_pPowerMeterDevice)
        {
            m_pPowerMeterDevice->loop(WiFi.status());

            // notify MQTT connection status change
            if (m_pPowerMeterDevice->isMqttconnected() != isMqttconnected)
            {
                isMqttconnected = !isMqttconnected;
                _broadcastMQTTConnectionStatus(NULL);
            }
        }
        if (forceRestartAfterPersistance && !isPersistanceDirty)
        {
            ESP.restart();
        }
        if (forceRestart == 0)
        {
            ESP.restart();
        }
        if (forceRestart > 0)
        {
            forceRestart--;
            // return;
        }
    };

    void suspend(boolean suspend)
    {
        PWBOARD_DEBUG_MSG("suspend %s\n", (suspend) ? "true" : "false");

        // Disable client connections
        ws.enable(false);

        // Advertise connected clients what's going on
        ws.textAll("OTA Update Started");

        // Close them
        ws.closeAll();
    }; //@TODO
    String stringProcessor(const String &variable)
    {
        PWBOARD_DEBUG_MSG("stringProcessor %s\n", variable.c_str());
        if (variable == "NODENAME")
        {
            if (m_pPowerMeterDevice)
                return String(m_pPowerMeterDevice->getName());
            else
                return m_PowermeterBoardSettings.node_name;
        }
        if (variable == "SSIDNAME")
            return String(m_PowermeterBoardSettings.ssid_name);
        if (variable == "MQTTDOMAIN")
            return String(m_PowermeterBoardSettings.mqtt_domain);
        if (variable == "MQTTPORT")
            return String(m_PowermeterBoardSettings.mqtt_port);
        if (variable == "MQTTLOGIN")
            return String(m_PowermeterBoardSettings.mqtt_login);
        if (variable == "MQTTPWD")
            return String(m_PowermeterBoardSettings.mqtt_pwd);
        if (variable == "MQTTCONNECTIONSTATUS")
            return (m_pPowerMeterDevice && m_pPowerMeterDevice->isMqttconnected()) ? "Connected" : "Disconnected";

        return "";
    };
    boolean isMqttConnected() { return isMqttconnected; };

private:
    void _fillPMDatatoJson(int index, JsonObject &obj)
    {
        // PWBOARD_DEBUG_MSG("_fillPMDatatoJson index %d\n", index);
        // PWBOARD_DEBUG_MSG("_fillPMDatatoJson cumul float %f\n", m_PowermeterDatasPersistance[index].cumulative);
        int cumul = (int)(m_PowermeterDatasPersistance[index].cumulative * 10 + 0.5) / 10;
        // PWBOARD_DEBUG_MSG("_fillPMDatatoJson cumul decimal %d\n", cumul);
        obj["cumulative"] = cumul;
        obj["ticks"] = m_PowermeterDatasPersistance[index].ticks;
    }
    void _fillDefinitionToJson(int index, JsonObject &obj)
    {
        if (NULL != m_Powermeters[index])
        {
            _fillDefinitionToJson(m_Powermeters[index]->getDefinition(), obj);
        }
    }
    void _fillDefinitionToJson(PowermeterDef powermeterDef, JsonObject &obj)
    {
        // allocate the memory for the document
        obj["dIO"] = powermeterDef.dIO;
        obj["name"] = powermeterDef.name;
        obj["nbTickByKW"] = powermeterDef.nbTickByKW;
        obj["voltage"] = powermeterDef.voltage;
        obj["maxAmp"] = powermeterDef.maxAmp;
    }
    String _getPowermeterAsJsonString(PowermeterDef powermeterDef)
    {
        String response;

#ifdef ARDUINOJSON_6_COMPATIBILITY
        const size_t CAPACITY = JSON_OBJECT_SIZE(7);
        StaticJsonDocument<CAPACITY> doc;
        // create an object
        JsonObject obj = doc.to<JsonObject>();
#else
        JsonDocument doc;
        JsonObject obj = doc.to<JsonObject>();
#endif
        _fillDefinitionToJson(powermeterDef, obj);
        _fillPMDatatoJson(powermeterDef.dIO, obj);
        serializeJson(obj, response);
        return response;
    }

    void _printPowermeterDef(PowermeterDef powermeterDef)
    {
        PWBOARD_DEBUG_MSG("pm dIO %d\n", powermeterDef.dIO);
        PWBOARD_DEBUG_MSG("pm name %s\n", powermeterDef.name);
        PWBOARD_DEBUG_MSG("pm nbTickByKW %d\n", powermeterDef.nbTickByKW);
        PWBOARD_DEBUG_MSG("pm voltage %d\n", powermeterDef.voltage);
        PWBOARD_DEBUG_MSG("pm maxAmp %d\n", powermeterDef.maxAmp);
    }
    void _printPowermeterData(DDS238Data powermeterData)
    {
        PWBOARD_DEBUG_MSG("pm ticks %d\n", powermeterData.ticks);
        PWBOARD_DEBUG_MSG("pm cumulative %f\n", powermeterData.cumulative);
    }
    boolean _updatePowermeters(PowermeterDef powermeterDef, DDS238Data powermeterValues)
    {
        PWBOARD_DEBUG_MSG("_updatePowermeters\n");
        // uint32_t freeBefore = ESP.getFreeHeap();
        // PWBOARD_DEBUG_MSG(" free heap %d\n", freeBefore);
        _printPowermeterDef(powermeterDef);
        uint8 powermeterIndex = BoardIOToPowermeterIndex[powermeterDef.dIO];
        PWBOARD_DEBUG_MSG("powermeterIndex %d\n", powermeterIndex);
        Powermeter *pPowermeter = this->m_Powermeters[powermeterIndex];
        PWBOARD_DEBUG_MSG("powermeterIndex %sFound\n", (NULL == pPowermeter) ? "Not " : "");
        if (NULL != pPowermeter)
        {
            // mark new data with previous
            powermeterValues.tag = m_PowermeterDatasPersistance[powermeterIndex].tag;

            _removePowermeter(powermeterIndex);
            _addPowermeter(powermeterDef, powermeterValues);
            this->isPersistanceDirty = true;
            PWBOARD_DEBUG_MSG("_editPowermetersEND\n");
            return true;
        }
        else
        {
            PWBOARD_DEBUG_MSG("_editPowermetersEND\n");
            return false;
        }
    }
    boolean _removePowermeter(uint8 powermeterIndex)
    {
        Powermeter *pPowermeter = this->m_Powermeters[powermeterIndex];
        if (NULL != pPowermeter)
        {
            this->m_Powermeters[powermeterIndex] = NULL;
            delete pPowermeter;
            return true;
        }
        return false;
    }
    boolean _addPowermeter(PowermeterDef powermeterDef, DDS238Data powermeterValues)
    {
        // uint32_t freeBefore = ESP.getFreeHeap();
        // PWBOARD_DEBUG_MSG(" free heap %d\n", freeBefore);
        _printPowermeterDef(powermeterDef);
        _printPowermeterData(powermeterValues);

        uint8 powermeterIndex = BoardIOToPowermeterIndex[powermeterDef.dIO];
        Powermeter *pPowermeter = this->m_Powermeters[powermeterIndex];

        if (NULL == pPowermeter)
        {
            // allocate a PowerMeter object
            this->m_Powermeters[powermeterIndex] = new Powermeter(
                powermeterDef,
                powermeterValues,
                m_pPowerMeterDevice,
                // create lambda as persistance callback
                [this, powermeterIndex](DDS238Data data)
                {
                    this->m_PowermeterDatasPersistance[powermeterIndex] = data;
                    // @TODO analyse if broadcast should be done async in main loop
                    this->_broadcastPowerMeterData(powermeterIndex, NULL);
                    this->isPersistanceDirty = true;
                });
            // PWBOARD_DEBUG_MSG("Add at %d\n",powermeterIndex);
            this->isPersistanceDirty = true;

            PWBOARD_DEBUG_MSG("add new powermeter %d %d\n", BoardIOToPowermeterIndex[powermeterDef.dIO] + 1, powermeterDef.dIO);
            return true;
        }
        else
        {
            PWBOARD_DEBUG_MSG("powermeter %d %d already exist\n", BoardIOToPowermeterIndex[powermeterDef.dIO] + 1, powermeterDef.dIO);
            return false;
        }
    }

    void _broadcastPowerMeterInfo(int index, AsyncWebSocketClient *client)
    {
        PWBOARD_DEBUG_MSG("_broadcastPowerMeterInfo %d to %s\n", index, (NULL == client) ? "ALL" : "client");
        JsonDocument doc;
        doc["type"] = "pdu";

        for (int i = 0; i < 10; i++)
        {
            if (
                (NULL != this->m_Powermeters[i]) &&
                ((i == index) || (index == 255)))
            {
                PWBOARD_DEBUG_MSG("_broadcastPowerMeterInfo adding %d \n", i);
                JsonObject data = doc["datas"].add<JsonObject>();
                _fillDefinitionToJson(i, data);
                _fillPMDatatoJson(i, data);
                if (index != 255)
                    break;
            }
        }

        String response;
        doc.shrinkToFit(); // optional
        serializeJson(doc, response);
        if (NULL != client)
        {
            client->text(response);
        }
        else
        {
            ws.textAll(response);
        }

        // const size_t CAPACITY = JSON_OBJECT_SIZE(7);
        // int nbElement = 0;

        // if (index == 255)
        // {
        //     for (int i = 0; i < 10; i++)
        //     {
        //         if (NULL != this->m_Powermeters[i])
        //         {
        //             nbElement++;
        //         }
        //     }
        // }
        // else
        // {
        //     nbElement = 1;
        // }

        // DynamicJsonDocument doc(nbElement * CAPACITY);
        // JsonArray root = doc.to<JsonArray>();
        // PWBOARD_DEBUG_MSG("_broadcastPowerMeterInfo %d \n", nbElement);

        // for (int i = 0; i < 10; i++)
        // {
        //     if (
        //         (NULL != this->m_Powermeters[i]) &&
        //         ((i == index) || (index == 255)))
        //     {
        //         PWBOARD_DEBUG_MSG("_broadcastPowerMeterInfo adding %d \n", i);
        //         JsonObject obj = root.createNestedObject();
        //         _fillDefinitionToJson(i, obj);
        //         _fillPMDatatoJson(i, obj);
        //         if (index != 255)
        //             break;
        //     }
        // }

        // _broadcastEvent("pi", root, client);
    }
    void _broadcastPowerMeterData(uint8 index, AsyncWebSocketClient *client)
    {
        PWBOARD_DEBUG_MSG("_broadcastPowerMeterData %d to %s\n", index, (NULL == client) ? "ALL" : "client");

        JsonDocument doc;
        doc["type"] = "pdu";
        for (int i = 0; i < 10; i++)
        {
            if (
                (NULL != this->m_Powermeters[i]) &&
                ((i == index) || (index == 255)))
            {
                JsonObject data = doc["datas"].add<JsonObject>();
                data["dIO"] = PowermeterIndexToBoardIO[i];
                _fillPMDatatoJson(i, data);
                if (index != 255)
                    break;
            }
        }

        String response;
        doc.shrinkToFit(); // optional
        serializeJson(doc, response);
        if (NULL != client)
        {
            client->text(response);
        }
        else
        {
            ws.textAll(response);
        }

        // const size_t CAPACITY = JSON_OBJECT_SIZE(4);
        // int nbElement = 0;

        // if (index == 255)
        // {
        //     for (int i = 0; i < 10; i++)
        //     {
        //         if (NULL != this->m_Powermeters[i])
        //         {
        //             nbElement++;
        //         }
        //     }
        // }
        // else
        // {
        //     nbElement = 1;
        // }

        // DynamicJsonDocument doc(nbElement * CAPACITY);
        // JsonArray root = doc.to<JsonArray>();

        // for (int i = 0; i < 10; i++)
        // {
        //     if (
        //         (NULL != this->m_Powermeters[i]) &&
        //         ((i == index) || (index == 255)))
        //     {
        //         JsonObject obj = root.createNestedObject();
        //         obj["dIO"] = PowermeterIndexToBoardIO[i];
        //         _fillPMDatatoJson(i + 1, obj);
        //         if (index != 255)
        //             break;
        //     }
        // }
        // _broadcastEvent("pdu", root, client);
    }
    const char *_wl_status_to_string(wl_status_t status)
    {
        switch (status)
        {
        case WL_NO_SHIELD:
            return "WL_NO_SHIELD";
        case WL_IDLE_STATUS:
            return "WL_IDLE_STATUS";
        case WL_NO_SSID_AVAIL:
            return "WL_NO_SSID_AVAIL";
        case WL_SCAN_COMPLETED:
            return "WL_SCAN_COMPLETED";
        case WL_CONNECTED:
            return "WL_CONNECTED";
        case WL_CONNECT_FAILED:
            return "WL_CONNECT_FAILED";
        case WL_CONNECTION_LOST:
            return "WL_CONNECTION_LOST";
        case WL_WRONG_PASSWORD:
            return "WL_WRONG_PASSWORD";
        case WL_DISCONNECTED:
            return "WL_DISCONNECTED";
        }
        return "UNKNOWN";
    }
    void _broadcastMQTTConnectionStatus(AsyncWebSocketClient *client)
    {
        PWBOARD_DEBUG_MSG("_broadcastMQTTConnectionStatus %sconnected\n", (isMqttconnected) ? "" : "dis");
        JsonDocument doc;
        doc["type"] = "mcs";
        doc["datas"] = isMqttconnected;
        String response;
        doc.shrinkToFit(); // optional
        serializeJson(doc, response);
        if (NULL != client)
        {
            client->text(response);
        }
        else
        {
            ws.textAll(response);
        }

        // allocate the memory for the document
        // DynamicJsonDocument doc(10);
        // // create a variant
        // JsonVariant mqttStatusEvent = doc.to<JsonVariant>();
        // mqttStatusEvent.set(isMqttconnected);

        // _broadcastEvent("mcs", mqttStatusEvent, client);
    }
    void _broadcastWIFIStatus(AsyncWebSocketClient *client)
    {
        PWBOARD_DEBUG_MSG("_broadcastWIFIStatus %s to %s\n", _wl_status_to_string(WiFi.status()), (NULL == client) ? "ALL" : "client");
        JsonDocument doc;
        doc["type"] = "ws";
        doc["datas"] = WiFi.status();
        String response;
        doc.shrinkToFit(); // optional
        serializeJson(doc, response);
        if (NULL != client)
        {
            client->text(response);
        }
        else
        {
            ws.textAll(response);
        }

        // // allocate the memory for the document
        // DynamicJsonDocument doc(10);
        // // create a variant
        // JsonVariant statusEvent = doc.to<JsonVariant>();
        // statusEvent.set(WiFi.status());
        // _broadcastEvent("ws", statusEvent, client);
    }
    void _broadcastWIFIConfig(AsyncWebSocketClient *client)
    {
        PWBOARD_DEBUG_MSG("_broadcastWIFIConfig to %s\n", (NULL == client) ? "ALL" : "client");
        JsonDocument doc;
        doc["type"] = "wc";
        JsonObject datas = doc["datas"].to<JsonObject>();
        datas["ssid"] = WiFi.SSID();
        datas["rssi"] = WiFi.RSSI();
        datas["bssid"] = WiFi.BSSIDstr();
        datas["channel"] = String(WiFi.channel());
        datas["ip"] = WiFi.localIP().toString();
        datas["host"] = WiFi.hostname();
        datas["status"] = String(WiFi.status());

        String response;
        doc.shrinkToFit(); // optional
        serializeJson(doc, response);
        if (NULL != client)
        {
            client->text(response);
        }
        else
        {
            ws.textAll(response);
        }

        // // allocate the memory for the document
        // const size_t CAPACITY = JSON_OBJECT_SIZE(1);
        // DynamicJsonDocument doc(256);

        // // create a variant
        // JsonObject wifiInfoEvent = doc.to<JsonObject>();
        // wifiInfoEvent["ssid"] = WiFi.SSID();
        // wifiInfoEvent["rssi"] = WiFi.RSSI();
        // wifiInfoEvent["bssid"] = WiFi.BSSIDstr();
        // wifiInfoEvent["channel"] = String(WiFi.channel());
        // wifiInfoEvent["ip"] = WiFi.localIP().toString();
        // wifiInfoEvent["host"] = WiFi.hostname();
        // wifiInfoEvent["status"] = String(WiFi.status());
        // _broadcastEvent("wc", wifiInfoEvent, client);
    }
    // void _broadcastEvent(const char *eventType, const JsonArray &eventDatas, AsyncWebSocketClient *client)
    // {
    //     PWBOARD_DEBUG_MSG("_broadcastEvent array %s to %s\n", eventType, (NULL == client) ? "ALL" : "client");
    //     String response;
    //     // const size_t CAPACITY = JSON_OBJECT_SIZE(3);

    //     DynamicJsonDocument doc(1024);
    //     doc["type"] = eventType;
    //     doc["datas"] = eventDatas;

    //     serializeJson(doc, response);

    //     if (NULL != client)
    //     {
    //         client->text(response);
    //     }
    //     else
    //     {
    //         ws.textAll(response);
    //     }
    // }
    // void _broadcastEvent(const char *eventType, const JsonVariant &eventDatas, AsyncWebSocketClient *client)
    // {
    //     PWBOARD_DEBUG_MSG("_broadcastEvent variant %s to %s\n", eventType, (NULL == client) ? "ALL" : "client");
    //     String response;
    //     const size_t CAPACITY = JSON_OBJECT_SIZE(3);

    //     DynamicJsonDocument doc(CAPACITY);
    //     doc["type"] = eventType;
    //     doc["datas"] = eventDatas;

    //     serializeJson(doc, response);
    //     PWBOARD_DEBUG_MSG("_broadcastEvent %s\n", response.c_str());
    //     if (NULL != client)
    //     {
    //         client->text(response);
    //     }
    //     else
    //     {
    //         ws.textAll(response);
    //     }
    // }
    // void _broadcastEvent(const char *eventType, const JsonObject &eventDatas, AsyncWebSocketClient *client)
    // {
    //     PWBOARD_DEBUG_MSG("_broadcastEvent document %s to %s\n", eventType, (NULL == client) ? "ALL" : "client");
    //     String response;
    //     // const size_t CAPACITY = JSON_OBJECT_SIZE(3);

    //     DynamicJsonDocument doc(1024);
    //     doc["type"] = eventType;
    //     doc["datas"] = eventDatas;

    //     serializeJson(doc, response);

    //     if (NULL != client)
    //     {
    //         client->text(response);
    //     }
    //     else
    //     {
    //         ws.textAll(response);
    //     }
    // }

    //
    // int addPM(JsonObject Name);
    // String getPM(int index);
    // void removePM(int index);
    // void updatePM(String jsonConfig);
    // void updatePersistence();

    PowermeterBoardSettings m_PowermeterBoardSettings;

    HADevice *m_pPowerMeterDevice;
    // HAComponent *m_pRebootComponent;
    Powermeter *m_Powermeters[10];
    DDS238Data m_PowermeterDatasPersistance[10];
    // FS &m_FileSystem;
    uint8_t lastWifiStatus = 0;

    int m_TagPersistanceIndex;
    int m_SettingsPersistanceIndex;
    int m_DataPersistanceIndex;
    int m_DefinitionPersistanceIndex;

    boolean isPersistanceDirty = false;
    boolean isMqttconnected = false;
};

PowermeterBoard POWERMETERBOARD;

#endif