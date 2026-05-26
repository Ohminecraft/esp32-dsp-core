#pragma once

#include <Arduino.h>

#if defined(CONFIG_SPIRAM)
  #define PSRAM_MALLOC(size) heap_caps_malloc((size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
  #define PSRAM_FREE(p)      heap_caps_free(p)
#else
  #define PSRAM_MALLOC(size) malloc(size)
  #define PSRAM_FREE(p)      free(p)
#endif