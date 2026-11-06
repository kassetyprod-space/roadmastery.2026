# Превью модели машины без Unreal

Модель «Сокол-1500» строится кодом (`Source/VehicleSimCore/Private/vsim/BodyModel.cpp`).
Посмотреть её можно в браузере:

```bash
cmake -S Tests -B build && cmake --build build
build/VehicleSim/vmodel_tests Tools/CarPreview/car.json   # проверки + выгрузка геометрии
cd Tools/CarPreview && python3 -m http.server 8000
```

Открыть `http://localhost:8000/viewer.html#eye=430,-380,110&tgt=0,0,-5`.
Параметры после `#`: `eye`, `tgt` — камера в сантиметрах (оси Unreal), `fov`,
`open=Hood,DoorFL,DoorRL,Trunk,Glovebox` — открытые части, `cull=0` — без отсечения
изнанок (по умолчанию включено: если где-то «дыра», значит треугольники вывернуты).

`shoot.py` делает скриншоты тех же видов через Playwright.
