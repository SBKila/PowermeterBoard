#include "myDebugLogger.h"

// Only compile this implementation if Debug is enabled
#if defined(ENABLE_DEBUG)

#include <stdio.h>  // Required for vsnprintf
#include <stdlib.h> // Required for malloc and free

// Forward declaration of internal helper (private to this file)
void _internalLog(const char *module, const char *format, va_list args);

// -------------------------------------------------------------------------
// Overload 1: Handles standard strings stored in RAM (const char*)
// -------------------------------------------------------------------------
void logDebug(const char *module, const char *format, ...)
{
    va_list arg;
    va_start(arg, format);
    _internalLog(module, format, arg);
    va_end(arg);
}

// -------------------------------------------------------------------------
// Overload 2: Handles strings stored in FLASH memory (F() macro)
// -------------------------------------------------------------------------
void logDebug(const char *module, const __FlashStringHelper *format, ...)
{
    va_list arg;
    va_start(arg, format);

    // Cast Flash string pointer to a readable pointer
    PGM_P p = reinterpret_cast<PGM_P>(format);
    size_t n = strlen_P(p);

    // Allocate buffer for the format string
    char *buffer = (char *)malloc(n + 1);

    if (buffer)
    {
        strcpy_P(buffer, p);
        _internalLog(module, buffer, arg);
        free(buffer);
    }
    else
    {
        MY_DEBUG_ESP_PORT.print(F("[ERROR] Log buffer allocation failed"));
    }

    va_end(arg);
}

// -------------------------------------------------------------------------
// Internal Logic: Formats and prints the message SAFELY
// -------------------------------------------------------------------------
void _internalLog(const char *module, const char *format, va_list args)
{
    // 1. Calculate readable time (uptime)
    unsigned long current_millis = millis();
    unsigned long seconds = current_millis / 1000;
    unsigned long minutes = seconds / 60;
    unsigned long hours = minutes / 60;

    // 2. Print Formatted Timestamp [HH:MM:SS.ms] and Module DIRECTLY
    // This avoids buffering the prefix, saving RAM.
    MY_DEBUG_ESP_PORT.printf("[%02lu:%02lu:%02lu.%03lu] [%s] ",
                             hours,
                             minutes % 60,
                             seconds % 60,
                             current_millis % 1000,
                             module);

    // 3. Determine EXACT size needed for the message
    va_list args_copy;
    va_copy(args_copy, args);
    // vsnprintf with NULL/0 returns the length that WOULD have been written
    int len = vsnprintf(NULL, 0, format, args_copy);
    va_end(args_copy);

    // 4. Safety Check: If corruption creates a massive string (e.g. > 1KB), truncate it.
    // This prevents the "Huge Number" bug from crashing the Allocator.
    if (len < 0)
        return; // Encoding error
    if (len > 512)
        len = 512; // Cap max debug line length to 512 bytes

    // 5. Allocate exactly what is needed (+1 for null terminator)
    char *buffer = (char *)malloc(len + 1);

    if (buffer)
    {
        vsnprintf(buffer, len + 1, format, args);
        MY_DEBUG_ESP_PORT.print(buffer);
        free(buffer);
    }
    else
    {
        // Fallback if heap is full
        MY_DEBUG_ESP_PORT.print(F(" [LOG ERR: Out of RAM]"));
    }

    // 6. Add Newline? (Optional: Your original code commented this out,
    //    but usually debug logs need a newline at the end)
    // MY_DEBUG_ESP_PORT.println();
}

#endif // End ENABLE_DEBUG