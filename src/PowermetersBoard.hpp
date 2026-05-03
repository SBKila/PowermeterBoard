#ifndef PMBOARD_H
#define PMBOARD_H

#pragma once
#include "AsyncJson.h"
#include "EEPROMEX.h"
#include "ESPAsyncWebServer.h"
#include "Powermeter.hpp"
#include <MD5Builder.h>
#include <AESLib.h>
#include "WifiManager.hpp"

// EXTERN BLINKER STATE
extern int g_currentBlinkerState;
extern String getBlinkerStateString(int state);

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
#define NBPOWERMETERS 10
#include "HALib/HALib.h"

AsyncWebSocket ws("/pmb/ws"); // access at ws://[esp ip]/pmb/ws
// AsyncEventSource events("pmb/events"); // event source (Server-Sent events)

struct PowermeterBoardSettings {
  int tag;
  char node_name[32];
  char ssid_name[32];
  char ssid_key[64];
  char mqtt_domain[128];
  int mqtt_port = 1883;
  char mqtt_login[32];
  char mqtt_pwd[64];
};
class PowermeterBoard {
public:
  PowermeterBoard() {
    memset(m_Powermeters, 0, sizeof(m_Powermeters));
    memset((void *)&m_PowermeterDatasPersistance, 0,
           NBPOWERMETERS * sizeof(DDS238Data));
  }
  ~PowermeterBoard() { delete (m_pPowerMeterDevice); }
  size_t setupRTCPersistance(size_t offset) {
    return sizeof(DDS238Data[NBPOWERMETERS]);
  }
  int setupPersistance() {
    // manage Persistance tag
    m_TagPersistanceIndex = EEPROMEX.allocate(sizeof(int));
    // manage PowerMeter Persistance
    m_DefinitionPersistanceIndex =
        EEPROMEX.allocate(sizeof(PowermeterDef[NBPOWERMETERS]));
    // manage PowerMeter Data Persistance
    m_DataPersistanceIndex =
        EEPROMEX.allocate(sizeof(DDS238Data[NBPOWERMETERS]));
    // manage PowerMeter setting
    m_SettingsPersistanceIndex =
        EEPROMEX.allocate(sizeof(PowermeterBoardSettings));
    return sizeof(m_TagPersistanceIndex) +
           sizeof(PowermeterDef[NBPOWERMETERS]) +
           sizeof(m_PowermeterDatasPersistance) +
           sizeof(m_PowermeterBoardSettings);
  }

