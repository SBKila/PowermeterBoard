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
    
    void setupEditPowerMeterHandler(const char *deviceName, Client &pEthClient, AsyncWebServer *p_pWebServer, fs::FS fs)
    {
        AsyncCallbackJsonWebHandler *handler = new AsyncCallbackJsonWebHandler("/pmb/pm", [this](AsyncWebServerRequest *request, JsonVariant &json)
                                                                               {
            PWBOARD_DEBUG_MSG("POST /pmb/pm\n");
            const JsonObject &jsonObj = json.as<JsonObject>();

            PowermeterDef powermeterDef;
            strncpy(powermeterDef.name, jsonObj["pm_name"], 25);
            powermeterDef.maxAmp = jsonObj["pm_maxamp"];
            powermeterDef.nbTickByKW = jsonObj["pm_nbtick"];
            powermeterDef.voltage = jsonObj["pm_voltage"];
            powermeterDef.dIO = PowermeterIndexToBoardIO[(int)jsonObj["pm_ref"] - 1];

            if (_addPowermeters(powermeterDef))
            {
                request->send(200, "application/json", _getPowermeterAsJsonString(powermeterDef));
            }
            else
            {
                request->send(400);
            } });
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
                    DDS238Data powermeterDatasPersistance[10];
                    PWBOARD_DEBUG_MSG("index%d\n", index);
                    for (JsonObject pmDefinition : pmDefinitions)
                    {
                        PWBOARD_DEBUG_MSG(pmDefinition["name"]);
                        strncpy(storedPowerMeterDefinitions[index].name, pmDefinition["name"], 25);
                        storedPowerMeterDefinitions[index].maxAmp = pmDefinition["maxAmp"];
                        storedPowerMeterDefinitions[index].nbTickByKW = pmDefinition["nbTickByKW"];
                        storedPowerMeterDefinitions[index].voltage = pmDefinition["voltage"];
                        storedPowerMeterDefinitions[index].dIO = pmDefinition["dIO"];

                        powermeterDatasPersistance[index].tag = 963;
                        powermeterDatasPersistance[index].ticks = pmDefinition["ticks"];
                        powermeterDatasPersistance[index].cumulative = pmDefinition["cumulative"];

                        _printPowermeterDef(storedPowerMeterDefinitions[index]);
                        _printPowermeterData(powermeterDatasPersistance[index]);
                        index++;
                    }

                    EEPROMEX.put(m_DefinitionPersistanceIndex, storedPowerMeterDefinitions);
                    EEPROMEX.put(m_DataPersistanceIndex, powermeterDatasPersistance);
                    EEPROMEX.commit();
                    request->send(200);
                    forceRestart = 2000;
                }
                else if (json.is<JsonObject>())
                {
                    PWBOARD_DEBUG_MSG("data is object\n");
                    const JsonObject &jsonObj = json.as<JsonObject>();

                    PowermeterDef powermeterDef;
                    strncpy(powermeterDef.name, jsonObj["pm_name"], 25);
                    powermeterDef.maxAmp = jsonObj["pm_maxamp"];
                    powermeterDef.nbTickByKW = jsonObj["pm_nbtick"];
                    powermeterDef.voltage = jsonObj["pm_voltage"];
                    powermeterDef.dIO = PowermeterIndexToBoardIO[(int)jsonObj["pm_ref"] - 1];

                    if (_addPowermeters(powermeterDef))
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
                DynamicJsonDocument doc(1024);
                boolean atleastone = false;
                for (int i = 0; i < 10; i++)
                {
                    Powermeter *pPowerMeter = this->m_Powermeters[i];
                    
                    PWBOARD_DEBUG_MSG("(%d,%d),",i,(NULL != pPowerMeter)?pPowerMeter->getDefinition().dIO:-1);
                    if (NULL != pPowerMeter)
                    {
                        atleastone = true;
                        JsonObject obj = doc.createNestedObject();
                        _fillDefinitionToJson(pPowerMeter->getDefinition(), obj);
                        _fillPMDatatoJson(pPowerMeter->getDefinition().dIO, obj);
                    }
                }
                PWBOARD_DEBUG_MSG("\n");

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

        setupEditPowerMeterHandler(deviceName, pEthClient, p_pWebServer, fs);
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
                _addPowermeters(storedPowerMeterDefinitions[i]);
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
    }

    int forceRestart = -1;
    void loop()
    {
        if (forceRestart == 0)
        {
            ESP.restart();
        }
        if (forceRestart > 0)
        {
            forceRestart--;
            return;
        }

        for (int index = 0; index < 10; index++)
        {
            if (m_Powermeters[index] != NULL)
            {
                m_Powermeters[index]->loop();
            }
        }
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

private:
    void _fillPMDatatoJson(int index, JsonObject &obj)
    {
        obj["cumulative"] = m_PowermeterDatasPersistance[index - 1].cumulative;
        obj["ticks"] = m_PowermeterDatasPersistance[index - 1].ticks;
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
        const size_t CAPACITY = JSON_OBJECT_SIZE(7);
        StaticJsonDocument<CAPACITY> doc;
        // create an object
        JsonObject obj = doc.to<JsonObject>();
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
        PWBOARD_DEBUG_MSG("pm cumulative %d\n", powermeterData.cumulative);
    }
    boolean _editPowermeters(PowermeterDef powermeterDef)
    {
        // uint32_t freeBefore = ESP.getFreeHeap();
        // PWBOARD_DEBUG_MSG(" free heap %d\n", freeBefore);
        _printPowermeterDef(powermeterDef);
        uint8 powermeterIndex = BoardIOToPowermeterIndex[powermeterDef.dIO];
        Powermeter *pPowermeter = this->m_Powermeters[powermeterIndex];
        if (NULL != pPowermeter)
        {
            this->isPersistanceDirty = true;
        }
        else
        {
            return false;
        }
    }
    boolean _addPowermeters(PowermeterDef powermeterDef)
    {
        // uint32_t freeBefore = ESP.getFreeHeap();
        // PWBOARD_DEBUG_MSG(" free heap %d\n", freeBefore);
        _printPowermeterDef(powermeterDef);
        uint8 powermeterIndex = BoardIOToPowermeterIndex[powermeterDef.dIO];
        Powermeter *pPowermeter = this->m_Powermeters[powermeterIndex];

        if (NULL == pPowermeter)
        {
            // allocate a PowerMeter object
            this->m_Powermeters[powermeterIndex] = new Powermeter(
                powermeterDef,
                m_pPowerMeterDevice,
                // create lambda as persistance callback
                [this, powermeterIndex](DDS238Data data)
                {
                    this->m_PowermeterDatasPersistance[powermeterIndex] = data;
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

        const size_t CAPACITY = JSON_OBJECT_SIZE(7);
        int nbElement = 0;

        if (index == 255)
        {
            for (int i = 0; i < 10; i++)
            {
                if (NULL != this->m_Powermeters[i])
                {
                    nbElement++;
                }
            }
        }
        else
        {
            nbElement = 1;
        }

        DynamicJsonDocument doc(nbElement * CAPACITY);
        JsonArray root = doc.to<JsonArray>();
        PWBOARD_DEBUG_MSG("_broadcastPowerMeterInfo %d \n", nbElement);

        for (int i = 0; i < 10; i++)
        {
            if (
                (NULL != this->m_Powermeters[i]) &&
                ((i == index) || (index == 255)))
            {
                PWBOARD_DEBUG_MSG("_broadcastPowerMeterInfo adding %d \n", i);
                JsonObject obj = root.createNestedObject();
                _fillDefinitionToJson(i, obj);
                _fillPMDatatoJson(i, obj);
                if (index != 255)
                    break;
            }
        }

        _broadcastEvent("pi", root, client);
    }
    void _broadcastPowerMeterData(uint8 index, AsyncWebSocketClient *client)
    {
        PWBOARD_DEBUG_MSG("_broadcastPowerMeterData %d to %s\n", index, (NULL == client) ? "ALL" : "client");
        const size_t CAPACITY = JSON_OBJECT_SIZE(4);
        int nbElement = 0;

        if (index == 255)
        {
            for (int i = 0; i < 10; i++)
            {
                if (NULL != this->m_Powermeters[i])
                {
                    nbElement++;
                }
            }
        }
        else
        {
            nbElement = 1;
        }

        DynamicJsonDocument doc(nbElement * CAPACITY);
        JsonArray root = doc.to<JsonArray>();

        for (int i = 0; i < 10; i++)
        {
            if (
                (NULL != this->m_Powermeters[i]) &&
                ((i == index) || (index == 255)))
            {
                JsonObject obj = root.createNestedObject();
                obj["dIO"] = PowermeterIndexToBoardIO[i];
                _fillPMDatatoJson(i + 1, obj);
                if (index != 255)
                    break;
            }
        }
        _broadcastEvent("pdu", root, client);
    }
    void _broadcastMQTTConnectionStatus(AsyncWebSocketClient *client)
    {
        // allocate the memory for the document
        DynamicJsonDocument doc(10);
        // create a variant
        JsonVariant mqttStatusEvent = doc.to<JsonVariant>();
        mqttStatusEvent.set(isMqttconnected);

        _broadcastEvent("mcs", mqttStatusEvent, client);
    }
    void _broadcastWIFIStatus(AsyncWebSocketClient *client)
    {
        // allocate the memory for the document
        DynamicJsonDocument doc(10);
        // create a variant
        JsonVariant statusEvent = doc.to<JsonVariant>();
        statusEvent.set(WiFi.status());
        _broadcastEvent("ws", statusEvent, client);
    }
    void _broadcastWIFIConfig(AsyncWebSocketClient *client)
    {
        // allocate the memory for the document
        const size_t CAPACITY = JSON_OBJECT_SIZE(1);
        DynamicJsonDocument doc(256);

        // create a variant
        JsonObject wifiInfoEvent = doc.to<JsonObject>();
        wifiInfoEvent["ssid"] = WiFi.SSID();
        wifiInfoEvent["rssi"] = WiFi.RSSI();
        wifiInfoEvent["bssid"] = WiFi.BSSIDstr();
        wifiInfoEvent["channel"] = String(WiFi.channel());
        wifiInfoEvent["ip"] = WiFi.localIP().toString();
        wifiInfoEvent["host"] = WiFi.hostname();
        wifiInfoEvent["status"] = String(WiFi.status());
        _broadcastEvent("wc", wifiInfoEvent, client);
    }
    void _broadcastEvent(const char *eventType, const JsonArray &eventDatas, AsyncWebSocketClient *client)
    {
        PWBOARD_DEBUG_MSG("_broadcastEvent array %s to %s\n", eventType, (NULL == client) ? "ALL" : "client");
        String response;
        // const size_t CAPACITY = JSON_OBJECT_SIZE(3);

        DynamicJsonDocument doc(1024);
        doc["type"] = eventType;
        doc["datas"] = eventDatas;

        serializeJson(doc, response);

        if (NULL != client)
        {
            client->text(response);
        }
        else
        {
            ws.textAll(response);
        }
    }
    void _broadcastEvent(const char *eventType, const JsonVariant &eventDatas, AsyncWebSocketClient *client)
    {
        PWBOARD_DEBUG_MSG("_broadcastEvent variant %s to %s\n", eventType, (NULL == client) ? "ALL" : "client");
        String response;
        const size_t CAPACITY = JSON_OBJECT_SIZE(3);

        DynamicJsonDocument doc(CAPACITY);
        doc["type"] = eventType;
        doc["datas"] = eventDatas;

        serializeJson(doc, response);
        PWBOARD_DEBUG_MSG("_broadcastEvent %s\n", response.c_str());
        if (NULL != client)
        {
            client->text(response);
        }
        else
        {
            ws.textAll(response);
        }
    }
    void _broadcastEvent(const char *eventType, const JsonObject &eventDatas, AsyncWebSocketClient *client)
    {
        PWBOARD_DEBUG_MSG("_broadcastEvent document %s to %s\n", eventType, (NULL == client) ? "ALL" : "client");
        String response;
        // const size_t CAPACITY = JSON_OBJECT_SIZE(3);

        DynamicJsonDocument doc(1024);
        doc["type"] = eventType;
        doc["datas"] = eventDatas;

        serializeJson(doc, response);

        if (NULL != client)
        {
            client->text(response);
        }
        else
        {
            ws.textAll(response);
        }
    }

    //
    // int addPM(JsonObject Name);
    // String getPM(int index);
    // void removePM(int index);
    // void updatePM(String jsonConfig);
    // void updatePersistence();

    PowermeterBoardSettings m_PowermeterBoardSettings;

    HADevice *m_pPowerMeterDevice;
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