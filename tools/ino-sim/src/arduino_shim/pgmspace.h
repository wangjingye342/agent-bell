// pgmspace.h — host shim for AVR/ESP program-memory macros.
//
// On real hardware these read from flash; on the host, "flash" is just normal
// memory, so every accessor degrades to a plain dereference. Adafruit_GFX.cpp
// includes <pgmspace.h> when ESP32 is defined and also provides its own
// fallbacks, but other libraries (and sketches) include this directly.
#pragma once
#include <cstdint>
#include <cstring>

#ifndef PROGMEM
#define PROGMEM
#endif
#ifndef PGM_P
#define PGM_P const char *
#endif
#ifndef PGM_VOID_P
#define PGM_VOID_P const void *
#endif
#ifndef F
#define F(x) (x)
#endif

#ifndef pgm_read_byte
#define pgm_read_byte(addr) (*(const uint8_t *)(addr))
#endif
#ifndef pgm_read_byte_near
#define pgm_read_byte_near(addr) (*(const uint8_t *)(addr))
#endif
#ifndef pgm_read_word
#define pgm_read_word(addr) (*(const uint16_t *)(addr))
#endif
#ifndef pgm_read_word_near
#define pgm_read_word_near(addr) (*(const uint16_t *)(addr))
#endif
#ifndef pgm_read_dword
#define pgm_read_dword(addr) (*(const uint32_t *)(addr))
#endif
#ifndef pgm_read_float
#define pgm_read_float(addr) (*(const float *)(addr))
#endif
#ifndef pgm_read_ptr
#define pgm_read_ptr(addr) (*(void *const *)(addr))
#endif

// String functions operate on normal memory on the host.
#ifndef memcpy_P
#define memcpy_P memcpy
#endif
#ifndef strcpy_P
#define strcpy_P strcpy
#endif
#ifndef strncpy_P
#define strncpy_P strncpy
#endif
#ifndef strlen_P
#define strlen_P strlen
#endif
#ifndef strcmp_P
#define strcmp_P strcmp
#endif
#ifndef sprintf_P
#define sprintf_P sprintf
#endif
#ifndef snprintf_P
#define snprintf_P snprintf
#endif
