#ifndef EEPROMEX_H
#define EEPROMEX_H

#define NO_GLOBAL_EEPROM
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

private:
    int memSize = 0;
};
extern EEPROMEXClass EEPROMEX;
#endif