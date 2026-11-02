// Peregon — VehicleSimCore
// Готовые конфигурации машин. Все машины вымышленные, цифры — правдоподобные для своей эпохи.
#pragma once

#include "vsim/Vehicle.h"

namespace vsim
{
namespace Presets
{
// «Сокол-1500»: классический заднеприводный седан 70-х. 1.5 л, ~71 л.с., 5-ступенчатая КПП.
// Стартовая машина перегона: простая, ремонтопригодная, не быстрая.
VehicleConfig Sokol1500();
} // namespace Presets
} // namespace vsim