  void setupHandlerUpdatePowerMeter(const char *deviceName,
                                    AsyncWebServer *p_pWebServer, fs::FS fs) {
    AsyncCallbackJsonWebHandler *handler = new AsyncCallbackJsonWebHandler(
        "/pmb/pm", [this](AsyncWebServerRequest *request, JsonVariant &json) {
          PWBOARD_DEBUG_MSG(F("POST /pmb/pm\n"));
          const JsonObject &pmDefinition = json.as<JsonObject>();

          uint8 powermeterIndex = (int)pmDefinition["pmref"] - 1;

          PowermeterDef powermeterDef;
          const char *nameSrc = pmDefinition["name"];
          if (nameSrc) {
            strncpy(powermeterDef.name, nameSrc,
                    sizeof(powermeterDef.name) - 1);
            powermeterDef.name[sizeof(powermeterDef.name) - 1] =
                '\0'; // REQUIRED
          } else {
            strcpy(powermeterDef.name, "Unknown");
          }
          powermeterDef.maxAmp = pmDefinition["maxAmp"];
          powermeterDef.nbTickByKW = pmDefinition["nbTickByKW"];
          powermeterDef.voltage = pmDefinition["voltage"];
          powermeterDef.dIO = PowermeterIndexToBoardIO[powermeterIndex];
          DDS238Data powermeterValues;
          powermeterValues.tag =
              m_PowermeterDatasPersistance[powermeterIndex].tag;
          powermeterValues.ticks = 0;
          powermeterValues.cumulative = pmDefinition["cumulative"];

          if (_removePowermeter(powermeterIndex)) {
            // update data storage
            m_PowermeterDatasPersistance[powermeterIndex].tag =
                powermeterValues.tag;
            m_PowermeterDatasPersistance[powermeterIndex].ticks =
                powermeterValues.ticks;
            m_PowermeterDatasPersistance[powermeterIndex].cumulative =
                powermeterValues.cumulative;
          }
          if (_addPowermeter(powermeterDef, powermeterValues, true)) {
            request->send(
                200, "application/json",
                _getPowermeterAsJsonString(powermeterDef, powermeterValues));
          } else {
            request->send(400);
          }
        });
    handler->setMethod(HTTP_POST);
    p_pWebServer->addHandler((AsyncWebHandler *)handler);
  };
  void setupHandlerAddPowerMeter(const char *deviceName,
                                 AsyncWebServer *p_pWebServer, fs::FS fs) {
    AsyncCallbackJsonWebHandler *handler = new AsyncCallbackJsonWebHandler(
        "/pmb/pm", [this](AsyncWebServerRequest *request, JsonVariant &json) {
          PWBOARD_DEBUG_MSG(F("PUT /pmb/pm\n"));

          if (json.is<JsonArray>()) {
            PWBOARD_DEBUG_MSG(F("data is array\n"));
            const JsonArray &pmDefinitions = json.as<JsonArray>();

            int index = 0;
            PowermeterDef storedPowerMeterDefinitions[NBPOWERMETERS];
            // DDS238Data powermeterDatasPersistance[NBPOWERMETERS];

            for (JsonObject pmDefinition : pmDefinitions) {
              PWBOARD_DEBUG_MSG(F("index %d\n"), index);
              PWBOARD_DEBUG_MSG(F("=> ticks %d d\n"),
                                (int)pmDefinition["ticks"]);
              PWBOARD_DEBUG_MSG(F("=> cumulative %f f\n"),
                                (float)pmDefinition["cumulative"]);
              PWBOARD_DEBUG_MSG(F("=> nbTickByKW %d d\n"),
                                (int)pmDefinition["nbTickByKW"]);

              strncpy(storedPowerMeterDefinitions[index].name,
                      pmDefinition["name"], 24);
              storedPowerMeterDefinitions[index].name[24] = '\0';
              storedPowerMeterDefinitions[index].maxAmp =
                  pmDefinition["maxAmp"];
              storedPowerMeterDefinitions[index].nbTickByKW =
                  pmDefinition["nbTickByKW"];
              storedPowerMeterDefinitions[index].voltage =
                  pmDefinition["voltage"];
              storedPowerMeterDefinitions[index].dIO = pmDefinition["dIO"];
              PowermeterDef newDef;
              const char *nameSrc = pmDefinition["name"];
              if (nameSrc) {
                strncpy(newDef.name, nameSrc, sizeof(newDef.name) - 1);
                newDef.name[sizeof(newDef.name) - 1] = '\0'; // REQUIRED
              } else {
                strcpy(newDef.name, "Unknown");
              }
              newDef.maxAmp = pmDefinition["maxAmp"];
              newDef.nbTickByKW = pmDefinition["nbTickByKW"];
              newDef.voltage = pmDefinition["voltage"];
              newDef.dIO = pmDefinition["dIO"];
              // PowermeterDef curDef = m_Powermeters[index]->getDefinition();
              DDS238Data powerMeterData;
              powerMeterData.ticks = 0;
              powerMeterData.cumulative = pmDefinition["cumulative"];
              _updatePowermeters(newDef, powerMeterData);

              index++;
            }
            EEPROMEX.put(m_DefinitionPersistanceIndex,
                         storedPowerMeterDefinitions);
            EEPROMEX.put(m_DataPersistanceIndex, m_PowermeterDatasPersistance);
            // EEPROMEX.put(m_DataPersistanceIndex, powermeterDatasPersistance);
            isPersistanceDirty = true;

            request->send(200);
            forceRestart = 2000;
          } else if (json.is<JsonObject>()) {
            PWBOARD_DEBUG_MSG(F("data is object\n"));
            const JsonObject &jsonObj = json.as<JsonObject>();
            PowermeterDef powermeterDef;
            const char *jsonName = jsonObj["name"];
            strncpy(powermeterDef.name, jsonName ? jsonName : "Unknown", 24);
            powermeterDef.name[24] = '\0';
            powermeterDef.maxAmp = jsonObj["maxAmp"];
            powermeterDef.nbTickByKW = jsonObj["nbTickByKW"];
            powermeterDef.voltage = jsonObj["voltage"];
            powermeterDef.dIO = (int)jsonObj["dIO"];
            DDS238Data powermeterValues;
            powermeterValues.ticks = 0;
            powermeterValues.cumulative = jsonObj["cumulative"];

            if (_addPowermeter(powermeterDef, powermeterValues, true)) {
              request->send(
                  200, "application/json",
                  _getPowermeterAsJsonString(powermeterDef, powermeterValues));
            } else {
              request->send(400);
            }
          } else {
            PWBOARD_DEBUG_MSG(F("data is something else\n"));
            request->send(400);
          }
        });
    handler->setMethod(HTTP_PUT);
    p_pWebServer->addHandler(handler);
  }
  void setupHandlerDeletePowerMeter(const char *deviceName,
                                    AsyncWebServer *p_pWebServer, fs::FS fs) {
    // Handle a DELETE request to /pmb/pm/<pmIndex>
    p_pWebServer->on(
        "^\\/pmb\\/pm\\/([0-9])$", HTTP_DELETE,
        [this](AsyncWebServerRequest *request) {
          uint8 powermeterIndex = (uint8)request->pathArg(0).toInt();
          PWBOARD_DEBUG_MSG(F("DELETE /pmb/pm/%d\n"), powermeterIndex);
          boolean removed = this->_removePowermeter(powermeterIndex);
          request->send(removed ? 200 : 404);
          if (removed)
            this->_broadcastPowerMeterRemoved(powermeterIndex, NULL);
        });
  };
  void setupHandlerGetPowerMeters(const char *deviceName,
                                  AsyncWebServer *p_pWebServer, fs::FS fs) {
    p_pWebServer->on(
        "/pmb/pm", HTTP_GET,
        [this](AsyncWebServerRequest *request) {
          PWBOARD_DEBUG_MSG(F("GET /pmb/pm\n"));
          AsyncResponseStream *response =
              request->beginResponseStream("application/json");
#ifdef ARDUINOJSON_6_COMPATIBILITY
          DynamicJsonDocument doc(1024);
#else
          JsonDocument doc;
#endif
          boolean atleastone = false;
          for (int i = 0; i < NBPOWERMETERS; i++) {
            Powermeter *pPowerMeter = this->m_Powermeters[i];
            // PWBOARD_DEBUG_MSG("(%d,%d),", i, (NULL != pPowerMeter) ?
            // pPowerMeter->getDefinition().dIO : -1);
            if (NULL != pPowerMeter) {
              atleastone = true;
#ifdef ARDUINOJSON_6_COMPATIBILITY
              JsonObject obj = doc.createNestedObject();
#else
              JsonObject obj = doc.add<JsonObject>();
#endif
              _fillDefinitionToJson(pPowerMeter->getDefinition(), obj);
              _fillPMDatatoJson(i, obj);
            }
          }
          if (atleastone) {
            serializeJson(doc, *response);
          } else {
            response->println("[]");
          }

          request->send(response);
        },
        NULL, NULL);
  }
  void setupHandlerSetMqtt(const char *deviceName, AsyncWebServer *p_pWebServer,
                           fs::FS fs) {

    AsyncCallbackJsonWebHandler *handler = new AsyncCallbackJsonWebHandler(
        "/pmb/mqtt", [this](AsyncWebServerRequest *request, JsonVariant &json) {
          PWBOARD_DEBUG_MSG(F("POST /pmb/mqtt\n"));
          const JsonObject &jsonObj = json.as<JsonObject>();

          strncpy(this->m_PowermeterBoardSettings.mqtt_domain,
                  jsonObj["mqtt_url"],
                  sizeof(this->m_PowermeterBoardSettings.mqtt_domain));
          this->m_PowermeterBoardSettings.mqtt_port =
              atoi(jsonObj["mqtt_port"]);
          strncpy(this->m_PowermeterBoardSettings.mqtt_login,
                  jsonObj["mqtt_login"],
                  sizeof(this->m_PowermeterBoardSettings.mqtt_login));
          strncpy(this->m_PowermeterBoardSettings.mqtt_pwd, jsonObj["mqtt_pwd"],
                  sizeof(this->m_PowermeterBoardSettings.mqtt_pwd));

          this->m_PowermeterBoardSettings.tag = PMBMAGIC;
          this->isPersistanceDirty = true;

          PWBOARD_DEBUG_MSG(F("POST %s:%d (%s:%s)\n"),
                            this->m_PowermeterBoardSettings.mqtt_domain,
                            this->m_PowermeterBoardSettings.mqtt_port,
                            m_PowermeterBoardSettings.mqtt_login,
                            m_PowermeterBoardSettings.mqtt_pwd);

          m_pPowerMeterDevice->setup(
              this->m_PowermeterBoardSettings.mqtt_domain,
              this->m_PowermeterBoardSettings.mqtt_port,
              m_PowermeterBoardSettings.mqtt_login,
              m_PowermeterBoardSettings.mqtt_pwd);

          request->send(200);
        });
    handler->setMethod(HTTP_POST);
    p_pWebServer->addHandler(handler);
  }

