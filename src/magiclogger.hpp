#include "AsyncWebSocket.h"
#ifndef LOGGER_H
#define LOGGER_H

#pragma once


class MagicLogger
{
    char logString[256];
    AsyncWebSocket* m_pWs;
    public:
    void setup(AsyncWebSocket* p_pWs) {
        m_pWs=p_pWs;
    };
    void print(...) {
        snprintf(logString,sizeof(logString), magiclogger); \
        m_pWs->printfAll("{ \"debug\" : \"[MAIN] %s\"}", logString);

    }
};

MagicLogger WSLOGGER;

#endif