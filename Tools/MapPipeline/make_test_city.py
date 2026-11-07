#!/usr/bin/env python3
"""Синтетический «ответ Overpass» для проверки конвейера города без сети.

Это НЕ Новосибирск — схема с теми же элементами, что встречаются в настоящей выгрузке: проспект с кольцом и
светофорами, сетка улиц, путепровод, река с островом и мостом «Октябрьский мост», театр «оперы и балета»,
кварталы, парк, железная дорога, выезд к трассе. Нужен тестам (Tests/RoadGen) и превью.

    python Tools/MapPipeline/make_test_city.py > Tools/MapPipeline/data/test.osm.json
    python Tools/MapPipeline/fetch_city.py novosibirsk --input Tools/MapPipeline/data/test.osm.json --out Tests/RoadGen/data/test.city
"""
import json
import math
import random

LAT0, LON0 = 55.0302, 82.9204
MLAT = 111320.0
MLON = 111320.0 * math.cos(math.radians(LAT0))
els = []
_next = [1]


def nid():
    _next[0] += 1
    return _next[0]


def ll(n, e):
    return {"lat": LAT0 + n / MLAT, "lon": LON0 + e / MLON}


node_at = {}


def node(n, e, tags=None):
    key = (round(n, 1), round(e, 1))
    if key in node_at:
        i = node_at[key]
        if tags:
            for el in els:
                if el["type"] == "node" and el["id"] == i:
                    el.setdefault("tags", {}).update(tags)
                    return i
            g = ll(n, e)
            els.append({"type": "node", "id": i, "lat": g["lat"], "lon": g["lon"], "tags": tags})
        return i
    i = nid()
    node_at[key] = i
    if tags:
        g = ll(n, e)
        els.append({"type": "node", "id": i, "lat": g["lat"], "lon": g["lon"], "tags": tags})
    return i