  void setupHandlerConfig(const char *deviceName, AsyncWebServer *p_pWebServer, fs::FS fs) {
    p_pWebServer->on("/pmb/config/export", HTTP_POST, [this](AsyncWebServerRequest *request) {
      if (!request->hasParam("pwd", true)) {
        request->send(400, "text/plain", "Missing password");
        return;
      }
      String pwd = request->getParam("pwd", true)->value();
      
      JsonDocument doc;
      JsonObject wifiObj = doc["wifi"].to<JsonObject>();
      WIFIMANAGER.exportConfigJson(wifiObj);
      
      JsonObject mqttObj = doc["mqtt"].to<JsonObject>();
      this->exportConfigJson(mqttObj);
      
      JsonArray pmArr = doc["powermeters"].to<JsonArray>();
      for (int i = 0; i < NBPOWERMETERS; i++) {
        if (m_Powermeters[i] != NULL) {
          JsonObject pmObj = pmArr.add<JsonObject>();
          _fillDefinitionToJson(i, pmObj);
          _fillValuestoJson(m_PowermeterDatasPersistance[i], pmObj);
        }
      }
      
      String jsonStr;
      serializeJson(doc, jsonStr);
      
      MD5Builder md5;
      md5.begin();
      md5.add(pwd);
      md5.calculate();
      byte key[16];
      md5.getBytes(key);
      byte iv[16] = {0};
      
      AESLib aesLib;
      uint16_t cipher_len = aesLib.get_cipher64_length(jsonStr.length());
      char* encryptedArr = new char[cipher_len + 1];
      memset(encryptedArr, 0, cipher_len + 1);
      aesLib.encrypt64((const byte*)jsonStr.c_str(), jsonStr.length(), encryptedArr, key, 16, iv);
      String encrypted = String(encryptedArr);
      delete[] encryptedArr;
      
      AsyncWebServerResponse *response = request->beginResponse(200, "application/octet-stream", encrypted);
      response->addHeader("Content-Disposition", "attachment; filename=\"powermeters_backup.enc\"");
      request->send(response);
    });

    p_pWebServer->on("/pmb/config/import", HTTP_POST, [this](AsyncWebServerRequest *request) {
      PWBOARD_DEBUG_MSG(F("POST /pmb/config/import\n"));
      if (!request->hasParam("pwd", true)) {
        PWBOARD_DEBUG_MSG(F("Missing password param\n"));
        request->send(400, "text/plain", "Missing password");
        return;
      }
      String pwd = request->getParam("pwd", true)->value();
      String *encStr = (String*)request->_tempObject;
      if (!encStr) {
        PWBOARD_DEBUG_MSG(F("No file uploaded (encStr is null)\n"));
        request->send(400, "text/plain", "No file uploaded");
        return;
      }
      PWBOARD_DEBUG_MSG(F("File received, length: %d\n"), encStr->length());
      
      MD5Builder md5;
      md5.begin();
      md5.add(pwd);
      md5.calculate();
      byte key[16];
      md5.getBytes(key);
      byte iv[16] = {0};
      
      AESLib aesLib;
      uint16_t cipher_len = encStr->length();
      byte* decryptedArr = new byte[cipher_len + 1];
      memset(decryptedArr, 0, cipher_len + 1);
      uint16_t decrypted_len = aesLib.decrypt64((char*)encStr->c_str(), cipher_len, decryptedArr, key, 16, iv);
      (void)decrypted_len;
      String decrypted = String((char*)decryptedArr);
      delete[] decryptedArr;
      delete encStr;
      request->_tempObject = NULL;
      
      JsonDocument doc;
      DeserializationError err = deserializeJson(doc, decrypted);
      if (err) {
        PWBOARD_DEBUG_MSG(F("JSON deserialize error: %s\n"), err.c_str());
        request->send(400, "text/plain", "Invalid password or corrupted file");
        return;
      }
      PWBOARD_DEBUG_MSG(F("JSON deserialize OK. Restoring...\n"));
      
      JsonObject wifiObj = doc["wifi"];
      if (!wifiObj.isNull()) WIFIMANAGER.importConfigJson(wifiObj);
      
      JsonObject mqttObj = doc["mqtt"];
      if (!mqttObj.isNull()) this->importConfigJson(mqttObj);
      
      JsonArray pmArr = doc["powermeters"];
      if (!pmArr.isNull()) {
        for (int i = 0; i < NBPOWERMETERS; i++) {
          if (m_Powermeters[i] != NULL) {
            _removePowermeter(i);
            m_PowermeterDatasPersistance[i].tag = 0;
            m_PowermeterDatasPersistance[i].ticks = 0;
            m_PowermeterDatasPersistance[i].cumulative = 0;
          }
        }
        for (JsonObject pmDefinition : pmArr) {
          PowermeterDef newDef;
          const char* pmName = pmDefinition["name"];
          strncpy(newDef.name, pmName ? pmName : "Unknown", sizeof(newDef.name) - 1);
          newDef.name[sizeof(newDef.name) - 1] = '\0';
          newDef.maxAmp = pmDefinition["maxAmp"];
          newDef.nbTickByKW = pmDefinition["nbTickByKW"];
          newDef.voltage = pmDefinition["voltage"];
          newDef.dIO = pmDefinition["dIO"];
          
          DDS238Data powerMeterData;
          powerMeterData.ticks = pmDefinition["ticks"];
          powerMeterData.cumulative = pmDefinition["cumulative"];
          powerMeterData.tag = PMBMAGIC;
          
          _addPowermeter(newDef, powerMeterData, true);
        }
        isPersistanceDirty = true;
      }
      
      request->send(200, "text/html", "<b>Configuration restored.</b> Rebooting...<script>setTimeout(()=>window.location.href='/pmb/', 5000);</script>");
      forceRestart = 2000;
    }, [this](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
      if (!index) {
        request->_tempObject = new String();
      }
      String *encStr = (String*)request->_tempObject;
      encStr->concat((const char*)data, len);
    });
  }
  void setup(const char *deviceName, AsyncWebServer *p_pWebServer, fs::FS fs) {
    PWBOARD_DEBUG_MSG(F("setup powermetersBoard\n"));

    // attach set mqtt settings
    setupHandlerSetMqtt(deviceName, p_pWebServer, fs);
    setupHandlerConfig(deviceName, p_pWebServer, fs);

    // Get list of configured powermeter
    setupHandlerGetPowerMeters(deviceName, p_pWebServer, fs);
    setupHandlerUpdatePowerMeter(deviceName, p_pWebServer, fs);
    setupHandlerAddPowerMeter(deviceName, p_pWebServer, fs);
    setupHandlerDeletePowerMeter(deviceName, p_pWebServer, fs);
    m_pPowerMeterDevice = new HALIB_NAMESPACE::HADevice(
        deviceName, "Kila Product", "PowerMeter", "v 0.1");

    // attach AsyncWebSocket
    ws.onEvent([this](AsyncWebSocket *server, AsyncWebSocketClient *client,
                      AwsEventType type, void *arg, uint8_t *data, size_t len) {
      PWBOARD_DEBUG_MSG(F("onEvent /pmb/ws\n"));
      if (type == WS_EVT_CONNECT) {
        PWBOARD_DEBUG_MSG(F("websocket client connected\n"));
        _broadcastPowerMeterInfo(255, client);
        _broadcastMQTTConnectionStatus(client);
        _broadcastWIFIConfig(client);
      } else if (type == WS_EVT_DISCONNECT) {
        PWBOARD_DEBUG_MSG(F("Client disconnected\n"));
      } else if (type == WS_EVT_DATA) {
        AwsFrameInfo *info = (AwsFrameInfo *)arg;
        if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
          JsonDocument doc;
          DeserializationError err = deserializeJson(doc, data, len);
          if (!err && doc["type"] == "req_vars") {
              JsonDocument resp;
              resp["type"] = "sc";
              JsonObject respData = resp["datas"].to<JsonObject>();
              JsonArray vars = doc["vars"].as<JsonArray>();
              for (JsonVariant v : vars) {
                 String key = v.as<String>();
                 respData[key] = this->stringProcessor(key);
              }
              String out;
              serializeJson(resp, out);
              client->text(out);
          }
        }
      }
    });
    p_pWebServer->addHandler(&ws);

