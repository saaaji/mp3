#pragma once

#define APP_NAME "mp3-fw"

#define ENABLE_LOGGING
#ifdef ENABLE_LOGGING
  #define LOG(...) do {\
    fprintf(stderr, "[%s %s:%d] ", APP_NAME, __FILE__, __LINE__);\
    fprintf(stderr, __VA_ARGS__);\
    fprintf(stderr, "\n");\
  } while (0)
#else
  #define LOG(...) ((void) 0)
#endif

#define CHECK(val, ...) do {\
  if (!(val)) {\
    LOG(__VA_ARGS__);\
    esp_restart();\
  }\
} while (0)

#define CHECK_EQ(v1, v2, ...) CHECK(v1 == v2, __VA_ARGS__)
#define CHECK_NEQ(v1, v2, ...) CHECK(v1 != v2, __VA_ARGS__)