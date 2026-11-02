// Peregon — VehicleSimCore
// Экспорт символов из DLL модуля в Unreal. В CMake-сборке тестов макрос пустой.
#pragma once

#if defined(VEHICLESIMCORE_API)
	// UBT задаёт VEHICLESIMCORE_API=DLLEXPORT/DLLIMPORT; сами DLLEXPORT/DLLIMPORT объявлены в Core.
	#include "HAL/Platform.h"
	#define VSIM_API VEHICLESIMCORE_API
	#ifdef _MSC_VER
		// Классы с std::vector внутри: предупреждение про dll-interface здесь безопасно.
		#pragma warning(disable : 4251)
	#endif
#else
	#define VSIM_API
#endif
