#ifndef __SIMNATIVE_H__
#define __SIMNATIVE_H__

#define WIN32_LEAN_AND_MEAN		// Exclude rarely-used stuff from Windows headers
#include <windows.h>

#ifdef ARMLETDLL_EXPORTS
#define ARMLETDLL_API __declspec(dllexport)
#else
#define ARMLETDLL_API __declspec(dllimport)
#endif

#include "PceNativeCall.h"

#ifdef __cplusplus
extern "C" {
#endif 

ARMLETDLL_API unsigned long ARMlet_Main(const void* emulStateP, void *userData68KP, Call68KFuncType *call68KFuncP);

#ifdef __cplusplus
}
#endif 

#endif // __SIMNATIVE_H__