def way(points, tags, step=40.0):
    """Путь по точкам (север, восток) с промежуточными узлами через step м."""
    pts = []
    for (a, b) in zip(points, points[1:]):
        d = math.hypot(b[0] - a[0], b[1] - a[1])
        k = max(1, int(d // step))
        for i in range(k):
            t = i / k
            pts.append((a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t))
    pts.append(points[-1])
    ids = [node(n, e) for n, e in pts]
    els.append({"type": "way", "id": nid(), "nodes": ids, "geometry": [ll(n, e) for n, e in pts], "tags": tags})
    return ids


def poly(points, tags):
    ring = points + [points[0]]
    els.append({"type": "way", "id": nid(), "nodes": [nid() for _ in ring], "geometry": [ll(n, e) for n, e in ring], "tags": tags})


# Красный проспект: с юга на север, на площади — кольцо. Пересечения — общими узлами, как в OSM.
R = 70.0
ring_pts = [(R * math.cos(a), R * math.sin(a)) for a in [2 * math.pi * k / 24 for k in range(24)]]
way(ring_pts + [ring_pts[0]], {"highway": "primary", "junction": "roundabout", "lanes": "3", "name": "площадь Ленина"}, step=1000)
CROSS_N = [-900, -450, 450, 900, 1350]
LANES_E = [-1000, -600, 600, 1000]
way([(-1500, 0)] + [(n, 0) for n in CROSS_N if n < 0] + [(-R, 0)], {"highway": "primary", "lanes": "6", "name": "Красный проспект"})
way([(R, 0)] + [(n, 0) for n in CROSS_N if n > 0] + [(1750, 0)], {"highway": "primary", "lanes": "6", "name": "Красный проспект", "maxspeed": "60"})
way([(1750, 0), (3200, 0), (6000, 1200), (8300, 2240)], {"highway": "trunk", "lanes": "4", "name": "Мочищенское шоссе", "maxspeed": "90"})
way([(0, -R), (0, -600), (0, -1000), (0, -1400)], {"highway": "secondary", "name": "ул. Ленина"})
way([(0, R), (0, 600), (0, 1000), (0, 1400)], {"highway": "secondary", "name": "ул. Кирова"})
# Поперечные улицы со светофорами на проспекте.
for n, name in [(-900, "ул. Октябрьская"), (-450, "ул. Фрунзе"), (450, "ул. Гоголя"), (900, "ул. Писарева"), (1350, "ул. Державина")]:
    node(n, 0, {"highway": "traffic_signals"})
    way([(n, -1400), (n, -1000), (n, -600), (n, 0)], {"highway": "tertiary", "name": name})
    way([(n, 0), (n, 30), (n, 600), (n, 1000), (n, 1400)], {"highway": "tertiary", "name": name})
    node(n, 30, {"highway": "crossing"})
# Дворовые проезды пересекают поперечные улицы.
for e in LANES_E:
    pts = [(-1400, e)] + [(n, e) for n in sorted(CROSS_N + [0])] + [(1400, e)]
    way(pts, {"highway": "residential", "name": "ул. Дворовая"})
# Путепровод: улица над проспектом (с проспектом не пересекается — над ним).
way([(1800, -1400), (1800, -1000), (1800, -600), (1800, -60)], {"highway": "primary", "lanes": "4", "name": "ул. Ипподромская"})
way([(1800, -60), (1800, 60)], {"highway": "primary", "lanes": "4", "name": "ул. Ипподромская", "bridge": "yes", "layer": "1"})
way([(1800, 60), (1800, 600), (1800, 1000), (1800, 1400)], {"highway": "primary", "lanes": "4", "name": "ул. Ипподромская"})
# Набережная и мост через реку.
way([(-1500, 0), (-1500, -1200)], {"highway": "primary", "lanes": "6", "name": "ул. Большевистская"})
way([(-1500, -1200), (-1550, -1700)], {"highway": "primary", "lanes": "6", "name": "ул. Большевистская"})
way([(-1550, -1700), (-1750, -3500)], {"highway": "primary", "lanes": "6", "name": "Октябрьский мост", "bridge": "yes", "layer": "1"})
way([(-1750, -3500), (-1800, -4300)], {"highway": "primary", "lanes": "6", "name": "ул. Станиславского"})
node(-1500, 0, {"highway": "give_way"})

# Река (с островом): мультиполигон.
river_out = [(-6000, -1900), (6000, -1500), (6000, -3300), (-6000, -3700)]
island = [(-500, -2500), (300, -2450), (300, -2750), (-500, -2800)]
rel = {"type": "relation", "id": nid(), "tags": {"natural": "water", "name": "Обь"}, "members": [
    {"type": "way", "role": "outer", "geometry": [ll(n, e) for n, e in river_out + [river_out[0]]]},
    {"type": "way", "role": "inner", "geometry": [ll(n, e) for n, e in island + [island[0]]]}]}
els.append(rel)
poly([(-300, 450), (-300, 900), (-700, 900), (-700, 450)], {"leisure": "park", "name": "Первомайский сквер"})
way([(-3000, 3000), (0, 2200), (4000, 1500)], {"railway": "rail"}, step=200)

# Оперный театр к северо-востоку от площади.
poly([(130, 170), (130, 290), (240, 290), (240, 170)], {"building": "yes", "name": "Новосибирский театр оперы и балета", "building:levels": "6"})

# Кварталы: в центре — «сталинки» и доходные дома, дальше — панельки, за городом — частный сектор.
random.seed(7)
for bn in range(-1350, 1350, 450):
    for be in range(-1350, 1350, 400):
        if abs(bn) < 300 and abs(be) < 300:
            continue
        for k in range(3):
            n0 = bn + 60 + k * 120
            e0 = be + 60 + random.uniform(-15, 15)
            if abs(e0) < 60 or abs(n0) < 60:
                continue
            L, W = random.choice([(60, 13), (90, 13), (40, 15), (25, 22)])
            lv = random.choice([4, 5, 5, 9, 9, 12, 16]) if math.hypot(bn, be) > 800 else random.choice([3, 4, 5, 6])
            poly([(n0, e0), (n0, e0 + L), (n0 + W, e0 + L), (n0 + W, e0)], {"building": "apartments", "building:levels": str(lv)})
for k in range(80):  # частный сектор вдоль шоссе — в сжатой зоне
    t = random.uniform(0.2, 1.0)
    n = 3200 + (8300 - 3200) * t + random.uniform(-150, 150)
    e = 0 + 2240 * t + random.choice([-1, 1]) * random.uniform(40, 220)
    poly([(n, e), (n, e + 10), (n + 8, e + 10), (n + 8, e)], {"building": "house"})
poly([(2400, 300), (2400, 420), (2480, 420), (2480, 300)], {"building": "industrial"})

print(json.dumps({"elements": els}, ensure_ascii=False))
