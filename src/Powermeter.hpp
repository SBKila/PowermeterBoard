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
    static const uint8_t PowermeterToBoardIO[10] = {PM1,PM2,PM3,PM4,PM5,PM6,PM7,PM8,PM9,PM10};
class Powermeter
{
public:
    Powermeter(PowermeterDef p_Definition, HADevice *p_pHADevice, HA_DDS238_PERSISTENT_FUNCTION p_PersistenceFunction)
    {
        // keep powermeter definition 
        m_Definition = p_Definition;

        // create HomeAssistant Adapter
        m_pPowermeterAdapter = new HAAdapterDDS238(
            p_Definition.name,
            PowermeterToBoardIO[p_Definition.dIO-1],
            p_Definition.nbTickByKW,
            p_Definition.voltage,
            p_Definition.maxAmp,
            p_PersistenceFunction);

        // setup HomeAssistant Adapter
        m_pPowermeterAdapter->setup();

        // link HomeAssistant Adapter to device
        m_pPowermeterAdapter->setDevice(p_pHADevice);
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