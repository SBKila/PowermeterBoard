#ifndef POWERMETER_H
#define POWERMETER_H

#pragma once

struct PowermeterDef
{
    uint8_t dIO = 255;
    char name[25] = {};
    int nbTickByKW = 0;
    int voltage = 0;
    int maxAmp = 0;
};

class Powermeter
{
public:
    Powermeter(PowermeterDef p_Definition, HADevice *p_pHADevice, HA_DDS238_PERSISTENT_FUNCTION p_PersistenceFunction)
    {
        m_Definition = p_Definition;

        m_pPowermeterAdapter = new HAAdapterDDS238(
            p_Definition.name,
            p_Definition.dIO,
            p_Definition.nbTickByKW,
            p_Definition.voltage,
            p_Definition.maxAmp,
            p_PersistenceFunction);
    }

    PowermeterDef getDefinition()
    {
        return m_Definition;
    }

    void loop()
    {
        m_pPowermeterAdapter->loop();
    };

private:
    PowermeterDef m_Definition;
    HAAdapterDDS238 *m_pPowermeterAdapter;
};

#endif