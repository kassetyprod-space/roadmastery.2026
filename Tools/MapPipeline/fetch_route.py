#!/usr/bin/env python3
"""Выгрузка настоящей геометрии трассы Р-255 «Сибирь» (Новосибирск — Кемерово) из OpenStreetMap.

Запуск (нужен только Python 3, без сторонних библиотек):
    python Tools/MapPipeline/fetch_route.py
Результат: Content/Data/Routes/r255.json — игра подхватит его сама при следующем запуске
(в Output Log: «[Peregon] Геометрия Р-255 из OSM: N точек»).

Что делает:
  1. Запрашивает у Overpass API отношение route=road с ref «Р-255» (кириллическая Р) и геометрию его путей.
  2. Сшивает пути в одну линию от Новосибирска к Кемерову (жадно, по ближайшим концам).
  3. Прореживает до точки на каждый километр, считает километраж от начала.
  4. Необязательно (--elev) добавляет высоты из OpenTopoData (SRTM 90 м).
Данные OSM © участники OpenStreetMap, лицензия ODbL — указывайте это в титрах игры.
"""
import argparse
import json
import math
import os
import sys
import time
import urllib.parse
import urllib.request

OVERPASS = "https://overpass-api.de/api/interpreter"
START = (55.105, 82.955)   # Новосибирск, северный выезд (совпадает с rgen::NovosibirskKemerovoOrigin)
FINISH = (55.355, 86.087)  # Кемерово
QUERY = """
[out:json][timeout:180];
(
  relation["type"="route"]["route"="road"]["ref"~"^(Р|P|R)[- ]?255$"];
);
way(r);
out geom;
"""


def haversine(a, b):
    r = 6371.0
    la1, lo1, la2, lo2 = map(math.radians, (a[0], a[1], b[0], b[1]))
    d = math.sin((la2 - la1) / 2) ** 2 + math.cos(la1) * math.cos(la2) * math.sin((lo2 - lo1) / 2) ** 2
    return 2 * r * math.asin(math.sqrt(d))


def fetch_ways():
    data = urllib.parse.urlencode({"data": QUERY}).encode()
    req = urllib.request.Request(OVERPASS, data=data, headers={"User-Agent": "peregon-map-pipeline/1.0"})
    with urllib.request.urlopen(req, timeout=240) as resp:
        payload = json.load(resp)
    ways = []
    for el in payload.get("elements", []):
        if el.get("type") == "way" and el.get("geometry"):
            ways.append([(p["lat"], p["lon"]) for p in el["geometry"]])
    return ways


def in_box(p):
    # Только участок между Новосибирском и Кемеровом (с запасом).
    return 54.9 < p[0] < 56.0 and 82.7 < p[1] < 86.3


def chain(ways):
    ways = [w for w in ways if any(in_box(p) for p in w)]
    line = []
    used = [False] * len(ways)
    cur = START
    while True:
        best, best_d, rev = -1, 1e9, False
        for i, w in enumerate(ways):
            if used[i]:
                continue
            d0, d1 = haversine(cur, w[0]), haversine(cur, w[-1])
            if d0 < best_d:
                best, best_d, rev = i, d0, False
            if d1 < best_d:
                best, best_d, rev = i, d1, True
        limit = 30.0 if not line else 0.6  # первый путь ищем в радиусе 30 км, дальше — стык не длиннее 600 м
        if best < 0 or best_d > limit:
            break
        used[best] = True
        seg = list(reversed(ways[best])) if rev else ways[best]
        line.extend(seg if not line else seg[1:])
        cur = line[-1]
        if haversine(cur, FINISH) < 3.0:
            break
    return line


def resample(line, step_km=1.0):
    out = [(line[0][0], line[0][1], 0.0)]
    km = 0.0
    next_km = step_km
    for a, b in zip(line, line[1:]):
        d = haversine(a, b)
        while km + d >= next_km and d > 0:
            t = (next_km - km) / d
            out.append((a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, next_km))
            next_km += step_km
        km += d
    out.append((line[-1][0], line[-1][1], km))
    return out


def add_elevation(points):
    res = []
    for i in range(0, len(points), 90):
        chunk = points[i:i + 90]
        locs = "|".join(f"{p[0]:.5f},{p[1]:.5f}" for p in chunk)
        url = "https://api.opentopodata.org/v1/srtm90m?locations=" + urllib.parse.quote(locs, safe="|,.")
        with urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": "peregon-map-pipeline/1.0"}), timeout=60) as r:
            res.extend(x.get("elevation") for x in json.load(r)["results"])
        time.sleep(1.1)  # бесплатный лимит: 1 запрос в секунду
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--elev", action="store_true", help="добавить высоты SRTM (OpenTopoData)")
    ap.add_argument("--out", default=os.path.join(os.path.dirname(__file__), "..", "..", "Content", "Data", "Routes", "r255.json"))
    args = ap.parse_args()

    print("Запрос к Overpass API…")
    ways = fetch_ways()
    print(f"  путей: {len(ways)}")
    line = chain(ways)
    if len(line) < 50:
        sys.exit("Не удалось сшить трассу — попробуйте позже или проверьте запрос в overpass-turbo.eu")
    total = sum(haversine(a, b) for a, b in zip(line, line[1:]))
    print(f"  сшито {len(line)} точек, {total:.1f} км; конец в {haversine(line[-1], FINISH):.1f} км от Кемерова")
    pts = resample(line)
    elev = add_elevation(pts) if args.elev else [None] * len(pts)
    data = {
        "name": "Р-255 «Сибирь»: Новосибирск — Кемерово",
        "source": "© OpenStreetMap contributors, ODbL",
        "points": [dict(lat=round(p[0], 6), lon=round(p[1], 6), km=round(p[2], 3), **({"elev": e} if e is not None else {}))
                   for p, e in zip(pts, elev)],
    }
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=1)
    print(f"Готово: {os.path.abspath(args.out)} ({len(pts)} точек)")


if __name__ == "__main__":
    main()
