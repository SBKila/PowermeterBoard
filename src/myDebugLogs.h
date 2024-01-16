#ifndef myLogger_h
#define myLogger_h

#if defined(MY_DEBUG_ESP_PORT)
#define DEBUG_INIT(x) MY_DEBUG_ESP_PORT.begin(x)
#define DEBUG_MSG(...) logDebug(__VA_ARGS__);
#else
#define DEBUG_INIT(x)
#define DEBUG_MSG(...)
#endif
void logDebug(const char *module, const char *format, ...)
{
  va_list arg;
  va_start(arg, format);
  // Serial.printf("[%s] ", module);
  char tempFormat[3 + strlen(module) + strlen(format) + 1] = "";

  strcat(tempFormat, "[");
  strcat(tempFormat + 1, module);
  strcat(tempFormat + 1 + strlen(module), "] ");
  strcat(tempFormat + 1 + strlen(module) + 2, format);
  char temp[64];
  char *buffer = temp;
  size_t len = vsnprintf(temp, sizeof(temp), tempFormat, arg);
  va_end(arg);
  if (len > sizeof(temp) - 1)
  {
    buffer = new (std::nothrow) char[len + 1];
    if (!buffer)
    {
      return;
    }
    va_start(arg, format);
    vsnprintf(buffer, len + 1, format, arg);
    va_end(arg);
  }
  len = Serial.write((const uint8_t *)buffer, len);
  if (buffer != temp)
  {
    delete[] buffer;
  }
}

#endif