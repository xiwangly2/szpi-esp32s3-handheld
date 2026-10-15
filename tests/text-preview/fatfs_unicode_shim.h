#pragma once
// Compile the unchanged FatFs Unicode tables on the host without ESP/RTOS headers.
#include <stdint.h>
#define FF_DEFINED 80286
#define FF_USE_LFN 3
#define FF_CODE_PAGE 936
typedef uint16_t WCHAR;
typedef uint16_t WORD;
typedef uint32_t DWORD;
typedef unsigned int UINT;
typedef uint8_t BYTE;
WCHAR ff_oem2uni(WCHAR oem, WORD cp);
