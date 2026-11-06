// Модели узлов, инструментов и расходников из примитивов: аккумулятор с банками и клеммами, свечи с
// изолятором и шестигранником, трамблёр с крышкой и проводами, карбюратор с поплавковой камерой, круглый
// корпус воздушного фильтра с заборником, радиатор с сотами и бачками, генератор с рёбрами и шкивом,
// турбина «улиткой», глушитель, пружины и амортизаторы, фары, щиток, магнитола, болгарка, домкрат, баллоны…
// Одна и та же модель — под капотом, на земле, на полке и в руках. Цвет — в цвете вершин (материал белый).
#pragma once

#include "CoreMinimal.h"

struct FPeregonMeshBuffers;

namespace PeregonPartMeshes
{
// Есть ли своя модель у предмета.
PEREGON_API bool Has(FName ItemId);
// Добавить модель предмета в буфер. Where — основание (низ по центру) и поворот; Condition 0..1 — износ
// (тусклее, ржавее); Tint — цвет краски для баллона.
PEREGON_API void Build(FName ItemId, FPeregonMeshBuffers& Out, const FTransform& Where, float Condition = 1.f, const FLinearColor& Tint = FLinearColor::White);
// Узел, стоящий в машине (свечи — с колпачками и проводами к трамблёру).
PEREGON_API void BuildInstalled(int32 SlotIndex, FPeregonMeshBuffers& Out, const FTransform& Where, float Condition);
// Габарит модели (см) — чтобы уложить на полку или уменьшить в руках.
PEREGON_API FVector Size(FName ItemId);
} // namespace PeregonPartMeshes
