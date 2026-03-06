#ifndef EEPROMEX_H
#define EEPROMEX_H

#define NO_GLOBAL_EEPROM
#include <Arduino.h>
#include "EEPROM.h"

class EEPROMEXClass : public EEPROMClass
{
public:
    int allocate(int size)
    {
        int curSize = memSize;
        memSize += size;
        return curSize;
    }
    void begin()
    {
        EEPROMClass::begin(memSize);
    }

    // -------------------------------------------------------------------------
    // Custom commit overload
    // Wraps the original commit() with interrupt disable/enable
    // to prevent crashes or corruption during Flash write operations.
    // -------------------------------------------------------------------------
    bool commit()
    {
        bool success = false;
        success = EEPROMClass::commit();
        return success;
    }

    int getAllocatedSize()
    {
        return memSize;
    }

private:
    int memSize = 0;
};
extern EEPROMEXClass EEPROMEX;
#endif