    // Redirect /pmb to /pmb/index.htm (exact match to avoid redirect loop for
    // /pmb/...)
    p_pWebServer->on("^\\/pmb$", HTTP_GET, [](AsyncWebServerRequest *request) {
      request->redirect("/pmb/index.htm");
    });

    // attach static web files without Template Processor to save RAM
    p_pWebServer->serveStatic("/pmb/", fs, "/pmb/")
        .setDefaultFile("index.htm")
        .setCacheControl("max-age=86400");

    // 1. Memory Diagnostics (Heap)
    m_pHeapSensor =
        new HAComponentSensor("Free Memory", HALIB_NAMESPACE::SC_NONE,
                              false); // DC_NONE because "Bytes" is generic
    m_pHeapSensor->addProperty(HALIB_NAMESPACE::PROP_UNIT_OF_MEASUREMENT, "B");
    m_pHeapSensor->addProperty(HALIB_NAMESPACE::PROP_ENTITY_CATEGORY,
                               "diagnostic");
    m_pPowerMeterDevice->addComponent(m_pHeapSensor);

    // 2. Fragmentation Diagnostic
    m_pFragSensor = new HAComponentSensor("Memory Fragmentation",
                                          HALIB_NAMESPACE::SC_NONE, false);
    m_pFragSensor->addProperty(HALIB_NAMESPACE::PROP_UNIT_OF_MEASUREMENT, "%");
    m_pFragSensor->addProperty(HALIB_NAMESPACE::PROP_ENTITY_CATEGORY,
                               "diagnostic");
    m_pPowerMeterDevice->addComponent(m_pFragSensor);

