#ifndef REBOOTTRACKER_H
#define REBOOTTRACKER_H

#include <Arduino.h>
#include <EEPROMEX.h>

class RebootTrackerClass
{
private:
    int m_NbBoot = 0;

    // Variables for crash/reboot diagnostics
    String m_LastResetReason;
    String m_LastResetInfo;

public:
    int setupPersistance()
    {
        return 0;
    }

    void setup()
    {
        // --- 1. Capture Reboot Diagnostics ---
        // These functions read internal ESP8266 registers.
        m_LastResetReason = ESP.getResetReason();
        m_LastResetInfo = ESP.getResetInfo();

        // --- 2. Check RTC Memory Probe (Out Of Memory detection) ---
        uint32_t rtcData[2];
        if (ESP.rtcUserMemoryRead(100, rtcData, sizeof(rtcData))) {
            if (rtcData[0] == 0x12345678) {
                m_LastResetInfo += " [OOM Probe: MinHeap=" + String(rtcData[1]) + " B]";
                // Clear the RTC magic block to avoid false positives on next reboot
                rtcData[0] = 0;
                ESP.rtcUserMemoryWrite(100, rtcData, sizeof(rtcData));
            }
        }
    }

    // --- NOUVELLE FONCTION ---
    void saveToFS(fs::FS &fs)
    {
        String lines[10];
        size_t lineCount = 0;

        // 1. Read existing file to get last boot number and keep last 9 lines
        if (fs.exists("/reboot_history.log"))
        {
            File f = fs.open("/reboot_history.log", "r");
            if (f)
            {
                while (f.available())
                {
                    String line = f.readStringUntil('\n');
                    line.trim();
                    if (line.length() == 0)
                        continue;

                    // Always extract boot number to keep sequence correct
                    if (line.startsWith("Boot #"))
                    {
                        int sepIndex = line.indexOf(':');
                        if (sepIndex > 6)
                        {
                            int readBoot = line.substring(6, sepIndex).toInt();
                            if (readBoot > m_NbBoot)
                                m_NbBoot = readBoot;
                        }
                    }

                    // Filter: Do not keep "External System" logs in history
                    if (line.indexOf("Reason=[External System]") >= 0)
                        continue;

                    lines[lineCount % 10] = line;
                    lineCount++;
                }
                f.close();
            }
        }

        m_NbBoot++;

        // 2. Rewrite file with last 9 lines + new one (Total 10 max)
        File f = fs.open("/reboot_history.log", "w");
        if (f)
        {
            // Calculate which lines to keep from the buffer
            size_t linesToKeep = (lineCount < 9) ? lineCount : 9;
            size_t startIdx = (lineCount > 9) ? (lineCount - 9) : 0;

            for (size_t i = 0; i < linesToKeep; i++)
            {
                f.println(lines[(startIdx + i) % 10]);
            }

            f.printf("Boot #%d: Reason=[%s] Info=[%s]\n",
                     m_NbBoot,
                     m_LastResetReason.c_str(),
                     m_LastResetInfo.c_str());
            f.close();
        }
    }
    String stringProcessor(const String &var)
    {
        // Boot Diagnostics
        if (var == "BOOT")
            return String(m_NbBoot);
        if (var == "RESET_REASON")
            return m_LastResetReason;
        if (var == "RESET_INFO")
            return m_LastResetInfo;

        return String();
    }
};

// Use 'extern' here to allow including this header in multiple files without errors.
// You must declare the actual instance in your main.cpp (see below).
extern RebootTrackerClass REBOOTTRACKER;

#endif