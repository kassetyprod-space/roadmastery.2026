# Peregon — автонастройка контента при запуске редактора.
#
# Unreal сам выполняет init_unreal.py из Content/Python, если включён PythonScriptPlugin.
# Скрипт создаёт мастер-материал M_PSX, если его ещё нет. Текстуры игра генерирует
# в коде (UPeregonArtSubsystem), материал нужен один на всё: текстура + цвет + шероховатость.
#
# Пересоздать материал: удалите /Game/Peregon/Materials/M_PSX и перезапустите редактор.

import unreal

MATERIAL_DIR = "/Game/Peregon/Materials"
MATERIAL_NAME = "M_PSX"
MATERIAL_PATH = MATERIAL_DIR + "/" + MATERIAL_NAME

_handle = None


def _log(msg):
    unreal.log("[Peregon] " + msg)


def _set_usage(material):
    # Без этих флагов в собранной игре инстансы деревьев и процедурная геометрия получат серый материал.
    for prop in ("used_with_instanced_static_meshes", "used_with_static_lighting"):
        try:
            material.set_editor_property(prop, True)
        except Exception:
            pass
    try:
        unreal.MaterialEditingLibrary.set_material_usage(material, unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)
    except Exception:
        pass


def create_psx_material():
    if unreal.EditorAssetLibrary.does_asset_exist(MATERIAL_PATH):
        return
    _log("Создаю " + MATERIAL_PATH)
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    mel = unreal.MaterialEditingLibrary
    material = tools.create_asset(MATERIAL_NAME, MATERIAL_DIR, unreal.Material, unreal.MaterialFactoryNew())
    if material is None:
        unreal.log_error("[Peregon] Не удалось создать материал")
        return

    # Двусторонний: процедурная геометрия видна с любой стороны.
    material.set_editor_property("two_sided", True)
    _set_usage(material)

    # UV: либо из меша, либо мировые (для боксов полигона: текстура не растягивается на 1 км).
    texcoord = mel.create_material_expression(material, unreal.MaterialExpressionTextureCoordinate, -1400, -100)
    worldpos = mel.create_material_expression(material, unreal.MaterialExpressionWorldPosition, -1600, 100)
    mask = mel.create_material_expression(material, unreal.MaterialExpressionComponentMask, -1400, 100)
    mask.set_editor_property("r", True)
    mask.set_editor_property("g", True)
    mask.set_editor_property("b", False)
    mask.set_editor_property("a", False)
    mel.connect_material_expressions(worldpos, "", mask, "")
    tile = mel.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -1400, 250)
    tile.set_editor_property("parameter_name", "TileSize")
    tile.set_editor_property("default_value", 400.0)
    divide = mel.create_material_expression(material, unreal.MaterialExpressionDivide, -1200, 150)
    mel.connect_material_expressions(mask, "", divide, "A")
    mel.connect_material_expressions(tile, "", divide, "B")
    world_uv = mel.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -1200, 300)
    world_uv.set_editor_property("parameter_name", "WorldUV")
    world_uv.set_editor_property("default_value", 0.0)
    uv_lerp = mel.create_material_expression(material, unreal.MaterialExpressionLinearInterpolate, -1000, 0)
    mel.connect_material_expressions(texcoord, "", uv_lerp, "A")
    mel.connect_material_expressions(divide, "", uv_lerp, "B")
    mel.connect_material_expressions(world_uv, "", uv_lerp, "Alpha")

    tex = mel.create_material_expression(material, unreal.MaterialExpressionTextureSampleParameter2D, -800, 0)
    tex.set_editor_property("parameter_name", "Tex")
    default_tex = unreal.load_asset("/Engine/EngineResources/WhiteSquareTexture")
    if default_tex:
        tex.set_editor_property("texture", default_tex)
    mel.connect_material_expressions(uv_lerp, "", tex, "UVs")

    color = mel.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -800, 300)
    color.set_editor_property("parameter_name", "Color")
    color.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))

    multiply = mel.create_material_expression(material, unreal.MaterialExpressionMultiply, -450, 100)
    mel.connect_material_expressions(tex, "RGB", multiply, "A")
    mel.connect_material_expressions(color, "", multiply, "B")
    mel.connect_material_property(multiply, "", unreal.MaterialProperty.MP_BASE_COLOR)

    roughness = mel.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -450, 350)
    roughness.set_editor_property("parameter_name", "Roughness")
    roughness.set_editor_property("default_value", 0.8)
    mel.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)

    metallic = mel.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -450, 500)
    metallic.set_editor_property("parameter_name", "Metallic")
    metallic.set_editor_property("default_value", 0.0)
    mel.connect_material_property(metallic, "", unreal.MaterialProperty.MP_METALLIC)

    # Свечение (фары, фонари, приборы ночью).
    emissive = mel.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -450, 650)
    emissive.set_editor_property("parameter_name", "Emissive")
    emissive.set_editor_property("default_value", 0.0)
    emissive_mul = mel.create_material_expression(material, unreal.MaterialExpressionMultiply, -250, 600)
    mel.connect_material_expressions(multiply, "", emissive_mul, "A")
    mel.connect_material_expressions(emissive, "", emissive_mul, "B")
    mel.connect_material_property(emissive_mul, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    mel.recompile_material(material)
    unreal.EditorAssetLibrary.save_asset(MATERIAL_PATH, only_if_is_dirty=False)
    _log("Материал готов. Его стоит закоммитить в git (Content/Peregon/Materials/M_PSX.uasset).")


def _on_tick(delta_seconds):
    # Ждём первый кадр редактора: к этому моменту реестр ассетов уже загружен.
    global _handle
    unreal.unregister_slate_post_tick_callback(_handle)
    try:
        create_psx_material()
    except Exception as error:
        unreal.log_error("[Peregon] Ошибка создания материала: {}".format(error))


_handle = unreal.register_slate_post_tick_callback(_on_tick)
