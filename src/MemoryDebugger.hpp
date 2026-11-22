#ifndef MEMORYDEBUGGER_H
#define MEMORYDEBUGGER_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

class MemoryDebugger
{
private:
  unsigned long _lastSendTime;
  unsigned long _sendInterval;
  unsigned long _lastSampleTime;

  // Baseline memory (captured at boot)
  uint32_t _baselineFreeHeap;

  // Statistics for the current interval
  uint64_t _sumFreeHeap;
  uint64_t _sumFrag;
  uint32_t _minFreeHeap;
  uint32_t _loopCounter;

  // Last calculated averages (stored for the Processor)
  uint32_t _lastAvgFree;
  uint32_t _lastAvgFrag;
  uint32_t _lastMinFree;

  // Flash Diagnostics
  uint32_t _realFlashSize;
  uint32_t _ideFlashSize;
  bool _flashConfigError;

public:
  MemoryDebugger()
  {
    _lastSendTime = 0;
    _sendInterval = 30000; // Default analysis/reporting interval: 30 seconds
    _lastSampleTime = 0;
    _baselineFreeHeap = 0;

    // Initialize stats placeholders
    _lastAvgFree = 0;
    _lastAvgFrag = 0;
    _lastMinFree = 0;

    _realFlashSize = 0;
    _ideFlashSize = 0;
    _flashConfigError = false;

    resetStats();
  }

  // Call this at the VERY BEGINNING of setup() to capture baseline
  void begin(unsigned long intervalMs = 30000)
  {
    _baselineFreeHeap = ESP.getFreeHeap(); // Capture baseline
    _sendInterval = intervalMs;
    _lastSendTime = millis();

    // Flash Configuration Check
    _realFlashSize = ESP.getFlashChipRealSize();
    _ideFlashSize = ESP.getFlashChipSize();
    _flashConfigError = (_realFlashSize < _ideFlashSize);

    resetStats();
  }
  // Modified loop: Takes WebSocket as an argument dynamically
  void loop(AsyncWebSocket *ws)
  {
    unsigned long currentMillis = millis();

    // 1. DATA ACQUISITION (throttled to prevent WDT)
    if (currentMillis - _lastSampleTime >= 200)
    {
      _lastSampleTime = currentMillis;
      uint32_t currentFree = ESP.getFreeHeap();
      uint8_t currentFrag = ESP.getHeapFragmentation();

      _sumFreeHeap += currentFree;
      _sumFrag += currentFrag;
      _loopCounter++;

      if (currentFree < _minFreeHeap)
      {
        _minFreeHeap = currentFree;

        // Save real-time minimum heap to RTC Memory (survives crashes)
        uint32_t rtcData[2];
        rtcData[0] = 0x12345678; // Magic signature
        rtcData[1] = _minFreeHeap;
        ESP.rtcUserMemoryWrite(100, rtcData, sizeof(rtcData));
      }
    }

    // 2. DATA SENDING & STORING
    if (currentMillis - _lastSendTime >= _sendInterval)
    {

      // Save stats for the StringProcessor
      if (_loopCounter > 0)
      {
        _lastAvgFree = (uint32_t)(_sumFreeHeap / _loopCounter);
        _lastAvgFrag = (uint8_t)(_sumFrag / _loopCounter);
        _lastMinFree = _minFreeHeap;
      }

      // Send via WebSocket only if provided and ready
      if (ws != nullptr && _loopCounter > 0)
      {
        sendDebugData(ws);
      }

      _lastSendTime = currentMillis;
      resetStats();
    }
  }

  // Processor method for ESPAsyncWebServer templating
  // Usage: %MEM_FREE%, %MEM_MIN%, %MEM_FRAG%, %MEM_LEAK%
  String stringProcessor(const String &var)
  {
    if (var == "MEM_FREE")
    {
      return String(ESP.getFreeHeap()); // Instant value for page load
    }
    else if (var == "MEM_AVG")
    {
      return String(_lastAvgFree);
    }
    else if (var == "MEM_MIN")
    {
      return String(_lastMinFree > 0 ? _lastMinFree : ESP.getFreeHeap());
    }
    else if (var == "MEM_FRAG")
    {
      return String(ESP.getHeapFragmentation());
    }
    else if (var == "MEM_LEAK")
    {
      // Positive value means we lost memory since boot
      long leak = _baselineFreeHeap - ESP.getFreeHeap();
      return String(leak);
    }
    else if (var == "MEM_BASE")
    {
      return String(_baselineFreeHeap);
    }
    // Flash Diagnostics
    else if (var == "FLASH_REAL_SIZE")
    {
      return String(_realFlashSize);
    }
    else if (var == "FLASH_IDE_SIZE")
    {
      return String(_ideFlashSize);
    }
    else if (var == "FLASH_STATUS")
    {
      if (_flashConfigError)
        return String(F("CRITICAL ERROR: Real Flash < IDE Config! Partition overlap risk."));
      else
        return String(F("OK"));
    }
    return "";
  }

private:
  void resetStats()
  {
    _sumFreeHeap = 0;
    _sumFrag = 0;
    _loopCounter = 0;
    _minFreeHeap = 0xFFFFFFFF;
  }

  void sendDebugData(AsyncWebSocket *ws)
  {
    // JSON Format:
    char jsonBuffer[200];
    long leak = _baselineFreeHeap - _lastAvgFree;

    snprintf_P(jsonBuffer, sizeof(jsonBuffer),
               PSTR("{\"type\":\"mem\",\"avg\":%u,\"min\":%u,\"frag\":%u,\"leak\":%ld,\"base\":%u,\"loops\":%u}"),
               _lastAvgFree,
               _lastMinFree,
               _lastAvgFrag,
               leak,
               _baselineFreeHeap,
               _loopCounter);

    ws->textAll(jsonBuffer);
    // free(jsonBuffer);
  }
};
MemoryDebugger MEMORYDEBUGGER;
#endif