    // 3. Diagnostic WiFi (RSSI)
    m_pWifiRssiSensor = new HAComponentSensor(
        "WiFi Signal", HALIB_NAMESPACE::SC_SIGNAL_STRENGTH, false);
    m_pWifiRssiSensor->addProperty(HALIB_NAMESPACE::PROP_UNIT_OF_MEASUREMENT,
                                   "dBm");
    m_pWifiRssiSensor->addProperty(HALIB_NAMESPACE::PROP_ENTITY_CATEGORY,
                                   "diagnostic");
    m_pPowerMeterDevice->addComponent(m_pWifiRssiSensor);

    // 4. Network Status (Text)
    m_pNetworkStateSensor =
        new HALIB_NAMESPACE::HAComponentTextSensor("Network State");
    m_pNetworkStateSensor->addProperty(HALIB_NAMESPACE::PROP_ENTITY_CATEGORY,
                                       "diagnostic");
    m_pPowerMeterDevice->addComponent(m_pNetworkStateSensor);

    // 5. MQTT Status (Text)
    m_pMqttStateSensor =
        new HALIB_NAMESPACE::HAComponentTextSensor("MQTT Status");
    m_pMqttStateSensor->addProperty(HALIB_NAMESPACE::PROP_ENTITY_CATEGORY,
                                    "diagnostic");
    m_pPowerMeterDevice->addComponent(m_pMqttStateSensor);

    restore();

    if ((strlen(m_PowermeterBoardSettings.node_name) != 0) &&
        (0 != strcmp(m_PowermeterBoardSettings.node_name, deviceName))) {
      strcpy(m_PowermeterBoardSettings.node_name, deviceName);
      isPersistanceDirty = true;
    }

