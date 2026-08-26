#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <SKSE/Impl/PCH.h>

// CommonLibSSE-NG 6.x wraps the Win32 SDK under REX::W32. Load the RE surface
// before project headers include the native Windows SDK so its macro constants
// cannot collide with the wrapped names (for example MEM_RELEASE).
#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

using namespace std::literals;
