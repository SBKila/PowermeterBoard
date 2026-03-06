#ifndef myLogger_h
#define myLogger_h

#include <Arduino.h>
#include <stdarg.h>

// Define default port if not already defined
#ifndef MY_DEBUG_ESP_PORT
#define MY_DEBUG_ESP_PORT Serial
#endif

// Global Debug Toggle
#if defined(ENABLE_DEBUG)

#define DEBUG_INIT() MY_DEBUG_ESP_PORT.begin(MONITOR_SPEED)

// Variadic macro to support F() and variable arguments
#define DEBUG_MSG(module, fmt, ...) logDebug(module, fmt, ##__VA_ARGS__)

// -------------------------------------------------------------------------
// Function Prototypes (Promises)
// -------------------------------------------------------------------------
#ifdef __cplusplus
extern "C"
{
#endif
    // Overload 1: Handles standard strings stored in RAM (const char*)
    void logDebug(const char *module, const char *format, ...);
#ifdef __cplusplus
} // Fin du extern "C"
// Overload 2: Handles strings stored in FLASH memory (F() macro)
void logDebug(const char *module, const __FlashStringHelper *format, ...);
#endif
#else
// -------------------------------------------------------------------------
// Production Mode: All macros compile to nothing
// -------------------------------------------------------------------------
#define DEBUG_INIT(x)
#define DEBUG_MSG(...)
#endif

// -------------------------------------------------------------------------
// Module Specific Macros
// -------------------------------------------------------------------------

#endif