    if (strlen(m_PowermeterBoardSettings.mqtt_domain) != 0) {
      m_pPowerMeterDevice->setup(m_PowermeterBoardSettings.mqtt_domain,
                                 m_PowermeterBoardSettings.mqtt_port,
                                 m_PowermeterBoardSettings.mqtt_login,
                                 m_PowermeterBoardSettings.mqtt_pwd);
    };
    PWBOARD_DEBUG_MSG(F("setup END powermetersBoard\n"));
  };
  void restore() {
    PWBOARD_DEBUG_MSG(F("Restore\n"));

    // restore pmb settings
    int tag;
    EEPROMEX.get(m_TagPersistanceIndex, tag);
    if (PMBMAGIC != tag) {
      backup();
    }

    // restore pmb settings
    EEPROMEX.get(m_SettingsPersistanceIndex, m_PowermeterBoardSettings);

    // restore powermeter data
    EEPROMEX.get(m_DataPersistanceIndex, m_PowermeterDatasPersistance);

    // restore powermeters
    PowermeterDef storedPowerMeterDefinitions[NBPOWERMETERS];
    EEPROMEX.get(m_DefinitionPersistanceIndex, storedPowerMeterDefinitions);

    PWBOARD_DEBUG_MSG(F("Restore powermeters\n"));
    // for each powermeters
    for (int i = 0; i < NBPOWERMETERS; i++) {
      // if powermeter is defined
      if (storedPowerMeterDefinitions[i].dIO != 255) {
        _addPowermeter(storedPowerMeterDefinitions[i],
                       m_PowermeterDatasPersistance[i], false);
      }
    }

    ws.enable(true);
  };
  void backup() {
    PWBOARD_DEBUG_MSG(F("Backup\n"));

    // store pmb settings
    EEPROMEX.put(m_SettingsPersistanceIndex, m_PowermeterBoardSettings);

    // store powermeter data
    EEPROMEX.put(m_DataPersistanceIndex, m_PowermeterDatasPersistance);

    // store powermeters
    PowermeterDef storedPowerMeterDefinitions[NBPOWERMETERS];
    for (int i = 0; i < NBPOWERMETERS; i++) {
      if (m_Powermeters[i] != NULL) {
        _printPowermeterDef(m_Powermeters[i]->getDefinition());
        PWBOARD_DEBUG_MSG(F("Backup at %d\n"), i);
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
  void loop(AsyncWebSocket *ws) {
    if (m_isSuspended)
      return;

    if (isPersistanceDirty) {
      backup();
      isPersistanceDirty = false;
    }
    static unsigned long lastDiagSend = 0;
    if (millis() - lastDiagSend > 30000) {
      lastDiagSend = millis();

      if (m_pPowerMeterDevice && m_pPowerMeterDevice->isMqttconnected()) {
        m_pHeapSensor->setValue(
            MEMORYDEBUGGER.stringProcessor("MEM_FREE").toInt());
        m_pFragSensor->setValue(
            MEMORYDEBUGGER.stringProcessor("MEM_FRAG").toInt());
        // m_pMUsedSensor->setValue(
        //     MEMORYDEBUGGER.stringProcessor("MEM_LEAK").toInt());
        long rssi = WiFi.RSSI();
        if (rssi < 0 && rssi > -110) {
          m_pWifiRssiSensor->setValue((float)rssi);
        } else {
          // If garbage, don't set value or set to 0
          m_pWifiRssiSensor->setValue(0.0f);
        }

        m_pNetworkStateSensor->setValue(
            getBlinkerStateString(g_currentBlinkerState));
        m_pMqttStateSensor->setValue(isMqttconnected ? "Connected"
                                                     : "Disconnected");
      };
    }
    static unsigned long lastDebugSend = 0;
    if ((millis() - lastDebugSend > 5000) && (ws)) {
      lastDebugSend = millis();
      _sendDebugData(ws);
    }
    if (lastWifiStatus != WiFi.status()) {

      lastWifiStatus = WiFi.status();
      _broadcastWIFIStatus(NULL);
    }

    if (m_pPowerMeterDevice) {
      m_pPowerMeterDevice->loop(WiFi.status());
      // notify MQTT connection status change
      if (m_pPowerMeterDevice->isMqttconnected() != isMqttconnected) {
        isMqttconnected = !isMqttconnected;
        _broadcastMQTTConnectionStatus(NULL);
      }
    }
    for (int i = 0; i < NBPOWERMETERS; i++) {
      if (m_Powermeters[i] != NULL) {
        m_Powermeters[i]->loop();
      }
    }
    if (forceRestartAfterPersistance && !isPersistanceDirty) {
      EEPROMEX.commit();
      ESP.restart();
    }
    if (forceRestart == 0) {
      EEPROMEX.commit();
      ESP.restart();
    }
    if (forceRestart > 0) {
      forceRestart--;
      // return;
    }
  };

  void suspend(boolean suspend) {
    PWBOARD_DEBUG_MSG(F("suspend %s\n"), (suspend) ? "true" : "false");
    m_isSuspended = suspend;

    for (int i = 0; i < NBPOWERMETERS; i++) {
      if (m_Powermeters[i] != NULL) {
        m_Powermeters[i]->suspend(suspend);
      }
    }

    if (m_isSuspended) {
      ws.textAll("OTA Update Started");
      ws.enable(false);
      ws.closeAll();
    } else {
      ws.enable(true);
    }
  }; //@TODO
  String stringProcessor(const String &variable) {
    // PWBOARD_DEBUG_MSG(F("stringProcessor %s\n"), variable.c_str());
    if (variable == "NODENAME") {
      if (m_pPowerMeterDevice)
        return String(m_pPowerMeterDevice->getName());
      else
        return String(m_PowermeterBoardSettings.node_name);
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
      return (m_pPowerMeterDevice && m_pPowerMeterDevice->isMqttconnected())
                 ? "Connected"
                 : "Disconnected";

    return "";
  };
  boolean isMqttConnected() { return isMqttconnected; };

private:
  void _fillValuestoJson(DDS238Data values, JsonObject &obj) {
    char buffer[25];
    dtostrf(values.cumulative, 2, 1, buffer);
    // PWBOARD_DEBUG_MSG("_fillPMDatatoJson cumul float %0.1lf\n",
    // m_PowermeterDatasPersistance[index].cumulative);
    obj["cumulative"] = buffer;
    obj["ticks"] = values.ticks;
  }
  void _fillPMDatatoJson(int index, JsonObject &obj) {
    _fillValuestoJson(m_PowermeterDatasPersistance[index], obj);
    if (NULL != m_Powermeters[index]) {
      obj["ip"] = m_Powermeters[index]->getInstantPower();
    }
  }
  void _fillDefinitionToJson(int index, JsonObject &obj) {
    if (NULL != m_Powermeters[index]) {
      _fillDefinitionToJson(m_Powermeters[index]->getDefinition(), obj);
    }
  }
  void _fillDefinitionToJson(PowermeterDef powermeterDef, JsonObject &obj) {
    // allocate the memory for the document
    obj["dIO"] = powermeterDef.dIO;
    obj["name"] = powermeterDef.name;
    obj["nbTickByKW"] = powermeterDef.nbTickByKW;
    obj["voltage"] = powermeterDef.voltage;
    obj["maxAmp"] = powermeterDef.maxAmp;
  }
  String _getPowermeterAsJsonString(PowermeterDef powermeterDef,
                                    DDS238Data powermeterValues) {
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
    _fillValuestoJson(powermeterValues, obj);
    //_fillPMDatatoJson(powermeterDef.dIO, obj);
    serializeJson(obj, response);
    return response;
  }

  void exportConfigJson(JsonObject &obj) {
    obj["node_name"] = m_PowermeterBoardSettings.node_name;
    obj["mqtt_domain"] = m_PowermeterBoardSettings.mqtt_domain;
    obj["mqtt_port"] = m_PowermeterBoardSettings.mqtt_port;
    obj["mqtt_login"] = m_PowermeterBoardSettings.mqtt_login;
    obj["mqtt_pwd"] = m_PowermeterBoardSettings.mqtt_pwd;
  }

  void importConfigJson(JsonObject &obj) {
    if (!obj["node_name"].isNull()) strncpy(m_PowermeterBoardSettings.node_name, obj["node_name"], sizeof(m_PowermeterBoardSettings.node_name) - 1);
    if (!obj["mqtt_domain"].isNull()) strncpy(m_PowermeterBoardSettings.mqtt_domain, obj["mqtt_domain"], sizeof(m_PowermeterBoardSettings.mqtt_domain) - 1);
    if (!obj["mqtt_port"].isNull()) m_PowermeterBoardSettings.mqtt_port = obj["mqtt_port"];
    if (!obj["mqtt_login"].isNull()) strncpy(m_PowermeterBoardSettings.mqtt_login, obj["mqtt_login"], sizeof(m_PowermeterBoardSettings.mqtt_login) - 1);
    if (!obj["mqtt_pwd"].isNull()) strncpy(m_PowermeterBoardSettings.mqtt_pwd, obj["mqtt_pwd"], sizeof(m_PowermeterBoardSettings.mqtt_pwd) - 1);
    
    m_PowermeterBoardSettings.node_name[sizeof(m_PowermeterBoardSettings.node_name) - 1] = '\0';
    m_PowermeterBoardSettings.mqtt_domain[sizeof(m_PowermeterBoardSettings.mqtt_domain) - 1] = '\0';
    m_PowermeterBoardSettings.mqtt_login[sizeof(m_PowermeterBoardSettings.mqtt_login) - 1] = '\0';
    m_PowermeterBoardSettings.mqtt_pwd[sizeof(m_PowermeterBoardSettings.mqtt_pwd) - 1] = '\0';
    
    m_PowermeterBoardSettings.tag = PMBMAGIC;
    isPersistanceDirty = true;
  }

  void _printPowermeterDef(PowermeterDef powermeterDef) {
    PWBOARD_DEBUG_MSG(F("pm dIO %d\n"), powermeterDef.dIO);
    PWBOARD_DEBUG_MSG(F("pm name %s\n"), powermeterDef.name);
    PWBOARD_DEBUG_MSG(F("pm nbTickByKW %d\n"), powermeterDef.nbTickByKW);
    PWBOARD_DEBUG_MSG(F("pm voltage %d\n"), powermeterDef.voltage);
    PWBOARD_DEBUG_MSG(F("pm maxAmp %d\n"), powermeterDef.maxAmp);
  }
  void _printPowermeterData(DDS238Data powermeterData) {
    PWBOARD_DEBUG_MSG(F("pm ticks %d\n"), powermeterData.ticks);
    PWBOARD_DEBUG_MSG(F("pm cumulative %f\n"), powermeterData.cumulative);
  }
  boolean _updatePowermeters(PowermeterDef powermeterDef,
                             DDS238Data powermeterValues) {
    PWBOARD_DEBUG_MSG(F("_updatePowermeters\n"));
    // uint32_t freeBefore = ESP.getFreeHeap();
    // PWBOARD_DEBUG_MSG(" free heap %d\n", freeBefore);
    _printPowermeterDef(powermeterDef);
    uint8 powermeterIndex = BoardIOToPowermeterIndex[powermeterDef.dIO];
    PWBOARD_DEBUG_MSG(F("powermeterIndex %d\n"), powermeterIndex);
    Powermeter *pPowermeter = this->m_Powermeters[powermeterIndex];
    PWBOARD_DEBUG_MSG(F("powermeterIndex %sFound\n"),
                      (NULL == pPowermeter) ? "Not " : "");
    if (NULL != pPowermeter) {
      // mark new data with previous
      powermeterValues.tag = m_PowermeterDatasPersistance[powermeterIndex].tag;

      _removePowermeter(powermeterIndex);
      _addPowermeter(powermeterDef, powermeterValues, true);
      this->isPersistanceDirty = true;
      PWBOARD_DEBUG_MSG(F("_editPowermetersEND\n"));
      return true;
    } else {
      PWBOARD_DEBUG_MSG(F("_editPowermetersEND\n"));
      return false;
    }
  }
  boolean _removePowermeter(uint8 powermeterIndex) {
    Powermeter *pPowermeter = this->m_Powermeters[powermeterIndex];
    if (NULL != pPowermeter) {
      this->m_Powermeters[powermeterIndex] = NULL;
      delete pPowermeter;
      return true;
    }
    return false;
  }
  boolean _addPowermeter(PowermeterDef powermeterDef,
                         DDS238Data powermeterValues, boolean isNew) {
    // uint32_t freeBefore = ESP.getFreeHeap();
    // PWBOARD_DEBUG_MSG(" free heap %d\n", freeBefore);
    _printPowermeterDef(powermeterDef);
    _printPowermeterData(powermeterValues);

    uint8 powermeterIndex = BoardIOToPowermeterIndex[powermeterDef.dIO];
    Powermeter *pPowermeter = this->m_Powermeters[powermeterIndex];

    if (NULL == pPowermeter) {
      // if new powermeter store new powermeter data
      if (isNew)
        this->m_PowermeterDatasPersistance[powermeterIndex] = powermeterValues;

      // allocate a PowerMeter object
      this->m_Powermeters[powermeterIndex] = new Powermeter(
          powermeterDef, powermeterValues, m_pPowerMeterDevice,
          // create lambda as persistance callback
          [this, powermeterIndex](DDS238Data data) {
            this->m_PowermeterDatasPersistance[powermeterIndex] = data;
            // @TODO analyse if broadcast should be done async in main loop
            this->_broadcastPowerMeterData(powermeterIndex, NULL);
            this->isPersistanceDirty = true;
          },
          sizeof(DDS238Data) * powermeterIndex);
      // PWBOARD_DEBUG_MSG("Add at %d\n",powermeterIndex);
      this->isPersistanceDirty = true;

      PWBOARD_DEBUG_MSG(
          F("add %s powermeter %d %d\n"), isNew ? "new" : "restored",
          BoardIOToPowermeterIndex[powermeterDef.dIO] + 1, powermeterDef.dIO);
      this->_broadcastPowerMeterInfo(powermeterIndex, NULL);

      return true;
    } else {
      PWBOARD_DEBUG_MSG(F("powermeter %d %d already exist\n"),
                        BoardIOToPowermeterIndex[powermeterDef.dIO] + 1,
                        powermeterDef.dIO);
      return false;
    }
  }

  void _broadcastPowerMeterRemoved(uint8 index, AsyncWebSocketClient *client) {
    PWBOARD_DEBUG_MSG(F("_broadcastPowerMeterInfo %d to %s\n"), index,
                      (NULL == client) ? "ALL" : "client");
    JsonDocument doc;
    doc["type"] = "pmd";
    doc["datas"] = index;

    String response;
    doc.shrinkToFit(); // optional
    serializeJson(doc, response);
    if (NULL != client) {
      client->text(response);
    } else {
      ws.textAll(response);
    }
  }
  void _broadcastPowerMeterInfo(int index, AsyncWebSocketClient *client) {
    PWBOARD_DEBUG_MSG(F("_broadcastPowerMeterInfo %d to %s\n"), index,
                      (NULL == client) ? "ALL" : "client");
    JsonDocument doc;
    doc["type"] = "pdu";

    for (int i = 0; i < NBPOWERMETERS; i++) {
      if ((NULL != this->m_Powermeters[i]) &&
          ((i == index) || (index == 255))) {
        PWBOARD_DEBUG_MSG(F("_broadcastPowerMeterInfo adding %d \n"), i);
        JsonObject data = doc["datas"].add<JsonObject>();
        _fillDefinitionToJson(i, data);
        _fillPMDatatoJson(i, data);
        if (index != 255)
          break;
      }
    }
    if (doc["datas"].is<JsonArray>()) {
      String response;
      doc.shrinkToFit(); // optional
      serializeJson(doc, response);
      if (NULL != client) {
        client->text(response);
      } else {
        ws.textAll(response);
      }
    }
    // const size_t CAPACITY = JSON_OBJECT_SIZE(7);
    // int nbElement = 0;

    // if (index == 255)
    // {
    //     for (int i = 0; i < NBPOWERMETERS; i++)
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

    // for (int i = 0; i < NBPOWERMETERS; i++)
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
  void _broadcastPowerMeterData(uint8 index, AsyncWebSocketClient *client) {
    PWBOARD_DEBUG_MSG(F("_broadcastPowerMeterData %d to %s\n"), index,
                      (NULL == client) ? "ALL" : "client");

    JsonDocument doc;
    doc["type"] = "pdu";
    for (int i = 0; i < NBPOWERMETERS; i++) {
      if ((NULL != this->m_Powermeters[i]) &&
          ((i == index) || (index == 255))) {
        JsonObject data = doc["datas"].add<JsonObject>();
        data["dIO"] = PowermeterIndexToBoardIO[i];
        _fillPMDatatoJson(i, data);
        if (index != 255)
          break;
      }
    }
    // post only if datas present
    if (doc["datas"].is<JsonArray>()) {
      String response;
      doc.shrinkToFit(); // optional
      serializeJson(doc, response);
      if (NULL != client) {
        client->text(response);
      } else {
        ws.textAll(response);
      }
    }

    // const size_t CAPACITY = JSON_OBJECT_SIZE(4);
    // int nbElement = 0;

    // if (index == 255)
    // {
    //     for (int i = 0; i < NBPOWERMETERS; i++)
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

    // for (int i = 0; i < NBPOWERMETERS; i++)
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
  const char *_wl_status_to_string(wl_status_t status) {
    switch (status) {
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
  void _broadcastMQTTConnectionStatus(AsyncWebSocketClient *client) {
    PWBOARD_DEBUG_MSG(F("_broadcastMQTTConnectionStatus %sconnected\n"),
                      (isMqttconnected) ? "" : "dis");
    JsonDocument doc;
    doc["type"] = "mcs";
    doc["datas"] = isMqttconnected;
    String response;
    doc.shrinkToFit(); // optional
    serializeJson(doc, response);
    if (NULL != client) {
      client->text(response);
    } else {
      ws.textAll(response);
    }

    // allocate the memory for the document
    // DynamicJsonDocument doc(10);
    // // create a variant
    // JsonVariant mqttStatusEvent = doc.to<JsonVariant>();
    // mqttStatusEvent.set(isMqttconnected);

    // _broadcastEvent("mcs", mqttStatusEvent, client);
  }
  void _broadcastWIFIStatus(AsyncWebSocketClient *client) {
    PWBOARD_DEBUG_MSG(F("_broadcastWIFIStatus %s to %s\n"),
                      _wl_status_to_string(WiFi.status()),
                      (NULL == client) ? "ALL" : "client");
    JsonDocument doc;
    doc["type"] = "ws";
    doc["datas"] = WiFi.status();
    String response;
    doc.shrinkToFit(); // optional
    serializeJson(doc, response);
    if (NULL != client) {
      client->text(response);
    } else {
      ws.textAll(response);
    }

    // // allocate the memory for the document
    // DynamicJsonDocument doc(10);
    // // create a variant
    // JsonVariant statusEvent = doc.to<JsonVariant>();
    // statusEvent.set(WiFi.status());
    // _broadcastEvent("ws", statusEvent, client);
  }
  void _broadcastWIFIConfig(AsyncWebSocketClient *client) {
    PWBOARD_DEBUG_MSG(F("_broadcastWIFIConfig to %s\n"),
                      (NULL == client) ? "ALL" : "client");
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
    if (NULL != client) {
      client->text(response);
    } else {
      ws.textAll(response);
    }
  }

  void _sendDebugData(AsyncWebSocket *ws) {
    if (ws == nullptr || ws->count() == 0)
      return;
    char jsonBuffer[200];
    for (int i = 0; i < NBPOWERMETERS; i++) {
      Powermeter *pPowerMeter = this->m_Powermeters[i];
      if (pPowerMeter != nullptr) {
        memset(jsonBuffer, 0, sizeof(jsonBuffer));
        if (0 != pPowerMeter->toJsonDebug(jsonBuffer, sizeof(jsonBuffer))) {
          ws->textAll(jsonBuffer);
        }
      }
    }
  }
  PowermeterBoardSettings m_PowermeterBoardSettings;

  HADevice *m_pPowerMeterDevice;
  // HAComponent *m_pRebootComponent;
  Powermeter *m_Powermeters[NBPOWERMETERS];
  DDS238Data m_PowermeterDatasPersistance[NBPOWERMETERS];
  // FS &m_FileSystem;
  uint8_t lastWifiStatus = 0;

  int m_TagPersistanceIndex;
  int m_SettingsPersistanceIndex;
  int m_DataPersistanceIndex;
  int m_DefinitionPersistanceIndex;

  boolean isPersistanceDirty = false;
  boolean isMqttconnected = false;
  boolean m_isSuspended = false;

  HAComponentSensor *m_pHeapSensor;
  HAComponentSensor *m_pMinHeapSensor;
  HAComponentSensor *m_pFragSensor;
  // HAComponentSensor *m_pMUsedSensor;
  HAComponentSensor *m_pWifiRssiSensor;
  HALIB_NAMESPACE::HAComponentTextSensor *m_pNetworkStateSensor;
  HALIB_NAMESPACE::HAComponentTextSensor *m_pMqttStateSensor;
};

PowermeterBoard POWERMETERBOARD;

#endif