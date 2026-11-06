// Peregon — RoadGenCore
// Экспорт символов из DLL модуля в Unreal. В CMake-сборке тестов макрос пустой.
#pragma once

#if defined(ROADGENCORE_API)
	#include "HAL/Platform.h"
	#define RGEN_API ROADGENCORE_API
	#ifdef _MSC_VER
		#pragma warning(disable : 4251)
	#endif
#else
	#define RGEN_API
#endif
